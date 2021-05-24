#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 30

void test();
void test2(void);
void* exitthreadmain(void *arg);

int
main(int argc, char *argv[])
{ 
  int pid = fork();
  if (pid == 0)
  {
    test();
    exit();
  } else {
    wait();
  }
  
  // test();
  exit();
}

void*
exitthreadmain(void *arg)
{
  int i;
  if ((int)arg == 1){
    while(1){
      printf(1, "thread_exit ...\n");
      for (i = 0; i < 5000000; i++);
    }
  } else if ((int)arg == 2){
    exit();
  }
  thread_exit(0);

  return 0;
}

void test() {
  thread_t threads[NUM_THREAD];
  int i;
  for (i = 0; i < NUM_THREAD; i++){
      printf(1, "thread_create %d\n", i);
    if (thread_create(&threads[i], exitthreadmain, (void*)1) != 0){
      printf(1, "panic at thread_create\n");
    }
  }
}

void
test2(void)
{
  thread_t threads[NUM_THREAD];
  int i;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], exitthreadmain, (void*)2) != 0){
      printf(1, "panic at thread_create\n");
      // return -1;
    }
  }
  while(1);
}


