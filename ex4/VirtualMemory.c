#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

/******************************* Constants ***********************************/

#define FAIL_STATUS 0
#define SUCCES_STATUS 1

#define OFFSET_MASK ((1 << OFFSET_WIDTH) - 1)
#define FRAME_INDEX(addr) (addr >> OFFSET_WIDTH)
#define FRAME_ADDR(index) (index << OFFSET_WIDTH)

/**
 * Translate virtual address to physical address
 * @param addr address to translate
 * @return 1 on success, 0 on failure
 */
int translate(uint64_t *addr);

/**
 * Fills the given frame with zeros
 * @param index index of frame to clear
 */
void clear_frame(uint64_t index);

/******************************* API impl ***********************************/

void VMinitialize() { clear_frame(0); }

int VMread(uint64_t addr, word_t *value) {
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr) == FAIL_STATUS ||
      value == NULL) {
    return FAIL_STATUS;
  }
  // read the value from the physical memory
  PMread(addr, value);
  return SUCCES_STATUS;
}

int VMwrite(uint64_t addr, word_t value) {
  if (addr >= VIRTUAL_MEMORY_SIZE || translate(&addr) == FAIL_STATUS) {
    return FAIL_STATUS;
  }
  // write the value to the physical memory
  PMwrite(addr, value);
  return SUCCES_STATUS;
}

/******************************* Frame methods *******************************/

// Stores a frame and the address of its pointer in physical memory
typedef struct {
  // frame index
  uint64_t index;
  // address in physical memory of pointer to frame
  uint64_t addr;
} frame_ptr_t;

uint64_t get_child(uint64_t node, int index) {
  word_t child;
  PMread(FRAME_ADDR(node) + index, &child);
  return child;
}

frame_ptr_t get_child_ptr(frame_ptr_t node, int index) {
  frame_ptr_t child = {0, FRAME_ADDR(node.index) + index};
  PMread(child.addr, (word_t *)&child.index);
  return child;
}

void set_child(uint64_t node, int index, uint64_t child) {
  PMwrite(FRAME_ADDR(node) + index, child);
}

void clear_frame(uint64_t index) {
  uint64_t addr = FRAME_ADDR(index);
  for (int i = 0; i < PAGE_SIZE; i++) {
    PMwrite(addr + i, 0);
  }
}

/********************* Table tree traversal and translation *******************/

// context for dfs execution
typedef struct {
  // index of page to restore
  uint64_t page;
  // index of parent node
  uint64_t parent;
  // priority 1: unused table map frame (must not be the parent)
  frame_ptr_t unused_frame;
  // priority 2: frame with the max index
  frame_ptr_t max_index_frame;
  // priority 3: frame containing page with the max cyclic distance from page
  frame_ptr_t max_dist_frame;
  // page stored in max_dist_frame
  uint64_t max_dist_page;
  // max cyclic distance from page
  uint64_t max_dist;
} dfs_context_t;

/**
 * Executed for each node in the dfs. Finds:
 * 1. an unused table map frame if exists
 * 2. the frame with the max index
 * 3. the frame containing a page with the max cyclic distance from context.page
 * @param context the dfs context
 * @param node the current node
 * @param level the current level in the dfs
 * @param is_empty true if node has no children (when node is not a leaf)
 * @param page the page in node (when node is a leaf)
 * @return 1 if found an unused table map, 0 otherwise
 */
int update_context(dfs_context_t *context, frame_ptr_t node, int level,
                   bool is_empty, uint64_t page) {
  bool is_leaf = level >= TABLES_DEPTH;
  // 1. update unused frame
  if (!is_leaf && is_empty && node.index != 0 &&
      node.index != context->parent) {
    // return the current frame
    context->unused_frame = node;
    return SUCCES_STATUS;
  }
  // 2. update max index frame
  if (node.index > context->max_index_frame.index) {
    context->max_index_frame = node;
  }
  // 3. update max distance page (and corresponding frame)
  if (is_leaf) {
    int dist = abs((int)(context->page - page));
    dist = dist < NUM_PAGES - dist ? dist : NUM_PAGES - dist;

    if (dist > context->max_dist) {
      context->max_dist = dist;
      context->max_dist_frame = node;
      context->max_dist_page = page;
    }
  }

  return FAIL_STATUS;
}

