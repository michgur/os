#include "Barrier.h"
#include "MapReduceFramework.h"
#include <atomic>
#include <map>
#include <pthread.h>
#include <semaphore.h>
#include <vector>

class MapReduceJob {
private:
  const MapReduceClient &client;
  const InputVec &inputVec;
  OutputVec &outputVec;
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
  // atomic flag for indicating if job is joined
  std::atomic<bool> joined;
  // atomic counter for tracking stage progress
  std::atomic<int> counter;
  // atomic counters for tacking number of pairs
  std::atomic<int> inputSize, intermediateSize, outputSize;
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
  // get currently running tid (index in pool)
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
  MapReduceJob(const MapReduceClient &client, const InputVec &inputVec,
               OutputVec &outputVec, int numThreads);
  ~MapReduceJob();

  void insert2(K2 *key, V2 *value);
  void insert3(K3 *key, V3 *value);

  void join();

  stage_t getStage();
  float getStatePercentage();
};