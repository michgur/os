#include "PhysicalMemory.h"
#include "VirtualMemory.h"

#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  VMinitialize();

  // test - replicate algorithm example (works with OFFSET_WIDTH = 1,
  // PHYSICAL_ADDRESS_WIDTH = 4, VIRTUAL_ADDRESS_WIDTH = 5)
  //   int data[] = {1, 4, 5, 0, 0, 7, 0, 2, 0, 3, 0, 6, 0, 0};
  //   VMwrite(13, 3);
  //   word_t value;
  //   VMread(13, &value);
  //   assert(value == 3);
  //   VMread(6, &value);
  //   VMread(31, &value);

  //   for (int i = 0; i < 14; i++) {
  //     PMread(i, &value);
  //     assert(value == data[i]);
  //   }
  //   printf("PASSED\n");

  // given test (works with default constants)
  for (uint64_t i = 0; i < (2 * NUM_FRAMES); ++i) {
    printf("writing to %llu\n", (long long int)i);
    VMwrite(5 * i * PAGE_SIZE, i);
  }

  for (uint64_t i = 0; i < (2 * NUM_FRAMES); ++i) {
    word_t value;
    VMread(5 * i * PAGE_SIZE, &value);
    printf("reading from %llu %d\n", (long long int)i, value);
    assert(uint64_t(value) == i);
  }
  printf("success\n");

  return 0;
}
