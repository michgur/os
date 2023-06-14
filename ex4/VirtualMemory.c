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

typedef  uint64_t frame_index_t;
typedef  uint64_t frame_addr_t;

int dec_to_bin(int n) {
  int bin = 0;
  int rem, i = 1;
  while (n != 0) {
    rem = n % 2;
    n /= 2;
    bin += rem * i;
    i *= 10;
  }
  return bin;
}

void clear_frame(frame_index_t index) {
  printf("clear_frame: %llu\n", index);
  frame_addr_t addr = FRAME_ADDR(index);
  for (int i = 0; i < PAGE_SIZE; i++) {
    PMwrite(addr + i, 0);
  }
}

void VMinitialize() { clear_frame(0); }

int translate(uint64_t *addr, frame_index_t page, int level);

int VMread(uint64_t addr, word_t *value) {
  printf("VMread: %llu\n", addr);
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr, FRAME_INDEX(addr), 0) == FAIL_STATUS) {
    return FAIL_STATUS;
  }
  // read the value from the physical memory
  PMread(addr, value);
  return SUCCES_STATUS;
}

int VMwrite(uint64_t addr, word_t value) {
  printf("VMwrite: %llu\n", addr);
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr, FRAME_INDEX(addr), 0) == FAIL_STATUS) {
    return FAIL_STATUS;
  }
  // write the value to the physical memory
  PMwrite(addr, value);
  return SUCCES_STATUS;
}


int get_child(uint64_t node_addr, int child_offset, uint64_t* child_addr) {
    word_t value;
    PMread(node_addr + child_offset, &value);
    *child_addr = FRAME_ADDR(value);
    return value != 0 ? SUCCES_STATUS : FAIL_STATUS;
}

int set_child(uint64_t node_addr, int child_offset, uint64_t child_addr) {
    word_t value;
    PMread(node_addr + child_offset, &value);
    value = FRAME_INDEX(child_addr);
    PMwrite(node_addr + child_offset, value);
    return SUCCES_STATUS;
}


typedef struct {
  // index of page to insert
  frame_index_t page;
  // max cyclic distance from the new page
  uint64_t max_dist;
  // frame with the max cyclic distance
  frame_index_t max_dist_frame;
  // frame with the max index
  frame_index_t max_index_frame;

} dfs_context_t;

void update_context(dfs_context_t *context, frame_index_t frame_index, int level) {
  // update max index frame
  if (frame_index > context->max_index_frame) {
    context->max_index_frame = frame_index;
  }
  // update max distance page
  // min{NUM_PAGES - |page_swapped_in - p|,|page_swapped_in - p|}
  if (level == TABLES_DEPTH) {
    int dist = abs((int)(context->page - frame_index));
    dist = dist < NUM_PAGES - dist ? dist : NUM_PAGES - dist;

    if (dist < context->max_dist) {
      context->max_dist = dist;
      context->max_dist_frame = frame_index;
    }
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
int dfs(dfs_context_t *context, uint64_t node_index, int level, uint64_t *result) {
  // reached the last level of the page table
  if (level == TABLES_DEPTH) {
    // frame is not a page table frame
    return FAIL_STATUS;
  }
  update_context(context, node_index, level);
  // if page consists of only 0s, return the current frame
  bool is_empty = true;
  uint64_t child_addr, addr = FRAME_ADDR(node_index);
  for (int i = 0; i < PAGE_SIZE; i++) {
    if (get_child(addr, i, &child_addr) == SUCCES_STATUS) {
      is_empty = false;
      // search corresponding child
      if (dfs(context, child_addr, level + 1, result) != FAIL_STATUS) {
        return SUCCES_STATUS;
      }
    }
  }
  if (is_empty && addr != 0) {
    // return the current frame
    *result = addr;
    return SUCCES_STATUS;
  }
  return FAIL_STATUS;
}

int page_fault(uint64_t parent_addr, frame_index_t page_index, frame_addr_t *result_addr) {
  dfs_context_t context = {page_index, 0, 0, 0};
  bool remove_from_parent = true;

  if (dfs(&context, 0, 0, result_addr) == FAIL_STATUS || true) {
    // use max frame or max dist frame
    if (context.max_index_frame + 1 < NUM_FRAMES) {
      *result_addr = context.max_index_frame + 1;
      remove_from_parent = false;
    } else {
      *result_addr = context.max_dist_frame;
    }
  }

  if (*result_addr == 0) {
    return FAIL_STATUS;
  }

  // remove from parent
  if (remove_from_parent) {
    // TODO: remove from parent
    // PMevict(*addr);
  }
  // fill with 0s (if next layer is table)
  clear_frame(*result_addr);
  // TODO write to parent table row
  PMwrite(parent_addr, FRAME_INDEX(*result_addr));
  
  // return the address of the new frame
  return SUCCES_STATUS;
}

/**
 * Translates a virtual address to a physical address.
 * @param addr the virtual address to translate
 * @param level the current level of the translation
 * @return 1 on success, 0 on failure
 */
int translate(uint64_t *addr, frame_index_t page, int level) {
  printf("translate: addr=%lu, level=%d\n", dec_to_bin(*addr), level);
  if (level == TABLES_DEPTH) {
    // we reached the last level, return the address
    return SUCCES_STATUS;
  }
  // number of bits we need to get to this level
  // (i.e. page offset in previous + current level)
  int address_bits = (level + 1) * OFFSET_WIDTH;
  // offset of the next levels
  int next_offset = *addr & ((1 << address_bits) - 1);

  // SHR to get the relevant page address and offset bits
  *addr >>= address_bits;
  // keep the rightmost bits
  int level_offset = *addr & ~OFFSET_MASK;
  // translate the offset via physical memory lookup
  PMread(*addr, (word_t *)addr);
  // add back the level offset
  *addr |= level_offset;
  // check that addr != 0
  if ((*addr & OFFSET_MASK) == 0) {
    page_fault(*addr, page, addr);
  }
  // SHL back to the original position
  *addr <<= address_bits;
  // add the offset of the next levels
  *addr |= next_offset;
  // translate next levels
  return translate(addr, page, level + 1);
}
