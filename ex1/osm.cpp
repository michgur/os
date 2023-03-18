#include "osm.h"
#include <sys/time.h>

double gettimeofdaynano() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000.0;
}

#define OSM_TIME(expr, iterations)                                             \
  ((iterations == 0) ? -1 : ({                                                 \
    double start = gettimeofdaynano();                                         \
    for (; iterations > 0; iterations--) {                                     \
      expr;                                                                    \
    }                                                                          \
    gettimeofdaynano() - start;                                                \
  }))

double osm_operation_time(unsigned int iterations) {
  int a = 0;
  return OSM_TIME(a++, iterations);
}

void nop() {}

double osm_function_time(unsigned int iterations) {
  return OSM_TIME(nop(), iterations);
}

double osm_syscall_time(unsigned int iterations) {
  return OSM_TIME(OSM_NULLSYSCALL, iterations);
}

/** Run the tests. */

#include <iostream>

int main() {
  std::cout << "Op. time: " << osm_operation_time(1000000) << std::endl;
  std::cout << "Fn. time: " << osm_function_time(1000000) << std::endl;
  std::cout << "Syscall time: " << osm_syscall_time(1000000) << std::endl;
  return 0;
}
