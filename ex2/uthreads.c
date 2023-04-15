#include "uthreads.h"
#include <setjmp.h>

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

#define READY 0
#define RUNNING 1
#define BLOCKED 2

struct thread_t {
  int status;
  sigjmp_buf *env;
  void *stack;
};

struct thread_t _threads[MAX_THREAD_NUM];
int _quantum_usecs;
int _current_tid = -1;
int _available_tid = 0;

int uthread_init(int quantum_usecs) {
  if (quantum_usecs <= 0) {
    return -1;
  }

  _quantum_usecs = quantum_usecs;

  _threads[0].status = RUNNING;
  sigsetjmp(_threads[0].env, 1);

  _current_tid = 0;
  _available_tid++;
  return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
  if (entry_point == NULL || _available_tid == MAX_THREAD_NUM) {
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
  _available_tid++;
}

int uthread_terminate(int tid) { return 0; }

int uthread_block(int tid) { return 0; }

int uthread_resume(int tid) { return 0; }

int uthread_sleep(int num_quantums) { return 0; }

int uthread_get_tid() { return _current_tid; }

int uthread_get_total_quantums() { return 0; }

int uthread_get_quantums(int tid) { return 0; }
