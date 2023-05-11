#include "MapReduceFramework.h"
#include "Barrier.h"
#include <array>
#include <atomic>
#include <map>
#include <pthread.h>
#include <semaphore.h>
#include <vector>
#include <iostream>
#include <algorithm>

#define SAFE(x)                                                                \
  if ((x) != 0) {                                                              \
    std::cerr << "[[MapReduceFramework]] error on " #x << std::endl;           \
    exit(1);                                                                   \
  }

class JobContext;

class MapReduceJob { // todo: consolidate design (remove JobContext, separate
                     // threadpool and job)
private:
  JobContext *context;
  // number of threads
  int numThreads;
  // map pthread ID to tid (index in pool)
  std::map<pthread_t, int> threadpool;
  // intermediate vectors creates, in the map phase and rearranged in the
  // shuffle phase
  std::vector<IntermediateVec> intermediateVectors;

  /********** Pool-level methods and synchronization ********/
  // stage of the job. only modified by thread 0
  stage_t stage;
  // atomic counter for tracking stage progress
  std::atomic<int> counter;
  // atomic counters for tacking number of pairs
  std::atomic<int> inputSize, intermediateSize;
  // barrier for sort phase
  Barrier barrier;
  // semaphore for shuffle phase
  sem_t shuffleSem;
  // mutex for outputVec
  pthread_mutex_t outputVecMutex;

  // find the key with the maximum value across all intermediate vectors.
  // assumes vecs are sorted
  K2 *findMaxKey();
  // shuffle intermediate vectors from threads to this->intermediateVectors
  void shuffle();
  // get currently running tid
  int currentTid();
  // static wrapper for run
  static void *startThread(void *arg);

  /********** Thread-level methods **************************/
  // run map-reduce
  void run(int tid);
  // map phase
  void map(int tid);
  // sort phase
  void sort(int tid);
  // shuffle phase
  void shuffle(int tid);
  // reduce phase
  void reduce(int tid);

public:
  MapReduceJob(JobContext *jobContext);
  ~MapReduceJob();

  void insert2(K2 *key, V2 *value);
  void insert3(K3 *key, V3 *value);

  void join();

  stage_t getStage();
  float getStatePercentage();

  friend class JobContext;
};

class JobContext {
private:
  const MapReduceClient &client;
  const InputVec &inputVec;
  OutputVec &outputVec;
  int multiThreadLevel;

  MapReduceJob job;

public:
  JobContext(const MapReduceClient &client, const InputVec &inputVec,
             OutputVec &outputVec, int multiThreadLevel);
  ~JobContext();

  void getState(JobState *state);
  void wait();

  friend class MapReduceJob;
};

/************** Library functions implementation *******/

JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
  JobContext *jobContext =
      new JobContext(client, inputVec, outputVec, multiThreadLevel);
  return static_cast<JobHandle>(jobContext);
}

void waitForJob(JobHandle job) {
  JobContext *jobContext = static_cast<JobContext *>(job);
  jobContext->wait();
}

void getJobState(JobHandle job, JobState *state) {
  JobContext *jobContext = static_cast<JobContext *>(job);
  jobContext->getState(state);
}

void closeJobHandle(JobHandle job) {
  JobContext *jobContext = static_cast<JobContext *>(job);
  delete jobContext;
}

void emit2(K2 *key, V2 *value, void *context) {
  MapReduceJob *job = static_cast<MapReduceJob *>(context);
  job->insert2(key, value);
}

void emit3(K3 *key, V3 *value, void *context) {
  MapReduceJob *job = static_cast<MapReduceJob *>(context);
  job->insert3(key, value);
}

/************** JobContext implementation **************/

JobContext::JobContext(const MapReduceClient &client, const InputVec &inputVec,
                       OutputVec &outputVec, int multiThreadLevel)
    : client(client), inputVec(inputVec), outputVec(outputVec),
      multiThreadLevel(multiThreadLevel), job(this) {}

JobContext::~JobContext() {}

void JobContext::getState(JobState *state) {
  state->stage = job.getStage();
  state->percentage = job.getStatePercentage();
}

void JobContext::wait() { job.join(); }

/************** MapReduceThreadPool implementation *****/

MapReduceJob::MapReduceJob(JobContext *jobContext)
    : context(jobContext), numThreads(jobContext->multiThreadLevel), counter(0), barrier(numThreads), intermediateVectors(numThreads) {
  // set stage to map
  stage = MAP_STAGE;
  // set input size
  inputSize = jobContext->inputVec.size();
  // create threads
  for (int i = 0; i < jobContext->multiThreadLevel; i++) {
    pthread_t thread;
    SAFE(pthread_create(&thread, nullptr, startThread, this));
    // store thread and index in pool
    threadpool[thread] = i;
  }

  // initialize synchronization objects
  SAFE(sem_init(&shuffleSem, 0, 0));
  SAFE(pthread_mutex_init(&outputVecMutex, nullptr));
}

