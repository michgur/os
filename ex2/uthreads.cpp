#include "uthreads.h"
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// taken from demo_jmp.c
#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
               : "=g"(ret)
               : "0"(addr));
  return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
  address_t ret;
  asm volatile("xor    %%gs:0x18,%0\n"
               "rol    $0x9,%0\n"
               : "=g"(ret)
               : "0"(addr));
  return ret;
}

#endif

enum status_t {
  AVAILABLE, // Uninitialized slots in the `threads` array, not actual threads
  READY,     // Status of a ready thread
  RUNNING,   // Status of currently running thread
  BLOCKED    // Status of a blocked thread
};

#define FAILURE (-1)
#define SUCCESS 0

/** second in usecs */
#define SECOND 1000000

/** sigsetjmp macro */
#define SET_JMP(tid) sigsetjmp(threads[tid].env, 1)
/** siglongjmp macro */
#define LONG_JMP(tid) siglongjmp(threads[tid].env, 1)

/** setting thread's status macro */
#define SET_STATUS(tid, state) threads[tid].status = state

void free_all();

/** system error handler macro */
#define ERROR_MSG_SYSTEM(text)                                                 \
  fprintf(stderr, "system error: %s (function %s)\n", text, __func__);         \
  free_all();                                                                  \
  exit(1)
/** library thread error handler macro */
#define ERROR_MSG_THREAD(text)                                                 \
  fprintf(stderr, "thread library error: %s (function %s)\n", text, __func__); \
  return FAILURE

#define SIGMASK_BLOCK                                                          \
  if (sigprocmask(SIG_BLOCK, &masked_set, NULL) == FAILURE) {                  \
    ERROR_MSG_SYSTEM("sigprocmask blocking failed");                           \
  }
#define SIGMASK_UNBLOCK                                                        \
  if (sigprocmask(SIG_UNBLOCK, &masked_set, NULL) == FAILURE) {                \
    ERROR_MSG_SYSTEM("sigprocmask unblocking failed");                         \
  }

/** wraps a code block with sigprocmask signal blocking/unblocking, handles
 * errors */
#define WITH_SIGMASK_BLOCKED(ret_type, func_name, param_type, param_name)      \
  /** Declaration of underlying function */                                    \
  static ret_type __##func_name(param_type param_name);                        \
  /** The actual function, which calls the unerlying function */               \
  ret_type func_name(param_type param_name) {                                  \
    /** try to block signals */                                                \
    SIGMASK_BLOCK;                                                             \
    /** call the underlying function */                                        \
    ret_type ret = __##func_name(param_name);                                  \
    /** try to unblock signals, if it fails, exit with error */                \
    SIGMASK_UNBLOCK;                                                           \
    return ret;                                                                \
  }                                                                            \
  /** implementation of underlying function */                                 \
  static ret_type __##func_name(param_type param_name) /** implementation */

/** Represents a thread */
struct thread_t {
  /** Current status */
  enum status_t status;
  /** Number of quantums this thread has run */
  int quantums_run;
  /** Number of remaining quantums this thread has to sleep */
  int quantums_sleep;
  /** For blocked threads; Whether to wait for an explicit `uthread_resume`
   * call, or to become ready when `quantums_sleep == 0` */
  bool wait_for_resume;
  /** Priority of this thread. Running thread has highest priority */
  int priority;
  /** Environment for `sigsetjmp`, `siglongjmp` */
  sigjmp_buf env;
  /** Stack pointer */
  void *stack;
};

/** Globals */

/** Array of threads */
static struct thread_t threads[MAX_THREAD_NUM];
/** Total number of quantums the scheduler has run so far */
static int quantums_total = 0;
/** Currently running thread ID */
static int running_tid = -1;
/** Next empty slot available for spawn.
 * If no slots are available, this is set to -1 */
static int next_available_tid = 0;
/** Timer for the scheduler */
static struct itimerval timer;
/** Signal mask for thread switching */
static sigset_t masked_set;

/** Updates status and other fields of all threads,
 * and finds the next thread to run */
int update_and_find_next_tid() {
  int next_tid, next_priority = -1;
  for (int tid = 0; tid < MAX_THREAD_NUM; tid++) {
    struct thread_t *thread = &threads[tid];
    switch (thread->status) {
    case AVAILABLE:
      // do nothing
      break;
    case RUNNING:
      // count quontums and move to ready
      thread->quantums_run++;
      thread->status = READY;
      // Move to the end of the priority queue
      thread->priority = -1;
      // continue handling as a running thread; no break
    case READY:
      // update priority and choose next_tid
      if (++thread->priority > next_priority) {
        next_priority = thread->priority;
        next_tid = tid;
      }
      break;
    case BLOCKED:
      // update sleep duration & check if expired
      if (--thread->quantums_sleep <= 0) {
        if (thread->wait_for_resume) {
          // blocked with `uthread_block` and not explicitly resumed
          // reset sleep duration to avoid overflow
          thread->quantums_sleep = 0;
        } else {
          // resume thread and move to the end of the priority queue
          thread->status = READY;
          thread->priority = 0;
        }
      }
      break;
    }
  }
  return next_tid;
}

/** Caches the currect thread and jumps to thread with ID `tid` */
void jump_to_thread(int tid) {
  // cache current thread with sigsetjmp
  if (SET_JMP(running_tid) != 0) {
    return;
  }
  
  threads[tid].status = RUNNING;
  running_tid = tid;
  LONG_JMP(tid);
}

void scheduler(int sig) {
  if (running_tid == -1) {
    return;
  }

  SIGMASK_BLOCK;
  // count quantums
  quantums_total++;

  // choose next thread and jump to it
  int next_tid = update_and_find_next_tid();
  jump_to_thread(next_tid);
  SIGMASK_UNBLOCK;
}

