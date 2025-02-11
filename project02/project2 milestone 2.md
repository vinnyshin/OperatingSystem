# Implementation

## thread 추상화

따로 thread 구조체를 만들어 사용하려 했으나 대부분의 xv6 시스템 콜들이 proc구조체 기반으로 작동하여 많은 수정이 불가피 해보였기 때문에  thread는 proc구조체를 사용한다. 

```c
struct proc {
  uint sz;
  pde_t* pgdir;            
  char *kstack;                
  enum procstate state;       
  int pid;                     
  struct proc *parent;         
  struct trapframe *tf;        
  struct context *context;     
  void *chan;                  
  int killed;                  
  struct file *ofile[NOFILE];  
  struct inode *cwd;           
  char name[16];               
    
  // MLFQ variables
  struct queueMLFQ *priorityqueue; // Where the process is working on	
  int quantumtick; // How many ticks the process used
  int timesum; // With respect to time allotment, How many ticks the process used
  enum schedmode mode;

  // Thread variables
  int tid; // if tid == 0, it means master thread
  struct proc *master; // memory address of master thread
  void *ret_val; // return value, used in thread_exit, thread_join
  
  int nthreads;
  uint freepage[NPROC]; // freed page array for preventing external fragmentation
  int freepagesize; // freepage array is a stack data structure. it needs size for pop and push operation.
  // esp is uint check trapframe in x86.h
};
```

각 쓰레드는 2 Page를 할당 받는다. 하나는 가드페이지에 사용되고, 하나는 쓰레드 스택에 사용된다. 쓰레드의 경우 스택은 거꾸로 자라기 때문에 sz는 자신의 스택의 base을 가리킨다. 마스터의 경우 sz는 process의 top을 가리킨다. 마스터와 쓰레드를 구분하는 방법은 tid를 활용한다. tid가 0일 경우 master이고, tid가 0보다 크면 slave 쓰레드다.

쓰레드는 pgdir을 공유한다. 각 proc 구조체는 마스터와 같은 pgdir을 가리킨다.

각 쓰레드는 kstack을 freelist로부터 할당받는다. proc 구조체의 procstate로 쓰레드의 상태를 표현한다. proc struct기반으로 사용하기 때문에 sleep과 wakeup을 그대로 사용할 수 있다.

tf는 create시 create를 부른 쓰레드의 것을 복사한다. context, chan은 그대로 활용한다.

ofile과 cwd 경우 master thread가 가리키는 것을 deep copy하지 않고 같은 것을 가리키게 하여 shallow copy한다.  

name은 디버깅용으로 활용한다.

priority queue, schedmode는 master의 것을 활용한다. quantumtick과 timesum또한 master의 것을 활용한다. 허나 시간이 없어 MLFQ, STRIDE scheduler와의 상호작용 부분은구현하지 못하였다.

master변수는 master proc 구조체를 가리킨다. ret_val은 exit시 값을 임시로 저장하는 데 사용한다.

thread가 생성되면 nthreads가 증가한다. 이는 나중에 master thread에서 thread_exit을 하였을 때, 활용된다.

스택으로 구현된 freepage와 freepagesize는 thread_exit을 할 경우 남은 page 자원을 재활용하는 데 사용된다.  

## thread create

```c
// thread_create called in master thread
  if (myproc()->master == 0) {
    master = myproc();
  }
  // thread_create called in slave thread
  else {
    master = myproc()->master;
  }
```

thread create를 master thread에서 부를 수도 있고, slave thread에서 부를 수도 있다. 그것에 따른 master 를 설정해준다.

``` c
// Allocate thread.
  if ((nt = allocproc()) == 0)
  {
    panic("thread_create: allocproc?");
    return -1;
  }		
```

thread는 allocproc을 통해 할당해준다.

``` c
// restore value. it's not creating process
  --nextpid;

  // setting thread's default value using master's value
  nt->master = master;
  nt->parent = master->parent;
  nt->pgdir = master->pgdir;
  nt->pid = master->pid;
  *nt->tf = *master->tf;
```

allocproc을 통해 thread를 할당해주었기 때문에 nextpid의 값을 1 줄여준다.  이후 master를 설정해주고, 공유하는 값을 세팅한다. 그리고 tf를 값 복사한다.

``` c
for(int i = 0; i < NOFILE; i++)
    if(master->ofile[i])
        nt->ofile[i] = master->ofile[i];
  nt->cwd = master->cwd;

  safestrcpy(master->name, nt->name, sizeof(master->name));	
```

ofile과 cwd를 값 복사하지 않고 같은 주소를 바라보도록 한다. 이후 name을 복사해준다.

