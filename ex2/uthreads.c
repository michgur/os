#include "uthreads.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
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

/** Status of uninitialized slots in the `_threads` array, not actual threads */
#define AVAILABLE 0
/** Status of a ready thread */
#define READY 1
/** Status of currently running thread */
#define RUNNING 2
/** Status of a blocked thread */
#define BLOCKED 3

/** Represents a thread */
struct thread_t {
  /** Current status */
  int status;
  /** Number of quantums this thread has run */
  int quantums_run;
  /** Number of remaining quantums this thread has to sleep */
  int quantums_sleep;
  /** Priority of this thread. Running thread has priority 0 */
  int priority;
  /** Environment for `sigsetjmp`, `siglongjmp` */
  sigjmp_buf env;
  /** Stack pointer */
  void *stack;
};

/** Array of threads */
struct thread_t _threads[MAX_THREAD_NUM];
/** Quantum length in microseconds */
int _quantum_usecs;
/** Total number of quantums the scheduler has run so far */
int _quantums_total = 0;
/** Current thread ID */
int _current_tid = -1;
/** Next empty thread ID available for spawn */
int _available_tid = 0;
/** Next priority to assign to a thread */
int _next_priority = 0;

void scheduler(int sig) {
  if (_current_tid == -1) {
    return;
  }

  // count quantums
  _quantums_total++;
  _threads[_current_tid].quantums_run++;

  // decide which thread to run
  int next_tid = -1;
  for (int tid = 0; tid < MAX_THREAD_NUM; tid++) {
    switch (_threads[tid].status) {
    case AVAILABLE:
      // do nothing
      break;
    case READY:
      // TODO: update priority and choose next_tid
      break;
    case RUNNING:
      // do nothing ?
      break;
    case BLOCKED:
      // decrement sleep counter
      _threads[tid].quantums_sleep--;
      if (_threads[tid].quantums_sleep == 0) {
        // wake up thread
        _threads[tid].status = READY;
      }
      break;
    }
  }
}

int uthread_init(int quantum_usecs) {
  if (quantum_usecs <= 0) {
    return -1;
  }

  // add main thread
  _threads[0].status = RUNNING;
  sigsetjmp(_threads[0].env, 1);
  // setup variables
  _quantum_usecs = quantum_usecs;
  _current_tid = 0;
  _available_tid++;
  _next_priority++;
  // setup timer
  struct sigaction sa = {0};
  struct itimerval timer;
  sa.sa_handler = &scheduler;
  timer.it_interval.tv_usec = quantum_usecs;
  if (sigaction(SIGVTALRM, &sa, NULL) < 0 ||
      setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
    return -1;
  }

  return 0;
}

/** Finds the next available thread ID, or -1 if none are available */
int next_available_tid() {
  for (int i = 0; i < MAX_THREAD_NUM; i++) {
    if (_threads[i].status == AVAILABLE) {
      return i;
    }
  }
  return -1;
}

int uthread_spawn(thread_entry_point entry_point) {
  if (entry_point == NULL || _available_tid == -1) {
    return -1;
  }

  struct thread_t *thread = _threads + _available_tid;
  // allocate stack
  thread->stack = malloc(STACK_SIZE); // todo check failure
  // save context via sigsetjmp
  sigsetjmp(thread->env, 1);
  // set sp to address of stack
  address_t sp = (address_t)thread->stack + STACK_SIZE - sizeof(address_t);
  (thread->env->__jmpbuf)[JB_SP] = translate_address(sp);
  // set pc to address of entry_point
  address_t pc = (address_t)entry_point;
  (thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
  sigemptyset(&thread->env->__saved_mask);
  // set status to ready
  thread->status = READY;
  // assign priority
  thread->priority = _next_priority++;
  // advance available tid and return
  int tid = _available_tid;
  _available_tid = next_available_tid();
  return tid;
}

/** Free all memory used by `_threads` */
void free_all() {
  for (int i = 0; i < MAX_THREAD_NUM; i++) {
    if (_threads[i].status != AVAILABLE) {
      free(_threads[i].stack);
    }
  }
  free(_threads);
}

/** Returns true if the given tid is invalid or uninitialized */
int is_tid_invalid(int tid) {
  return tid < 0 || tid >= MAX_THREAD_NUM || _threads[tid].status == AVAILABLE;
}

int uthread_terminate(int tid) {
  // validate tid
  if (is_tid_invalid(tid)) {
    return -1;
  }

  // handle main thread termination
  if (tid == 0) {
    free_all();
    exit(0);
  }

  // free stack
  free(_threads[tid].stack);
  // reset variables
  _threads[tid].quantums_run = 0;
  _threads[tid].quantums_sleep = 0;
  // set status to available
  _threads[tid].status = AVAILABLE;
  if (tid < _available_tid) {
    _available_tid = tid;
  }

  // if terminating thread is current thread, go to scheduler
  if (tid == _current_tid) {
    scheduler(0);
  }

  return 0;
}

int uthread_block(int tid) {
  // validate tid
  if (is_tid_invalid(tid) || tid == 0) {
    return -1;
  }

  // set status to blocked
  _threads[tid].status = BLOCKED;

  // if blocking thread is current thread, go to scheduler
  if (tid == _current_tid) {
    scheduler(0);
  }

  return 0;
}

int uthread_resume(int tid) {
  // validate tid
  if (is_tid_invalid(tid)) {
    return -1;
  }

  // set status to ready
  if (_threads[tid].status == BLOCKED) {
    _threads[tid].status = READY;
  }

  return 0;
}

int uthread_sleep(int num_quantums) {
  if (_current_tid == 0 || num_quantums <= 0) {
    return -1;
  }

  // set sleep_quantums
  _threads[_current_tid].quantums_sleep = num_quantums;

  // block current thread and go to scheduler (happens in uthread_block)
  uthread_block(_current_tid);
  return 0;
}

int uthread_get_tid() { return _current_tid; }

int uthread_get_total_quantums() { return _quantums_total; }

int uthread_get_quantums(int tid) {
  if (is_tid_invalid(tid)) {
    return -1;
  }

  return _threads[tid].quantums_run;
}
