#include "osm.h"
#include <sys/time.h>

double gettimeofdaynano() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000.0;
}

void nop() {}

// double osm_operation_time(unsigned int iterations) {
//   if (iterations == 0) {
//     return -1;
//   }

//   int a = 0;
//   double start = gettimeofdaynano();
//   for (unsigned int i = 0; i < iterations; i++) {
//     a++;
//   }
//   return gettimeofdaynano() - start;
// }

// double osm_function_time(unsigned int iterations) {
//   if (iterations == 0) {
//     return -1;
//   }

//   double start = gettimeofdaynano();
//   for (unsigned int i = 0; i < iterations; i++) {
//     nop();
//   }
//   return gettimeofdaynano() - start;
// }

// double osm_syscall_time(unsigned int iterations) {
//   if (iterations == 0) {
//     return -1;
//   }

//   double start = gettimeofdaynano();
//   for (unsigned int i = 0; i < iterations; i++) {
//     OSM_NULLSYSCALL;
//   }
//   return gettimeofdaynano() - start;
// }

// use macro to avoid code duplication, and to avoid the overhead of function
// calls in the loop.
#define OSM_TIME(expr, iterations)                                             \
  ((iterations == 0) ? -1 : ({                                                 \
    double start = gettimeofdaynano();                                         \
    for (unsigned int i = 0; i < iterations; i++) {                            \
      expr;                                                                    \
    }                                                                          \
    gettimeofdaynano() - start;                                                \
  }))

// rewritten to use OSM_TIME macro
double osm_operation_time(unsigned int iterations) {
  int a = 0;
  return OSM_TIME(a++, iterations);
}

double osm_function_time(unsigned int iterations) {
  return OSM_TIME(nop(), iterations);
}

double osm_syscall_time(unsigned int iterations) {
  return OSM_TIME(OSM_NULLSYSCALL, iterations);
}
