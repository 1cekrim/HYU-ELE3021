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
#include "scheduler.h"

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
    int boostingperiod;
    struct mlfqueue q[NLEVEL];
} mlfq;

int mlfqueuepush(int, struct proc*);
struct proc* mlfqueuetop(int);
int mlfqueuepop(int);
int mlfqueuesize(int);

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext();
void mlfqboost();

int pqparent(int index)
{
    return (index - 1) / 2;
}

int pqleftchild(int index)
{
    return 2 * index + 1;
}

int pqrightchild(int index)
{
    return 2 * index + 2;
}

void pqinit(struct priorityqueue* pq)
{
    memset(pq->data, 0, sizeof(pq->data));
    pq->size = 0;
    pq->capacity = PQCAPACITY;
}

struct pqelement pqtop(struct priorityqueue* pq)
{
    return pq->data[0];
}

void pqshiftup(struct priorityqueue* pq, int index)
{
    int parent = pqparent(index);
    while (index > 0 && pq->data[parent].key < pq->data[index].key)
    {
        struct pqelement tmp = pq->data[parent];
        pq->data[parent] = pq->data[index];
        pq->data[index] = tmp;
        index = parent;
    }
}

void pqshiftdown(struct priorityqueue* pq, int index)
{
    int maxidx = index;
    int left = pqleftchild(index);
    if (left <= pq->size && pq->data[left].key > pq->data[maxidx].key)
    {
        maxidx = left;
    }

    int right = pqrightchild(index);
    if (right <= pq->size && pq->data[right].key > pq->data[maxidx].key)
    {
        maxidx = right;
    }

    if (index != maxidx)
    {
        struct pqelement tmp = pq->data[maxidx];
        pq->data[maxidx] = pq->data[index];
        pq->data[index] = tmp;
        pqshiftdown(pq, maxidx);
    }
}

int pqpush(struct priorityqueue* pq, int key, void* value)
{
    if (pq->size >= pq->capacity)
    {
        return pq->size;
    }
    pq->data[pq->size] = (struct pqelement){key, value};
    pqshiftup(pq, pq->size);
    ++pq->size;
    return 0;
}

void pqupdatetop(struct priorityqueue* pq, int key, void* value)
{
    pq->data[0] = (struct pqelement){key, value};
    pqshiftdown(pq, 0);
}

int pqpop(struct priorityqueue* pq)
{
    if (pq->size <= 0)
    {
        return 1;
    }
    pq->data[0] = pq->data[pq->size - 1];
    --pq->size;
    pqshiftdown(pq, 0);
    return 0;
}


extern int sys_uptime(void);

void mlfqinit()
{
    static int quantum[NLEVEL] = { 1, 2, 4 };
    static int allotment[NLEVEL - 1] = { 5, 10 };
    static int boostingperiod = 100;

    memset(&mlfq, 0, sizeof(mlfq));
    memmove(mlfq.quantum, quantum, NLEVEL);
    memmove(mlfq.allotment, allotment, NLEVEL - 1);
    mlfq.boostingperiod = boostingperiod;
}

void mlfqremove(struct proc* p)
{
    int level = p->mlfq.level;
    for (int i = 0; i < mlfqueuesize(level) - 1; ++i)
    {
        struct proc* t = mlfqueuetop(level);
        if (mlfqueuepop(level) == QFAILURE)
        {
            panic("mlfqremove: mlfqueuepop failure");
        }

        if (t != p)
        {
            if (mlfqueuepush(level, t) == QFAILURE)
            {
                panic("mlfqremove: mlfqueuepush failure");
            }
        }
    }
}

void mlfqboost()
{
    struct proc* p = 0;
    for (int level = 1; level < NLEVEL; ++level)
    {
        while ((p = mlfqueuetop(level)))
        {
            if (mlfqueuepop(level) == QFAILURE)
            {
                panic("mlfqboost: mlfqueuepop failure");
            }

            p->mlfq.executionticks = 0;
            
            if (mlfqueuepush(0, p) == QFAILURE)
            {
                panic("mlfqboost: mlfqueuepush failure");
            }
        }
    }
}

int mlfqpush(struct proc* p)
{
    memset(&p->mlfq, 0, sizeof(p->mlfq));
    if (mlfqueuepush(0, p) == QFAILURE)
    {
        return QFAILURE;
    }
    return QSUCCESS;
}

int mlfqnext(struct proc* p, uint start, uint end)
{
    static uint nextboostingtick;
    if (p->killed || p->state == ZOMBIE)
    {
        return 1;
    }

    int executiontick = end - start + 1;
    p->mlfq.executionticks += executiontick;

    int level = p->mlfq.level;
    int executionticks = p->mlfq.executionticks;

    if (level + 1 < NLEVEL && executionticks >= mlfq.allotment[level])
    {
        if (mlfqueuepop(level) == QFAILURE)
        {
            panic("mlfqnext: mlfqpop failure");
        }
        if (mlfqueuepush(level + 1, p) == QFAILURE)
        {
            panic("mlfqnext: mlfqpush failure");
        }

        return 1;
    }

    int result = executiontick >= mlfq.quantum[level];
    if (result)
    {
        if (mlfqueuepop(level) == QFAILURE)
        {
            panic("mlfqnext: mlfqpop failure");
        }
        if (mlfqueuepush(level, p) == QFAILURE)
        {
            panic("mlfqnext: mlfqpush failure");
        }
    }

    if (nextboostingtick <= end)
    {
        mlfqboost();
        nextboostingtick = end + mlfq.boostingperiod;
    }

    return result;
}

struct proc* mlfqtop()
{
    for (int level = 0; level < NLEVEL; ++level)
    {
        int size = mlfqueuesize(level);
        
        // 해당 level이 비어 있음
        if (!size)
        {
            continue;
        }

        struct proc* it;

        // q에서 runnable한 프로세스틑 찾음
        for (int i = 0; i < size; ++i)
        {
            it = mlfqueuetop(level);
            if (!it)
            {
                panic("mlfqnext: invalid top");
            }

            if (it->state == RUNNABLE)
            {
                return it;
            }

            // round robin
            mlfqueuepop(level);
            mlfqueuepush(level, it);
        }
    }

    return 0;
}

int mlfqueuepush(int level, struct proc* p)
{
    if (ISFULLQ)
    {
        return QFAILURE;
    }

    mlfq.q[level].q[mlfq.q[level].rear = (mlfq.q[level].rear + 1) % MSIZE] = p;
    
    p->mlfq.level = level;

    return QSUCCESS;
}

struct proc* mlfqueuetop(int level)
{
    return ISEMPTYQ ? 0 : mlfq.q[level].q[(mlfq.q[level].front + 1) % MSIZE];
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

int mlfqueuesize(int level)
{
    struct mlfqueue* q = &mlfq.q[level];
    int front = q->front;
    int rear = q->rear;
    int v = rear - front;
    return v >= 0 ? v : (MSIZE + v);
}