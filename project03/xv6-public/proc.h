// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

enum schedmode { MLFQ, STRIDE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  
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

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

struct queueMLFQ {
  struct proc* proc[NPROC];
	int front;
	int rear;
	// each process will get diferrent time allotment
	// for q0, 5 ticks (highest)
	// for q1, 10 ticks (middle)
	// for q2, no ticks counted (lowest)
	int timeallotment;
	// each process will get differrent Round Robin policy
	// for q0, 1 tick (highest)
	// for q1, 2 ticks(middle)
	// for q2, 4 ticks(lowest)
	int timequantum;
};

// Process for stride scheduling
struct strideproc
{
  struct proc* proc;
  int ticket;       
  // total ticket / ticket 
  double stride;       
  // how much the process traveled, if a new process created, its passval will be initialized with minimun passval among the processes
  double passval;
  //MLFQ ,STRIDE mode 
  enum schedmode mode;
};

struct strideheap {
    struct strideproc proc[NPROC];
    int count;
};