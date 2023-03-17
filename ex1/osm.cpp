#include "osm.h"
#include <sys/time.h>

double gettimeofdaynano() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000.0;
}

void nop() {}

double osm_operation_time(unsigned int iterations) {
  if (iterations == 0) {
    return -1;
  }

  int a = 0;
  double start = gettimeofdaynano();
  for (unsigned int i = 0; i < iterations; i++) {
    a++;
  }
  return gettimeofdaynano() - start;
}

double osm_function_time(unsigned int iterations) {
  if (iterations == 0) {
    return -1;
  }

  double start = gettimeofdaynano();
  for (unsigned int i = 0; i < iterations; i++) {
    nop();
  }
  return gettimeofdaynano() - start;
}

double osm_syscall_time(unsigned int iterations) {
  if (iterations == 0) {
    return -1;
  }

  double start = gettimeofdaynano();
  for (unsigned int i = 0; i < iterations; i++) {
    OSM_NULLSYSCALL;
  }
  return gettimeofdaynano() - start;
}