MapReduceJob::~MapReduceJob() {
  // delete threads
  for (auto &pair : threadpool) {
    SAFE(pthread_cancel(pair.first));
  }
  // destroy synchronization objects
  SAFE(sem_destroy(&shuffleSem));
  SAFE(pthread_mutex_destroy(&outputVecMutex));
}

void MapReduceJob::run(int tid) {
  map(tid);
  sort(tid);
  shuffle(tid);
  reduce(tid);
}

void MapReduceJob::map(int tid) {
  int index = -1;
  while ((index = counter.fetch_add(1)) < inputSize.load()) {
    const InputPair &p = context->inputVec[index];
    context->client.map(p.first, p.second, this);
  }
}

void MapReduceJob::sort(int tid) {
  // sort by key
  std::sort(intermediateVectors[tid].begin(), intermediateVectors[tid].end(),
            [](const IntermediatePair &p1, const IntermediatePair &p2) {
              return *p1.first < *p2.first;
            });
  // wait for all threads to finish this phase
  barrier.barrier();
}

void MapReduceJob::shuffle(int tid) {
  if (tid != 0) {
    // wait for thread 0 to finish shuffling
    SAFE(sem_wait(&shuffleSem));
  } else {
    // horizontal shuffle across all pairs
    shuffle();
    // wake up all threads
    SAFE(sem_post(&shuffleSem));
  }
}

void MapReduceJob::reduce(int tid) {
  int index = -1;
  while ((index = counter.fetch_add(1)) < intermediateVectors.size()) {
    context->client.reduce(&intermediateVectors[index], this);
  }
}

K2 *MapReduceJob::findMaxKey() {
  K2 *max = nullptr;
  for (const IntermediateVec &vec : intermediateVectors) {
    if (!vec.empty() && (max == nullptr || *max < *vec.back().first)) {
      max = vec.back().first;
    }
  }
  return max;
}

void MapReduceJob::shuffle() {
  // set stage (thread safe, only thread 0 writes) and reset counter
  stage = SHUFFLE_STAGE;
  counter.store(0);

  K2 *key = nullptr;
  std::vector<IntermediateVec> result;
  // iterate over all pairs across all threads, ordered by key
  while ((key = findMaxKey()) != nullptr) {
    IntermediateVec resultVec;
    // insert all pairs with the same key to vec
    for (int i = 0; i < numThreads; i++) {
      IntermediateVec &threadVec = intermediateVectors[i];
      while (!(threadVec.empty() || *threadVec.back().first < *key ||
             *key < *threadVec.back().first)) {
        resultVec.push_back(threadVec.back());
        threadVec.pop_back();
      }
    }
    counter.fetch_add(resultVec.size());
    result.push_back(resultVec);
  }
  // update intermediate vectors
  intermediateVectors = result;
  // set stage and reset counter
  stage = REDUCE_STAGE;
  counter.store(0);
}

int MapReduceJob::currentTid() {
  pthread_t t = pthread_self();
  return threadpool[t];
}

void MapReduceJob::insert2(K2 *key, V2 *value) {
  int tid = currentTid();
  // insert pair to intermediate vector (thread-safe, each thread has its own)
  intermediateVectors[tid].push_back(IntermediatePair(key, value));
  // count intermediate pairs
  intermediateSize.fetch_add(1);
}

void MapReduceJob::insert3(K3 *key, V3 *value) {
  // lock output vector mutex
  SAFE(pthread_mutex_lock(&outputVecMutex));
  // insert pair to output vector
  context->outputVec.push_back(OutputPair(key, value));
  // unlock output vector mutex
  SAFE(pthread_mutex_unlock(&outputVecMutex));
}

void *MapReduceJob::startThread(void *arg) {
  MapReduceJob *job = static_cast<MapReduceJob *>(arg);
  job->run(job->currentTid());
  return nullptr;
}

void MapReduceJob::join() {
  for (auto &pair : threadpool) {
    SAFE(pthread_join(pair.first, nullptr));
  }
}

stage_t MapReduceJob::getStage() { return stage; }

float MapReduceJob::getStatePercentage() {
  int count = counter.load();
  int size = ((stage == MAP_STAGE) ? inputSize : intermediateSize).load();
  return ((float)count) / size * 100;
}
