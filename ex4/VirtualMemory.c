#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define FAIL_STATUS 0
#define SUCCES_STATUS 1

#define OFFSET_MASK ((1 << OFFSET_WIDTH) - 1)
#define FRAME_INDEX(addr) (addr >> OFFSET_WIDTH)
#define FRAME_ADDR(index) (index << OFFSET_WIDTH)

void clear_frame(uint64_t addr) {
  for (int i = 0; i < PAGE_SIZE; i++) {
    PMwrite(addr + i, 0);
  }
}

void VMinitialize() { clear_frame(0); }

int translate(uint64_t *addr, int level);

int VMread(uint64_t addr, word_t *value) {
  printf("VMread: %llu\n", addr);
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr, 0) == FAIL_STATUS) {
    return FAIL_STATUS;
  }
  printf("PMread: %llu\n", addr);
  // read the value from the physical memory
  PMread(addr, value);
  return SUCCES_STATUS;
}

int VMwrite(uint64_t addr, word_t value) {
  printf("VMwrite: %llu\n", addr);
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr, 0) == FAIL_STATUS) {
    return FAIL_STATUS;
  }
  // write the value to the physical memory
  printf("PMwrite: %llu\n", addr);
  PMwrite(addr, value);
  return SUCCES_STATUS;
}

typedef struct {
  // index of page to insert
  uint64_t page_addr;
  // max cyclic distance from the new page
  uint64_t max_dist;
  // frame with the max cyclic distance
  uint64_t max_dist_frame_addr;
  // frame with the max index
  uint64_t max_index_frame_addr;

} dfs_context_t;

void update_context(dfs_context_t *context, uint64_t frame_addr, int level) {
  // update max index frame
  if (frame_addr > context->max_index_frame_addr) {
    context->max_index_frame_addr = frame_addr;
  }
  // update max distance frame
  // min{NUM_PAGES - |page_swapped_in - p|,|page_swapped_in - p|}
  int dist = abs((int)(context->page_addr - frame_addr));
  dist = dist < VIRTUAL_MEMORY_SIZE - dist ? dist : VIRTUAL_MEMORY_SIZE - dist;

  if (dist < context->max_dist) {
    context->max_dist = dist;
    context->max_dist_frame_addr = frame_addr;
  }
}

/**
 * Selects a frame to evict and insert the new page into.
 * Finds a table frame that consists of only 0s, or the first available frame,
 * or selects the frame with max cyclic dist if all frame are used.
 * @param addr the address to start the search from (should be a page address
 * with offset 0)
 * @param max_addr max physical address used for a frame
 * @param level the current level of search
 * @return 1 on success (result is in addr), 0 on failure (max_addr can be used)
 */
int dfs(dfs_context_t *context, uint64_t addr, int level, uint64_t *result) {
  update_context(context, addr, level);
  // reached the last level of the page table
  if (level == TABLES_DEPTH) {
    // frame is not a page table frame
    // *result = context->max_dist
    return FAIL_STATUS;
  }
  // if page consists of only 0s, return the current frame
  bool is_empty = true;
  word_t value;
  for (int i = 0; i < PAGE_SIZE; i++) {
    PMread(addr + i, &value);
    // non-zero value
    if (value != 0) {
      is_empty = false;
      // search corresponding child
      uint64_t child_addr = addr | value;
      if (dfs(context, child_addr, level + 1, result) != FAIL_STATUS) {
        return SUCCES_STATUS;
      }
    }
  }
  if (is_empty) {
    // return the current frame
    *result = addr;
    return SUCCES_STATUS;
  }
  return FAIL_STATUS;
}

int page_fault(uint64_t *addr) {
  dfs_context_t context = {*addr, 0, 0, 0};
  bool remove_from_parent = true;

  if (dfs(&context, 0, 0, addr) == FAIL_STATUS) {
    // use max frame or max dist frame
    if (context.max_index_frame_addr + PAGE_SIZE < VIRTUAL_MEMORY_SIZE) {
      *addr = context.max_index_frame_addr + PAGE_SIZE;
      remove_from_parent = false;
    } else {
      *addr = context.max_dist_frame_addr;
    }
  }

  if (*addr == 0) {
    return FAIL_STATUS;
  }

  // remove from parent
  if (remove_from_parent) {
    // TODO: remove from parent
  }
  // fill with 0s (if next layer is table)
  clear_frame(*addr);

  // TODO write to parent table row
  // return the address of the new frame
  return SUCCES_STATUS;
}

/**
 * Translates a virtual address to a physical address.
 * @param addr the virtual address to translate
 * @param level the current level of the translation
 * @return 1 on success, 0 on failure
 */
int translate(uint64_t *addr, int level) {
  printf("translate: addr=%lu, level=%d\n", *addr, level);
  if (level == TABLES_DEPTH) {
    // we reached the last level, return the address
    return SUCCES_STATUS;
  }
  // number of bits we need to get to this level
  // (i.e. page offset in previous + current level)
  int address_bits = (level + 1) * OFFSET_WIDTH;
  // index of address where the current offset starts
  int bit_index = VIRTUAL_ADDRESS_WIDTH - address_bits;
  // offset of the next levels
  int next_offset = *addr & ((1 << bit_index) - 1);

  // SHR to get the relevant page address and offset bits
  *addr >>= bit_index;
  // keep the rightmost bits
  int level_offset = *addr & ~OFFSET_MASK;
  // translate the offset via physical memory lookup
  PMread(*addr, (word_t *)addr);
  // add back the level offset
  *addr |= level_offset;
  // check that addr != 0
  if ((*addr & OFFSET_MASK) == 0) {
    page_fault(addr);
  }
  // SHL back to the original position
  *addr <<= bit_index;
  // add the offset of the next levels
  *addr |= next_offset;
  // translate next levels
  return translate(addr, level + 1);
}