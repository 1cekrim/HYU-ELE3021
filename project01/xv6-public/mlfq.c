#define QFAILURE 1
#define QSUCCESS 0
    #define ISFULLQ (((mlfq.q[level].rear + 1) % MSIZE) == mlfq.q[level].front)
    #define ISEMPTYQ (mlfq.q[level].front == mlfq.q[level].rear)
#define MSIZE NPROC

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "mlfq.h"

struct mlfqueue
{
    struct proc* q[MSIZE];
    int rear;
    int front;
};

struct
{
    int quantum[NLEVEL];
    int allotment[NLEVEL - 1];
    struct mlfqueue q[NLEVEL];
} mlfq;

void mlfqinit()
{
    for (int i = 0; i < NLEVEL; ++i)
    {
        mlfq.q[i].front = 0;
        mlfq.q[i].rear = 0;
    }
}

int mlfqueuepush(int level, struct proc* p)
{
    if (ISFULLQ)
    {
        return QFAILURE;
    }

    mlfq.q[level].q[mlfq.q[level].rear = (mlfq.q[level].rear + 1) % MSIZE] = p;
    
    return QSUCCESS;
}

struct proc* mlfqueuetop(int level)
{
    return ISEMPTYQ ? 0 : mlfq.q[level].q[mlfq.q[level].front];
}

int mlfqueuepop(int level)
{
    if (ISEMPTYQ)
    {
        return QFAILURE;
    }
    mlfq.q[level].front = (mlfq.q[level].front + 1) % MSIZE;
    return QSUCCESS;
}