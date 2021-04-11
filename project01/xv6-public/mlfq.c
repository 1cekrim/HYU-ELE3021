#define QFAILURE 1
#define QSUCCESS 0
#define ISFULLQ (((mlfqueue.rear + 1) % MSIZE) == mlfqueue.front)
#define ISEMPTYQ (mlfqueue.front == mlfqueue.rear)
#define MSIZE NPROC

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "mlfq.h"



struct
{
    struct proc* q[MSIZE];
    int rear;
    int front;
} mlfqueue;

void mlfqinit()
{
    mlfqueue.front = 0;
    mlfqueue.rear = 0;
}

int mlfqueuepush(struct proc* p)
{
    if (ISFULLQ)
    {
        return QFAILURE;
    }

    mlfqueue.q[mlfqueue.rear = (mlfqueue.rear + 1) % MSIZE] = p;
    
    return QSUCCESS;
}

struct proc* mlfqueuetop()
{
    return ISEMPTYQ ? 0 : mlfqueue.q[mlfqueue.front];
}

int mlfqueuepop()
{
    if (ISEMPTYQ)
    {
        return QFAILURE;
    }
    mlfqueue.front = (mlfqueue.front + 1) % MSIZE;
    return QSUCCESS;
}