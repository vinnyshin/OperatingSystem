#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

extern struct strideheap strideheap;
extern int totalticket;
extern int usingticket;
extern struct strideproc mlfqstrideproc;

// iterative heapify
void heapify(void)
{
  for (int i = 1; i < strideheap.count; ++i)
  {
    // cprintf("strideheap.proc[i].passval: %d\n", strideheap.proc[i].passval);
    // cprintf("strideheap.proc[(i - 1) / 2].passval: %d\n", strideheap.proc[(i - 1) / 2].passval);
      
    if (strideheap.proc[i].passval < strideheap.proc[(i - 1) / 2].passval)
    {
      int j = i;
      // cprintf("strideheap.proc[i].passval: %d\n", strideheap.proc[i].passval);
      // cprintf("strideheap.proc[(i - 1) / 2].passval: %d\n", strideheap.proc[(i - 1) / 2].passval);
      while (strideheap.proc[j].passval < strideheap.proc[(j - 1) / 2].passval)
      {
        //swap
        struct strideproc temp;
        temp.mode = strideheap.proc[(j - 1) / 2].mode;
        temp.passval = strideheap.proc[(j - 1) / 2].passval;
        temp.proc = strideheap.proc[(j - 1) / 2].proc;
        temp.stride = strideheap.proc[(j - 1) / 2].stride;
        temp.ticket = strideheap.proc[(j - 1) / 2].ticket;

        strideheap.proc[(j - 1) / 2].mode = strideheap.proc[j].mode;
        strideheap.proc[(j - 1) / 2].passval = strideheap.proc[j].passval;
        strideheap.proc[(j - 1) / 2].proc = strideheap.proc[j].proc;
        strideheap.proc[(j - 1) / 2].stride = strideheap.proc[j].stride;
        strideheap.proc[(j - 1) / 2].ticket = strideheap.proc[j].ticket;

        strideheap.proc[j].mode = temp.mode;
        strideheap.proc[j].passval = temp.passval;
        strideheap.proc[j].proc = temp.proc;
        strideheap.proc[j].stride = temp.stride;
        strideheap.proc[j].ticket = temp.ticket;

        j = (j - 1) / 2;
      }
    }
  }
}

void insert(struct strideproc* strideproc)
{
  if(strideheap.count == NPROC)
  {
    panic("\n__Cannot insert Key__");
  }
  
  // cprintf("before index %d", strideheap.count);
  
  strideheap.proc[strideheap.count].mode = strideproc->mode;
  strideheap.proc[strideheap.count].passval = strideproc->passval;
  strideheap.proc[strideheap.count].proc = strideproc->proc;
  // cprintf("stride: %d", strideproc->stride);
  strideheap.proc[strideheap.count].stride = (double)totalticket / (double)(strideproc->ticket);
  strideheap.proc[strideheap.count].ticket = strideproc->ticket;
  strideheap.count = strideheap.count + 1;
  // cprintf("after index: %d", strideheap.count);
  // for (int i = 0; i < strideheap.count; i++)
  // {
  //   cprintf("ticket%d: %d",i, strideheap.proc[i].ticket);
  // }
  // cprintf("ticket: %d", strideheap.proc[index].ticket);
  heapify();
  // cprintf("proc[0] stride: %d\n", strideheap.proc[0].stride);
  // cprintf("proc[1] stride: %d\n", strideheap.proc[1].stride);
  // cprintf("ticket: %d", strideheap.proc[index].ticket);
}

void heapinit(void)
{
  mlfqstrideproc.mode = MLFQ;
  mlfqstrideproc.passval = 0;
  mlfqstrideproc.ticket = 20;
  usingticket = usingticket + mlfqstrideproc.ticket;
  mlfqstrideproc.proc = 0;
  insert(&mlfqstrideproc);

  // struct strideproc testproc;
  // testproc.mode = STRIDE;
  // testproc.passval = 0;
  // testproc.ticket = 5;
  // usingticket = usingticket + testproc.ticket;
  // testproc.proc = 0;
  // insert(&testproc);
  // cprintf("%d\n", strideheap.count);

  
  // cprintf("proc[0] stride: %d\n", strideheap.proc[0].stride);
  // cprintf("proc[1] stride: %d\n", strideheap.proc[1].stride);
}

// struct strideproc* popmin(void)
// {
//   struct strideproc* pop;
//   if(strideheap.count == 0){
//     // Stride Heap cannot be empty
//     // MLFQ will always reside in the heap.
//     panic("\n__Stride Heap is Empty__\n");
//   }

//   pop = &strideheap.proc[0];
//   cprintf("before pop[0] stride: %d\n", pop->stride);
//   strideheap.proc[0] = strideheap.proc[strideheap.count - 1];
//   cprintf("after pop[0] stride: %d\n", pop->stride);
//   strideheap.count = strideheap.count - 1;
//   cprintf("after pop cnt: %d\n", strideheap.count);
//   heapify();
//   cprintf("pop proc[0] stride: %d\n", strideheap.proc[0].stride);
//   cprintf("pop proc[1] stride: %d\n", strideheap.proc[1].stride);

//   cprintf("====== pop[0] ====== stride: %d\n", pop->stride);
//   return pop;
// }