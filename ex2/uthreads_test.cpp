#include "uthreads.h"
#include <iostream>
#include <unistd.h>

using namespace std;

int ret = 0;

void f1() {
    for (int i = 0; i < 2; i++) {
        cout << "f1" << endl;
        uthread_block(2);
        uthread_sleep(1);
        uthread_resume(2);
    }
    ret += 1;
    cout << "f1 ret" << endl;
    uthread_terminate(uthread_get_tid());
}

void f2() { 
    for (int i = 0; i < 2; i++) {
        cout << "f2" << endl;
        uthread_sleep(2);
    }
    ret += 2;
    cout << "f2 ret" << endl;
    uthread_terminate(uthread_get_tid());
}

void f3() { 
    for (int i = 0; i < 2; i++) {
        cout << "f3" << endl;
        uthread_sleep(4);
    }
    ret += 4;
    cout << "f3 ret" << endl;
    uthread_terminate(uthread_get_tid());
}

// expected order
// main
// f1
// f3
// main
// f1
// main
// f1 ret
// main
// f3
// f2
// main
// main
// f2 (has the same priority as f3, both waited 0 quantums since unblock, but has lower tid)
// main (has the same priority as f3, both waited 1 quantums, but has lower tid)
// f3 ret
// main
// f2 ret
// main

int main() {
  uthread_init(1000000);
  uthread_spawn(f1);
  uthread_spawn(f2);
  uthread_spawn(f3);
  
  while(ret != 7);

  return 0;
}