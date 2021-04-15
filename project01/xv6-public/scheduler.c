#define QFAILURE 1
#define QSUCCESS 0
#define MSIZE NPROC + 1

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

void strideprint(struct stridescheduler*);
void pqprint(struct priorityqueue*);

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
    while (index > 0 && pq->data[parent].key > pq->data[index].key)
    {
        struct pqelement tmp = pq->data[parent];
        pq->data[parent] = pq->data[index];
        pq->data[index] = tmp;
        index = parent;
    }
}

void pqshiftdown(struct priorityqueue* pq, int index)
{
    int minidx = index;
    int left = pqleftchild(index);
    if (left < pq->size && pq->data[left].key < pq->data[minidx].key)
    {
        minidx = left;
    }

    int right = pqrightchild(index);
    if (right < pq->size && pq->data[right].key < pq->data[minidx].key)
    {
        minidx = right;
    }

    if (index != minidx)
    {
        struct pqelement tmp = pq->data[minidx];
        pq->data[minidx] = pq->data[index];
        pq->data[index] = tmp;
        pqshiftdown(pq, minidx);
    }
}

int pqpush(struct priorityqueue* pq, struct pqelement element)
{
    if (pq->size >= pq->capacity)
    {
        return pq->size;
    }
    pq->data[pq->size] = element;
    pqshiftup(pq, pq->size);
    ++pq->size;
    return 0;
}

void pqupdatetop(struct priorityqueue* pq, struct pqelement element)
{
    pq->data[0] = element;
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

    int executiontick = end - start + ((p->mlfq.yield) ? 1 : 0);
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

    int result = (executiontick >= mlfq.quantum[level]) || p->mlfq.yield || p->state == SLEEPING;
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
    p->mlfq.yield = 0;

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

void strideinit(struct stridescheduler* ss, int maxticket)
{
    if (maxticket > STRIDEMAXTICKET)
    {
        panic("strideinit: maxticket > STRIDEMAXTICKET");
    }

    pqinit(&ss->pq);
    ss->totalusage = 0;
    ss->maxticket = maxticket;

    ss->stride[0] = 0;
    ss->minusage = 0;
    for (int i = 1; i <= maxticket; ++i)
    {
        ss->stride[i] = (double)1 / i;
    }
}

int stridepush(struct stridescheduler* ss, void* value, int usage)
{
    if (usage <= 0 || ss->totalusage + usage > ss->maxticket)
    {
        return QFAILURE;
    }

    ss->totalusage -= usage;

    int min = pqtop(&ss->pq).key;

    struct pqelement element;
    element.key = min + ss->minusage;
    element.value = value;
    element.value2 = (void*)usage;
    
    pqpush(&ss->pq, element);

    return 0;
}

void* stridetop(struct stridescheduler* ss)
{
    struct pqelement result = pqtop(&ss->pq);
    return result.value;
}

int stridenext(struct stridescheduler* ss)
{
    struct pqelement result = pqtop(&ss->pq);
    result.key += ss->stride[(int)result.value2];
    pqupdatetop(&ss->pq, result);
    return 0;
}

int strideremove(struct stridescheduler* ss, void* value)
{
    int find = -1;
    for (int index = 0; index < ss->pq.size; ++index)
    {
        if (ss->pq.data[index].value == value)
        {
            find = index;
            break;
        }
    }

    if (find == -1)
    {
        return 0;
    }

    int usage = (int)ss->pq.data[find].value2;
    ss->pq.data[find].key = ss->pq.data[0].key - 1;

    pqshiftup(&ss->pq, find);
    pqpop(&ss->pq);

    ss->totalusage += usage;

    return usage;
}

void strideprint(struct stridescheduler* stride)
{
    pqprint(&stride->pq);
}

void pqprint(struct priorityqueue* pq)
{
    for (int index = 0; index < pq->size; ++index)
    {
        cprintf("%d(%d, %d) ", (int)(pq->data[index].key * 100), pq->data[index].value, pq->data[index].value2);
    }
    cprintf("\n");
}
