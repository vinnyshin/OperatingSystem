#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int totalticket = 100;
int usingticket = 0;
int priorityBoostTick = 0;

struct queueMLFQ q0;
struct queueMLFQ q1;
struct queueMLFQ q2;

struct strideheap strideheap;
struct strideproc mlfqstrideproc;

void queueinit(void)
{
  // q0.timeallotment = 50;
  // q0.timequantum = 10;
  q0.timeallotment = 5;
  q0.timequantum = 1;
  q1.timeallotment = 10;
  q1.timequantum = 2;
  // q2's timeallotment is not defined
  q2.timeallotment = -1;
  q2.timequantum = 4;
}

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // for MLFQ scheduling
  p->mode = MLFQ;
  p->timesum = 0;
  p->quantumtick = 0;
  p->priorityqueue = &q0;
  
  release(&ptable.lock);
  
  // for threading
  p->master = 0;

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  enqueue(&q0, p);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  // cprintf("forked enqueue!\n");
  np->state = RUNNABLE;
  if (np->mode == MLFQ)
  {
    enqueue(&q0, np);
  }  
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  
  if (curproc == initproc)
    panic("init exiting");

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
  
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  // cprintf("exit pid: %d\n", curproc->pid);
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

void popmin(struct strideproc* pstrideproc)
{
  if(strideheap.count == 0){
    // Stride Heap cannot be empty
    // MLFQ will always reside in the heap.
    panic("\n__Stride Heap is Empty__\n");
  }
  pstrideproc->mode = strideheap.proc[0].mode;
  pstrideproc->passval = strideheap.proc[0].passval;
  pstrideproc->proc = strideheap.proc[0].proc;
  pstrideproc->stride = strideheap.proc[0].stride;
  pstrideproc->ticket = strideheap.proc[0].ticket;

  // cprintf("before pop[0] stride: %d\n", pstrideproc->stride);
  strideheap.proc[0].mode = strideheap.proc[strideheap.count - 1].mode;
  strideheap.proc[0].passval = strideheap.proc[strideheap.count - 1].passval;
  strideheap.proc[0].proc = strideheap.proc[strideheap.count - 1].proc;
  strideheap.proc[0].stride = strideheap.proc[strideheap.count - 1].stride;
  strideheap.proc[0].ticket = strideheap.proc[strideheap.count - 1].ticket;
  
  // cprintf("after pop[0] stride: %d\n", pstrideproc->stride);
  
  strideheap.count = strideheap.count - 1;
  
  // cprintf("after pop cnt: %d\n", strideheap.count);
  
  heapify();
  
  // cprintf("pop proc[0] stride: %d\n", strideheap.proc[0].stride);
  // cprintf("pop proc[1] stride: %d\n", strideheap.proc[1].stride);

  // cprintf("====== pop[0] ====== stride: %d\n", pstrideproc->stride);
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
struct strideproc pstrideproc;
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    // cprintf("LOOP!");

    popmin(&pstrideproc);

    // cprintf("mode: %d, stride: %d\n", pstrideproc.mode,pstrideproc.stride);

    pstrideproc.passval = pstrideproc.passval + pstrideproc.stride;
    // cprintf("%d | ", pstrideproc->passval);
    if (pstrideproc.mode == STRIDE)
    {
      // cprintf("LOOP!");
      p = pstrideproc.proc;
      // cprintf("pstrideproc->passval: %d\n", pstrideproc->passval);
      // cprintf("pstrideproc->stride: %d\n", pstrideproc->stride);
      
      // cprintf("pid: %d passval: %d\n", p->pid, strideproc.passval);
      if (p->state == ZOMBIE || p->state == UNUSED)
      {
        usingticket = usingticket - pstrideproc.ticket;
        // cprintf("usingticket: %d\n", usingticket);
        // insert(pstrideproc);
        release(&ptable.lock);
        continue;
      }

      // cprintf("passval");
      c->proc = p;
      // cprintf("%d state\n", p->state);
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      // cprintf("stride fin");
      insert(&pstrideproc);
      release(&ptable.lock);
      continue;
    }
    else
    {
      if (priorityBoostTick < 100)
      {
        
        if (q0.rear != q0.front)
        {
          p = dequeue(&q0);
          
          if (p->state == ZOMBIE || p->state == UNUSED)
          {
            insert(&pstrideproc);
            release(&ptable.lock);
            continue;
          }

          ++priorityBoostTick;
          ++p->quantumtick;
          ++p->timesum;

          if (p->timesum > q0.timeallotment)
          {
            p->timesum = 0;
            p->priorityqueue = &q1;
            p->quantumtick = 0;
            enqueue(&q1, p);
            insert(&pstrideproc);
            release(&ptable.lock);
            continue;
          }

          c->proc = p;
          // cprintf("%d state\n", p->state);
          switchuvm(p);
          p->state = RUNNING;
          // cprintf("LOOP!");
          // cprintf("tid: %d\n", p->tid);

          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
        else if (q1.rear != q1.front)
        {
          
          p = dequeue(&q1);

          if (p->state == ZOMBIE || p->state == UNUSED)
          {
            insert(&pstrideproc);
            release(&ptable.lock);
            continue;
          }

          ++priorityBoostTick;
          ++p->quantumtick;
          ++p->timesum;

          if (p->timesum > q1.timeallotment)
          {
            p->timesum = 0;
            p->priorityqueue = &q2;
            p->quantumtick = 0;
            enqueue(&q2, p);
            insert(&pstrideproc);
            release(&ptable.lock);
            continue;
          }

          c->proc = p;

          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
        else if (q2.rear != q2.front)
        {
          
          p = dequeue(&q2);

          if (p->state == ZOMBIE || p->state == UNUSED)
          {
            insert(&pstrideproc);
            release(&ptable.lock);
            continue;
          }

          ++priorityBoostTick;
          ++p->quantumtick;

          c->proc = p;
          // cprintf("%d state\n", p->state);
          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
      }
      else
      {
        while (isempty(&q2) == 0)
        {
          p = dequeue(&q2);
          p->timesum = 0;
          p->priorityqueue = &q0;
          p->quantumtick = 0;
          enqueue(&q0, p);
        }

        while (isempty(&q1) == 0)
        {
          p = dequeue(&q1);
          p->timesum = 0;
          p->priorityqueue = &q0;
          p->quantumtick = 0;
          enqueue(&q0, p);
        }
        priorityBoostTick = 0;
      }
    }
    
    // cprintf("%d\n", pstrideproc.ticket); 
    insert(&pstrideproc);
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  if (myproc()->mode == MLFQ)
  {
    enqueue(myproc()->priorityqueue, myproc());
  }  
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
    {
      p->state = RUNNABLE;
      if(p->mode == MLFQ)
      {
        enqueue(p->priorityqueue, p);
      }
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int sys_yield(void)
{
  yield();
  return 0;
}

int sys_getlev(void)
{
  return getlev();
}

int getlev(void)
{
  // getlev: get the level of current process ready queue of MLFQ. Returns one of the level of MLFQ (0/1/2)
  struct queueMLFQ *currentqueue = myproc()->priorityqueue;
  if (currentqueue == &q0)
  {
    return 0;
  }
  else if (currentqueue == &q1)
  {
    return 1;
  }
  else if (currentqueue == &q2)
  {
    return 2;
  }
  else
  {
    return -1;
  }
}

int sys_set_cpu_share(void)
{
  int portion = 0;

  if (argint(0, &portion) < 0)
  {
    return -1;
  }
  
  if (portion <= 0)
  {
  	return -1;
  }

  if (portion + usingticket > totalticket)
  {
  	return -1;
  }

  usingticket = usingticket + portion;
  
  struct strideproc strideproc;
  myproc()->mode = STRIDE;
  strideproc.mode = STRIDE;
  strideproc.proc = myproc();
  strideproc.ticket = portion;
  strideproc.passval = strideheap.proc[0].passval;

  // cprintf("mode: %d\n", strideproc.mode);
  // cprintf("procpid: %d\n", strideproc.proc->pid);
  // cprintf("ticket0: %d\n", strideheap.proc[0].ticket);
  // cprintf("ticket1: %d\n", strideproc.ticket);
  
  // cprintf("passval: %d\n", strideproc.passval);
  
  acquire(&ptable.lock);
  insert(&strideproc);
  release(&ptable.lock);

  return 0;
}

int thread_create(thread_t * thread, void * (*start_routine)(void *), void *arg)
{  
  struct proc *nt;
  struct proc *master;
  uint sz, sp;

  // thread_create called in master thread
  if (myproc()->master == 0) {
    master = myproc();
  }
  // thread_create called in slave thread
  else {
    master = myproc()->master;
  }

  // Allocate thread.
  if ((nt = allocproc()) == 0)
  {
    panic("thread_create: allocproc?");
    return -1;
  }
  
  // restore value. it's not creating process
  --nextpid;

  // setting thread's default value using master's value
  nt->master = master;
  nt->pgdir = master->pgdir;
  nt->pid = master->pid;
  *nt->tf = *master->tf;
  for(int i = 0; i < NOFILE; i++)
    if(master->ofile[i])
        nt->ofile[i] = master->ofile[i];
  nt->cwd = master->cwd;
  safestrcpy(master->name, nt->name, sizeof(master->name));
  

  // 익스터널 프래그멘테이션 해결 필요
  // 해결
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
  // stack allocation
  // cprintf("master->sz: %d\n", sz);
  sz = PGROUNDUP(sz);
  
  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  
  if((sz = allocuvm(master->pgdir, sz, sz + 2*PGSIZE)) == 0)
    panic("thread_create: allocuvm");
  
  // setting guard page, making flag PTE_U unavailable
  clearpteu(master->pgdir, (char*)(sz - 2*PGSIZE));
  
  // sz += 2*PGSIZE;
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
  enqueue(&q0, nt);
  
  release(&ptable.lock);
  
  return 0;
}

void thread_exit(void *retval) {
  struct proc *curthread = myproc();
  struct proc *p;
  int fd;
  
  if(curthread->master == 0) {
    // cprintf("thread exit called in master thread!\n");
    
    acquire(&ptable.lock);

    for (;;)
    {
      // printf(1, "shibal");
      
      // Scan through table looking for exited children.
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        // 해당하는 thread 찾기
        if (p->master != curthread && p->state != UNUSED)
          continue;
        if (p->state == ZOMBIE)
        {
          // cprintf("exit: %d\n", p->tid);
          // Found one.
          // tid = p->tid;
          kfree(p->kstack);
          p->kstack = 0;
          // freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          p->state = UNUSED;
          --p->master->nthreads;
          // p->master = 0;
          // *retval = p->ret_val;
          p->master->freepage[p->master->freepagesize++] = p->sz - 2*PGSIZE;  
          deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE);
          // release(&ptable.lock);
          // // return 0 on success
          continue;
          // return 0;
        }
      }

      // No point waiting if we don't have any children.
      if (curthread->killed)
      {
        release(&ptable.lock);
        goto exit;
      }
      // Wait for children to exit.  (See wakeup1 call in proc_exit.)
      if (curthread->nthreads != 0) {
        sleep(curthread, &ptable.lock); //DOC: wait-sleep
      }
      else {
        release(&ptable.lock);
        goto exit;
      }
        
    }
    exit:
      exit();
  }

  // cprintf("exit!\n");
  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curthread->ofile[fd])
    {
      // fileclose(curthread->ofile[fd]);
      curthread->ofile[fd] = 0;
    }
  }

  // begin_op();
  // iput(curthread->cwd);
  // end_op();
  curthread->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  
  wakeup1(curthread->master);

  // // // Pass abandoned children to init.
  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if (p->parent == curthread)
  //   {
  //     p->parent = initproc;
  //     if (p->state == ZOMBIE)
  //       wakeup1(initproc);
  //   }
  // }
  
  // Jump into the scheduler, never to return.
  curthread->ret_val = retval;
  curthread->state = ZOMBIE;
  // cprintf("exit tid: %d\n", curthread->tid);
  sched();
  panic("zombie exit");
}

int thread_join(thread_t thread, void **retval) {
  struct proc *p;
  // int tid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for (;;)
  {
    // printf(1, "shibal");
    
    // Scan through table looking for exited children.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      // 해당하는 thread ID 찾기
      if (p->tid != thread)
        continue;
      if (p->state == ZOMBIE)
      {
        // Found one.
        // tid = p->tid;
        kfree(p->kstack);
        p->kstack = 0;
        // freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        --p->master->nthreads;
        // p->master = 0;
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
    // cprintf("SLEEp!!\n");
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}