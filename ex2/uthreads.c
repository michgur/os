#include "uthreads.h"

int uthread_init(int quantum_usecs) { return 0; }

int uthread_spawn(thread_entry_point entry_point) { return 0; }

int uthread_terminate(int tid) { return 0; }

int uthread_block(int tid) { return 0; }

int uthread_resume(int tid) { return 0; }

int uthread_sleep(int num_quantums) { return 0; }

int uthread_get_tid() { return 0; }

int uthread_get_total_quantums() { return 0; }

int uthread_get_quantums(int tid) { return 0; }