int start_timer(bool start_immediately) {
  int ret = setitimer(ITIMER_VIRTUAL, &timer, NULL);
  if (start_immediately) {
    scheduler(0);
  }
  return ret;
}

WITH_SIGMASK_BLOCKED(int, uthread_init, int, quantum_usecs) {
  if (quantum_usecs <= 0) {
    ERROR_MSG_THREAD("invalid input");
  }

  // set main thread as running
  threads[0].status = RUNNING;
  // setup variables
  running_tid = 0;
  next_available_tid = 1;
  timer.it_value.tv_sec = quantum_usecs / SECOND;
  timer.it_value.tv_usec = quantum_usecs % SECOND;
  timer.it_interval.tv_sec = quantum_usecs / SECOND;
  timer.it_interval.tv_usec = quantum_usecs % SECOND;
  // setup timer signal handler
  struct sigaction sa = {0};
  sa.sa_handler = &scheduler;
  if (sigaction(SIGVTALRM, &sa, NULL) != SUCCESS ||
      start_timer(false) != SUCCESS) {
    ERROR_MSG_SYSTEM("sigaction or setitimer error");
  }

  // setup signal mask for thread switching
  sigemptyset(&masked_set);
  sigaddset(&masked_set, SIGVTALRM);

  return SUCCESS;
}

/** Finds the next available thread ID, or FAILURE if none are available */
int get_next_available_tid() {
  for (int i = 0; i < MAX_THREAD_NUM; i++) {
    if (threads[i].status == AVAILABLE) {
      return i;
    }
  }
  return FAILURE;
}

WITH_SIGMASK_BLOCKED(int, uthread_spawn, thread_entry_point, entry_point) {
  if (entry_point == NULL || next_available_tid == FAILURE) {
    ERROR_MSG_THREAD("invalid input or max threads num exceeded");
  }

  struct thread_t *thread = &threads[next_available_tid];
  // allocate stack
  thread->stack = malloc(STACK_SIZE);
  if (thread->stack == NULL) {
    ERROR_MSG_SYSTEM("allocation error");
  }
  // get context buffer via sigsetjmp
  SET_JMP(next_available_tid);
  // set sp to address of stack
  address_t sp = (address_t)thread->stack + STACK_SIZE - sizeof(address_t);
  (thread->env->__jmpbuf)[JB_SP] = translate_address(sp);
  // set pc to address of entry_point
  address_t pc = (address_t)entry_point;
  (thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
  // empty signal mask
  sigemptyset(&thread->env->__saved_mask);
  // set status to ready
  thread->status = READY;
  // move to end of priority queue
  thread->priority = 0;
  // advance available tid and return
  int tid = next_available_tid;
  next_available_tid = get_next_available_tid();
  return tid;
}

/** Free all memory used by `threads` */
void free_all() {
  // notice i=1 because we didn't allocate stack for the main thread (0)
  for (int i = 1; i < MAX_THREAD_NUM; i++) {
    if (threads[i].status != AVAILABLE) {
      free(threads[i].stack);
    }
  }
}

/** Returns true if the given tid is invalid or uninitialized */
bool is_tid_invalid(int tid) {
  return tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].status == AVAILABLE;
}

WITH_SIGMASK_BLOCKED(int, uthread_terminate, int, tid) {
  // validate tid
  if (is_tid_invalid(tid)) {
    ERROR_MSG_THREAD("invalid input");
  }

  // handle main thread termination
  if (tid == 0) {
    free_all();
    exit(0);
  }

  // free stack
  free(threads[tid].stack);
  // reset variables
  threads[tid].quantums_run = 0;
  threads[tid].quantums_sleep = 0;
  threads[tid].wait_for_resume = false;
  threads[tid].priority = 0;
  // set status to available
  threads[tid].status = AVAILABLE;
  if (tid < next_available_tid) {
    next_available_tid = tid;
  }

  // if terminating thread is current thread, reset timer and go to scheduler
  if (tid == running_tid) {
    start_timer(true);
  }

  return SUCCESS;
}

WITH_SIGMASK_BLOCKED(int, uthread_block, int, tid) {
  // validate tid
  if (is_tid_invalid(tid) || tid == 0) {
    ERROR_MSG_THREAD("invalid input");
  }

  // set status to blocked
  threads[tid].status = BLOCKED;
  // don't resume until explicitly told to
  threads[tid].wait_for_resume = true;
  // set priority to 0
  threads[tid].priority = 0;

  // if blocking thread is current thread, go to scheduler
  if (tid == running_tid) {
    start_timer(true);
  }

  return SUCCESS;
}

WITH_SIGMASK_BLOCKED(int, uthread_resume, int, tid) {
  // validate tid
  if (is_tid_invalid(tid) || tid == 0) {
    ERROR_MSG_THREAD("invalid input");
  }

  // don't wait for resume; become ready when sleep duration expires
  threads[tid].wait_for_resume = false;

  return SUCCESS;
}

WITH_SIGMASK_BLOCKED(int, uthread_sleep, int, num_quantums) {
  if (running_tid == 0 || num_quantums <= 0) {
    ERROR_MSG_THREAD("invalid input");
  }

  // set status to sleeping
  threads[running_tid].status = BLOCKED;
  // set sleep_quantums
  threads[running_tid].quantums_sleep = num_quantums;
  // set priority to 0
  threads[running_tid].priority = 0;
  // go to scheduler
  start_timer(true);
  return SUCCESS;
}

int uthread_get_tid() { return running_tid; }

int uthread_get_total_quantums() { return quantums_total; }

int uthread_get_quantums(int tid) {
  if (is_tid_invalid(tid)) {
    ERROR_MSG_THREAD("invalid input");
  }

  return threads[tid].quantums_run;
}