```c
// check whether freepage exists
  if (master->freepagesize > 0)
  {
    --master->freepagesize;
    sz = master->freepage[master->freepagesize];
  }
  else
  {
    sz = master->sz;
    master->sz = sz + 2*PGSIZE;
  }
```

thread_join에서 exit한 thread를 정리할 때 freepage에 추가해준다. 이후 thread create에서 새로운 thread를 만들 때, freepage가 존재한다면, 그 freepage를 사용하고, 존재하지 않으면 sz 변수를 사용한다.

``` c
if((sz = allocuvm(master->pgdir, sz, sz + 2*PGSIZE)) == 0)
    panic("thread_create: allocuvm");
  
  // setting guard page, making flag PTE_U unavailable
  clearpteu(master->pgdir, (char*)(sz - 2*PGSIZE));
```

위에서 선택한 sz에 2 PGSIZE만큼 새로 할당한다. 그리고 가드페이지를 설정한다.

``` c
// setting thread's arguments
  sp = sz;
  nt->sz = sz;

  sp -= 4;
  *((uint*)sp) = (uint)arg;
  sp -= 4;
  *((uint*)sp) = 0xffffffff; // fake return PC
  
  nt->tid = nexttid++;
  *thread = nt->tid;
  
  nt->tf->eip = (uint)start_routine;
  nt->tf->esp = sp;
  acquire(&ptable.lock);

  nt->state = RUNNABLE;
  ++master->nthreads;
  enqueue(master->priorityqueue, nt);
  
  release(&ptable.lock);
  
  return 0;
```

증가한 sz를 thread에 설정해주고 create에 같이 넣어준 arg변수를 세팅해주고 fake return PC값을 넣어준다. 

nexttid는 전역변수로 0부터 시작한다. 이 값이 양수면 slave 스레드고 0이면 master thread이다. 이후 tf의 eip값을 start_routine의 값으로 변경해주고 esp값을 지금 증가한 sp값으로 세팅해 준 후, # of thread 의 값을 증가시킨다.

state를 RUNNABLE로 바꾸고 스케줄러에 enqueue해주는 것으로 create 함수의 동작은 끝이난다.

## thread_exit

``` c
// thread_exit called in master thread
  if(curthread->master == 0) {
    acquire(&ptable.lock);
    for (;;)
    {
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        // find threads that have curthread as master
        if (p->master != curthread && p->state == UNUSED)
          continue;
        // its job should be finished.
        // As you know once job finished, the process state bacame ZOMBIE. 
        if (p->state == ZOMBIE)
        {
          // Found one.
          kfree(p->kstack);
          p->kstack = 0;
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          p->state = UNUSED;
          --p->master->nthreads;
          p->master->freepage[p->master->freepagesize++] = p->sz - 2*PGSIZE;  
          deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE);
          continue;
        }
      }
      // No point waiting if we don't have any children.
      if (curthread->killed)
      {
        release(&ptable.lock);
        goto exit;
      }
      // Wait for children to exit.
      if (curthread->nthreads != 0) {
        sleep(curthread, &ptable.lock);
      }
      else 
      {
        release(&ptable.lock);
        goto exit;
      }
        
    }
    exit:
      exit();
  }
```

마스터 쓰레드에서 thread_exit을 부를 경우 무한루프를 돌면서 slave 쓰레드가 모두 종료될 때 까지  sleep하며 기다린다. 종료되는 쓰레드마다 자원을 정리해주고, sleep을 반복하다가. nthreads 변수를 통해 모두 종료됐음을 확인하면 exit()을 불러 process를 종료시킨다.

```c
// Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curthread->ofile[fd])
    {
      curthread->ofile[fd] = 0;
    }
  }
  curthread->cwd = 0;
```

master thread에서 exit을 부른 것이 아니라면,  ofile과 cwd를 0으로 만들어준다.

``` c
wakeup1(curthread->master);
```

이후 wakeup을 통해 자고있는 master thread를 깨워준다.

``` c
curthread->ret_val = retval;
curthread->state = ZOMBIE;
```

리턴 벨류를 임시로 curthread에 저장하고, state를 ZOMBIE로 만들어 기다리고 있는 master thread 측에서 정리될 수 있도록 만든다.

## thread_join

``` c
for (;;)
  {  
    // Scan through table looking for exited children.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      // 해당하는 thread ID 찾기
      if (p->tid != thread)
        continue;
      if (p->state == ZOMBIE)
      {
        // Found one.
        kfree(p->kstack);
        p->kstack = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        --p->master->nthreads;
        *retval = p->ret_val;
        p->master->freepage[p->master->freepagesize++] = p->sz - 2*PGSIZE;  
        deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE);
        
        release(&ptable.lock);
        // return 0 on success  
        return 0;
      }
    }

    // No point waiting if we don't have any children.
    if (curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
```

