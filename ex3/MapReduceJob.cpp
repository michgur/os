#include "MapReduceJob.h"
#include <algorithm>
#include <iostream>

// safe macro for error handling of system calls
#define SAFE(x)                                                                \
  if ((x) != 0) {                                                              \
    std::cerr << "[[MapReduceFramework]] error on " #x << std::endl;           \
    this->~MapReduceJob();                                                     \
    exit(1);                                                                   \
  }

MapReduceJob::MapReduceJob(const MapReduceClient &client,
                           const InputVec &inputVec, OutputVec &outputVec,
                           int numThreads)
    : client(client), inputVec(inputVec), outputVec(outputVec),
      numThreads(numThreads), intermediateVectors(numThreads), counter(0), barrier(numThreads)
       {
  // set stage to map
  stage = MAP_STAGE;
  // set input size
  inputSize = inputVec.size();
  // create threads
  for (int i = 0; i < numThreads; i++) {
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
    pthread_cancel(pair.first);
    pthread_join(pair.first, nullptr);
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
    const InputPair &p = inputVec[index];
    client.map(p.first, p.second, this);
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
  while ((index = counter.fetch_add(1)) < outputSize.load()) {
    client.reduce(&intermediateVectors[index], this);
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
  // set output size
  outputSize = intermediateVectors.size();
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
  outputVec.push_back(OutputPair(key, value));
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
  int size = ((stage == MAP_STAGE)
                  ? inputSize
                  : ((stage == SHUFFLE_STAGE) ? intermediateSize : outputSize))
                 .load();
  return ((float)std::min(count, size)) / size * 100;
}
