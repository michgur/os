#include "MapReduceFramework.h"
#include "MapReduceJob.h"
#include <iostream>

JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
  MapReduceJob *job =
      new MapReduceJob(client, inputVec, outputVec, multiThreadLevel);
  return static_cast<JobHandle>(job);
}

void waitForJob(JobHandle handle) {
  MapReduceJob *job = static_cast<MapReduceJob *>(handle);
  job->join();
}

void getJobState(JobHandle handle, JobState *state) {
  MapReduceJob *job = static_cast<MapReduceJob *>(handle);
  state->stage = job->getStage();
  state->percentage = job->getStatePercentage();
}

void closeJobHandle(JobHandle handle) {
  MapReduceJob *job = static_cast<MapReduceJob *>(handle);
  delete job;
}

void emit2(K2 *key, V2 *value, void *context) {
  MapReduceJob *job = static_cast<MapReduceJob *>(context);
  job->insert2(key, value);
}

void emit3(K3 *key, V3 *value, void *context) {
  MapReduceJob *job = static_cast<MapReduceJob *>(context);
  job->insert3(key, value);
}