join을 실행한 측에서는 해당하는 thread를 tid값 비교를 통해 찾아 ZOMBIE인지 확인한다. ZOMBIE가 아닐 경우에는 sleep을 하고 나중에 exit될 때 까지 기다린다. ZOMBIE인 경우 자원을 정리해주고, ret_val에 값을 임시로 저장한 리턴 값을 복사하고 join을 종료한다. 이때 page를 deallocation하는데 이 페이지를 freepage스택을 활용하여 정보를 저장해둔다. 이는 thread_create에서 새로운 쓰레드를 생성할 때 활용된다.

## exit

``` c
for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == curproc->pid && p->state != UNUSED) {
      // 본인은 뺀다.
      if(p == curproc) continue;

      kfree(p->kstack);
      p->kstack = 0;
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      
      for (fd = 0; fd < NOFILE; fd++)
      {
        if (p->ofile[fd])
        {
          p->ofile[fd] = 0;
        }
      }
      p->cwd = 0;
      
      p->state = UNUSED;
      --p->master->nthreads;
    }      
  }
```

exit을 master 뿐만 아니라 slave thread에서도 부를 수 있기 때문에 본인을 뺀 모두르를 찾아 정리한다. state가 RUNNING일 수도 있고 그 무엇이든지 간에 UNUSED로 바꿔 버린다.

```c
// Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }
  
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
```



slave thread에서 결국 모든 자원을 mater thread와 공유한다. fileclose와 iput으로 ofile과 cwd를 핸들해주고

```c
wakeup1(curproc->parent);
curproc->state = ZOMBIE;
```

state를 ZOMBIE로 만들고 parent를 깨워준다. 이젠 parent wait단에서 남은 자원들이 정리가 될 것이다.

 ## fork

``` c
if (curproc->master == 0)
  {
    np->freepagesize = curproc->freepagesize;
    for (int i = 0; i < curproc->freepagesize; i++)
    {
    np->freepage[i] = curproc->freepage[i];
    }
  }
else
  {
    np->freepagesize = curproc->master->freepagesize;
    for (int i = 0; i < curproc->master->freepagesize; i++)
    {
    np->freepage[i] = curproc->master->freepage[i];
    }
  }
```

fork는 현재 스레드의 의 sz까지 copyuvm을 통해서 page 단위로 복사해준다. fork를 불렀을 당시 현재 스레드의 sz보다 낮은 메모리 주소 공간에서 thread_exit한 thread의 freepage를 활용해야 하기 때문에 freepage를 복사해준다.

```c
for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == curproc->pid && p->state != UNUSED) {
      // 본인은 뺀다.
      if(p == curproc) {
        continue;
        }
      if (p->sz < curproc->sz)
      {
        np->freepage[np->freepagesize++] = p->sz - 2*PGSIZE;
        deallocuvm(np->pgdir, p->sz, p->sz - 2*PGSIZE);
      }
    }      
  }
```

 본인을 이후 curproc(현재 쓰레드)의 sz보다 아래 주소공간을 사용하고 있던 thread의 주소공간을 clean해준다. 이를 freepage에 넣어 나중에 활용할 수 있도록 한다.

## Exec

Exec은 시간이 없어 구현하지 못했다.

## Sbrk

```c
int growproc(int n)
{
  uint sz;
  struct proc *master;

  if (myproc()->master == 0) {
    master = myproc();
  }
  else {
    master = myproc()->master;
  }
  sz = master->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(master->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(master->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  master->sz = sz;
  switchuvm(master);
  return 0;
}
```

growproc은 master thread의 sz를 증가시켜준다.

``` c
int
sys_sbrk(void)
{
  int addr;
  int n;

  acquire(&growproclock);
  
  if(argint(0, &n) < 0)
    return -1;
  if (myproc()->master == 0) {
    addr = myproc()->sz;
  }
  else {
    addr = myproc()->master->sz;
  }
  
  if(growproc(n) < 0)
    return -1;
  
  release(&growproclock);
  
  return addr;
}
```

이는 sys_sbrk에서 master의 sz를 참조하여 증가된 sz를 정상적으로 활용할 수 있도록 한다.

## Kill

```c
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && p->tid == 0)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
        enqueue(p->priorityqueue, p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
```

thread들은 pid를 공유하고 master thread는 tid가 0이기 때문에 주어진 pid를 활용하여 master thread를 찾아 kill해준다.

## Sleep

proc구조체 기반으로 작동하기 때문의 기존의 sleep코드를 재활용하였다.

## Pipe

파일 디스크립터 관련 코드들은 create할 당시 shallow copy를 하여 Pipe를 share하도록 하였다.