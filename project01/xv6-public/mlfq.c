#define QFAILURE 1
#define QSUCCESS 0
#define MSIZE NPROC + 1

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
    int qsize;
    int capacity;
};

struct
{
    int quantum[NLEVEL];
    int allotment[NLEVEL - 1];
    int boostingperiod;
    struct mlfqueue q[NLEVEL];
} mlfq;

int mlfqenqueue(int, struct proc*);
struct proc* mlfqueuetop(int);
int mlfqdequeue(int);
int mlfqueuesize(int);

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext();
void mlfqboost();

extern int sys_uptime(void);

void mlfqprint()
{
    for (int level = 0; level < NLEVEL; ++level)
    {
        cprintf("level = %d / %d, %d, %d", level, mlfq.q[level].rear, mlfq.q[level].front, mlfq.q[level].qsize);
        for (int j = 1; j <= mlfq.q[level].qsize; ++j)
        {
            cprintf(" %p(%d)", mlfq.q[level].q[(mlfq.q[level].front + j) % mlfq.q[level].capacity], mlfq.q[level].q[(mlfq.q[level].front + j) % mlfq.q[level].capacity]->mlfq.level);
        }
        cprintf("\n");
    }
    cprintf("\n");
}

void mlfqinit()
{
    static int quantum[NLEVEL] = { 2, 2, 4 };
    static int allotment[NLEVEL - 1] = { 5, 10 };
    static int boostingperiod = 100;

    memset(&mlfq, 0, sizeof(mlfq));
    memmove(mlfq.quantum, quantum, NLEVEL);
    memmove(mlfq.allotment, allotment, NLEVEL - 1);
    mlfq.boostingperiod = boostingperiod;

    for (int level = 0; level < NLEVEL; ++level)
    {
        mlfq.q[level].front = -1;
        mlfq.q[level].rear = -1;
        mlfq.q[level].capacity = MSIZE;
    }
}

int mlfqisfull(int level)
{
    return mlfq.q[level].qsize == (mlfq.q[level].capacity - 1);
}

int mlfqisempty(int level)
{
    return !mlfq.q[level].qsize;
}

void mlfqremove(struct proc* p)
{
    int level = p->mlfq.level;
    for (int i = 0; i < mlfqueuesize(level); ++i)
    {
        struct proc* t = mlfqueuetop(level);
        if (mlfqdequeue(level) == QFAILURE)
        {
            panic("mlfqremove: mlfqdequeue failure");
        }

        if (t != p)
        {
            if (mlfqenqueue(level, t) == QFAILURE)
            {
                panic("mlfqremove: mlfqenqueue failure");
            }
        }
    }
}

void mlfqboost()
{
    struct proc* p = 0;
    for (int level = 1; level < NLEVEL; ++level)
    {
        while (!mlfqisempty(level))
        {
            p = mlfqueuetop(level);
            if (!p)
            {
                panic("mlfqboost: mlfqtop failure");
            }
            if (mlfqdequeue(level) == QFAILURE)
            {
                panic("mlfqboost: mlfqdequeue failure");
            }

            p->mlfq.executionticks = 0;
            
            if (mlfqenqueue(0, p) == QFAILURE)
            {
                panic("mlfqboost: mlfqenqueue failure");
            }
        }
    }
}

int mlfqpush(struct proc* p)
{
    memset(&p->mlfq, 0, sizeof(p->mlfq));
    if (mlfqenqueue(0, p) == QFAILURE)
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

    uint executiontick = end - start;
    executiontick = executiontick ? executiontick : 1;
    p->mlfq.executionticks += executiontick;

    int level = p->mlfq.level;
    uint executionticks = p->mlfq.executionticks;

    if (level + 1 < NLEVEL && executionticks >= mlfq.allotment[level])
    {
        if (mlfqdequeue(level) == QFAILURE)
        {
            panic("mlfqnext: mlfqpop failure");
        }
        if (mlfqenqueue(level + 1, p) == QFAILURE)
        {
            panic("mlfqnext: mlfqpush failure");
        }

        return 1;
    }

    int result = executiontick >= mlfq.quantum[level];
    if (result)
    {
        if (mlfqdequeue(level) == QFAILURE)
        {
            panic("mlfqnext: mlfqpop failure");
        }
        if (mlfqenqueue(level, p) == QFAILURE)
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
                panic("mlfqtop: invalid top");
            }

            if (it->state == RUNNABLE)
            {
                return it;
            }

            // round robin
            if (mlfqdequeue(level) == QFAILURE)
            {
                panic("mlfqtop: mlfqdequeue failure");
            }
            if (mlfqenqueue(level, it) == QFAILURE)
            {
                panic("mlfqtop: mlfqenqueue failure");
            }
        }
    }

    return 0;
}

int mlfqenqueue(int level, struct proc* p)
{
    if (mlfq.q[level].qsize < 0 || mlfq.q[level].qsize == mlfq.q[level].capacity || mlfq.q[level].rear < -2 || mlfq.q[level].rear >= 65 || mlfq.q[level].front < -2 || mlfq.q[level].front >= 65)
    {
        panic("???");
    }
    if (mlfqisfull(level))
    {
        return QFAILURE;
    }
    mlfq.q[level].rear = (mlfq.q[level].rear + 1) % mlfq.q[level].capacity;
    mlfq.q[level].q[mlfq.q[level].rear] = p;
    ++mlfq.q[level].qsize;
    p->mlfq.level = level;

    return QSUCCESS;
}

struct proc* mlfqueuetop(int level)
{
    return mlfqisempty(level) ? 0 : mlfq.q[level].q[(mlfq.q[level].front + 1) % mlfq.q[level].capacity];
}

int mlfqdequeue(int level)
{
    if (mlfq.q[level].qsize < 0 || mlfq.q[level].qsize == mlfq.q[level].capacity || mlfq.q[level].rear < -2 || mlfq.q[level].rear >= 65 || mlfq.q[level].front < -2 || mlfq.q[level].front >= 65)
    {
        panic("???");
    }
    if (mlfqisempty(level))
    {
        return QFAILURE;
    }
    mlfq.q[level].front = (mlfq.q[level].front + 1) % mlfq.q[level].capacity;
    --mlfq.q[level].qsize;
    return QSUCCESS;
}

int mlfqueuesize(int level)
{
    return mlfq.q[level].qsize;
}