/**
 * Selects a frame to evict and insert the new page into.
 * @param context context of the dfs
 * @param node the current node
 * @param level the current level of search
 * @param virtual_addr the virtual address of the current node
 * @return 1 if found an unused table map, 0 otherwise (use max_index_frame or
 * max_dist_frame)
 */
int dfs(dfs_context_t *context, frame_ptr_t node, int level,
        uint64_t virtual_addr) {
  bool is_empty = true;
  if (level < TABLES_DEPTH) {
    virtual_addr <<= OFFSET_WIDTH;
    // visit each existing child
    for (int i = 0; i < PAGE_SIZE; i++) {
      frame_ptr_t child = get_child_ptr(node, i);
      if (child.index != 0) {
        is_empty = false;
        // search corresponding child
        if (dfs(context, child, level + 1, virtual_addr + i) != FAIL_STATUS) {
          return SUCCES_STATUS;
        }
      }
    }
  }
  return update_context(context, node, level, is_empty, virtual_addr);
}

/**
 * Finds a frame, evicts it if necessary and inserts the new page into it.
 * @param parent_node the parent node of the page (used to avoid evicting it)
 * @param index_in_parent the index of the page in the parent node
 * @param page the page to insert
 * @param is_leaf whether the page is a leaf
 * @return the index of the frame containing the page (after insertion)
 */
uint64_t page_fault(uint64_t parent_node, int index_in_parent, uint64_t page,
                    bool is_leaf) {
  dfs_context_t context = {page, parent_node, 0, 0, 0, 0};
  bool remove_from_parent = true;

  frame_ptr_t frame = {0, 0};

  if (dfs(&context, frame, 0, 0) == SUCCES_STATUS) {
    // found an unused table map frame
    frame = context.unused_frame;
  } else if (context.max_index_frame.index + 1 < NUM_FRAMES) {
    frame = context.max_index_frame;
    frame.index++;
    remove_from_parent = false;
  } else {
    frame = context.max_dist_frame;
    // evict the page in the frame
    PMevict(frame.index, context.max_dist_page);
  }

  // remove from old parent
  if (remove_from_parent) {
    PMwrite(frame.addr, 0);
  }
  if (is_leaf) {
    // if next level is a page, restore it
    PMrestore(frame.index, page);
  } else {
    // otherwise, clear the frame
    clear_frame(frame.index);
  }
  // write to new parent
  set_child(parent_node, index_in_parent, frame.index);
  // return the address of the new frame
  return frame.index;
}

/**
 * Finds the frame containing the page. If page is not in memory, evicts a frame
 * and restores the page into it.
 * @param page the page to insert
 * @param node the current node
 */
uint64_t find_frame(uint64_t page, uint64_t node, int level) {
  // print node and level
  if (level == TABLES_DEPTH) {
    return node;
  }

  // bit index of where the current level offset starts
  // we add 2 : once to skip the root and once to skip the current level
  uint64_t bit_index = (level + 2) * OFFSET_WIDTH;
  uint64_t offset = (page >> (VIRTUAL_ADDRESS_WIDTH - bit_index)) & OFFSET_MASK;

  uint64_t child = get_child(node, offset);
  if (child == 0) {
    // page fault
    child = page_fault(node, offset, page, level + 1 >= TABLES_DEPTH);
  }

  return find_frame(page, child, level + 1);
}

int translate(uint64_t *addr) {
  uint64_t offset = *addr & OFFSET_MASK;
  uint64_t page = *addr >> OFFSET_WIDTH;
  *addr = FRAME_ADDR(find_frame(page, 0, 0)) + offset;
  // if we got frame 0 it's considered a failure, unless the depth is 0
  return (*addr == offset && TABLES_DEPTH > 0) ? FAIL_STATUS : SUCCES_STATUS;
}
