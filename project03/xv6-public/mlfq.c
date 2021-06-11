#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int isempty(struct queueMLFQ* queue)
{
    if(queue->rear == queue->front)
    {
        return 1;
    }
    return 0;
}

void enqueue(struct queueMLFQ* queue, struct proc* proc)
{
    queue->rear = (queue->rear + 1) % NPROC;
    queue->proc[queue->rear] = proc;
}

struct proc* dequeue(struct queueMLFQ* queue)
{
    queue->front = (queue->front + 1) % NPROC;
    return queue->proc[queue->front];
}