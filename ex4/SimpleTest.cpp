#include "PhysicalMemory.h"
#include "VirtualMemory.h"

#include <cassert>
#include <cstdio>
#include <unordered_map>

using PhysicalAddressToValueMap = std::unordered_map<uint64_t, word_t>;

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
  // for (uint64_t i = 0; i < (2 * NUM_FRAMES); ++i) {
  //   printf("writing to %llu\n", (long long int)i);
  //   VMwrite(5 * i * PAGE_SIZE, i);
  // }

  // for (uint64_t i = 0; i < (2 * NUM_FRAMES); ++i) {
  //   word_t value;
  //   VMread(5 * i * PAGE_SIZE, &value);
  //   printf("reading from %llu %d\n", (long long int)i, value);
  //   assert(uint64_t(value) == i);
  // }
  // printf("success\n");

  // MAAYAN test
  for (int i = 0; i < RAM_SIZE; i++) {
    PMwrite(i, 0);
  }
  uint64_t addr = 0b10001011101101110011;
  assert(VMwrite(addr, 1337) == 1);

  // The offsets(for the page tables) of the above virtual address are
  // 8, 11, 11, 7, 3
  // Therefore, since we're starting with a completely empty table
  // every page table found will be according to the second criteria in the
  // PDF(an unused frame), and not the first one(as all frames containing
  // empty tables are exactly those we just created during the current address
  // translation, thus they can't be used)
  //
  // So, we expect the following to occur:
  // write(8, 1)     <- table at physical addr 0, offset is 8, next table at
  // frame index 1 write(27, 2)    <- table at physical addr 16, offset is 11,
  // next table at frame index 2 write(43, 3)    <- table at physical addr 32,
  // offset is 11, next table at frame index 2 write(55, 4)    <- table at
  // physical addr 48, offset is 7, next table at frame index 3 write(67,
  // 1337) <- table at physical addr 64, offset is 3, write 1337 in this
  // address.

  PhysicalAddressToValueMap expected{
      {8, 1}, {27, 2}, {43, 3}, {55, 4}, {67, 1337}};

  PhysicalAddressToValueMap got;
  for (const auto &kvp : expected) {
    word_t gottenValue;
    PMread(kvp.first, &gottenValue);
    got[kvp.first] = gottenValue;
  }

  assert(expected == got);

  word_t res;
  assert(VMread(addr, &res) == 1);
  assert(res == 1337);

  return 0;
}
