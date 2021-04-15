#define QFAILURE 1
#define QSUCCESS 0
#define MSIZE NPROC + 1
// #define DEBUGFLAG

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

int pqparent(int index);
int pqleftchild(int index);
int pqrightchild(int index);
void pqinit(struct priorityqueue* pq);
struct pqelement pqtop(struct priorityqueue* pq);
void pqshiftup(struct priorityqueue* pq, int index);
void pqshiftdown(struct priorityqueue* pq, int index);
int pqpush(struct priorityqueue* pq, struct pqelement element);
void pqupdatetop(struct priorityqueue* pq, struct pqelement element);
int pqpop(struct priorityqueue* pq);
void mlfqprint();
void mlfqinit();
int mlfqisfull(int level);
int mlfqisempty(int level);
void mlfqremove(struct proc* p);
void schedremoveproc(struct proc* p);
void mlfqboost();
int mlfqpush(struct proc* p);
int mlfqnext(struct proc* p, uint start, uint end);
struct proc* mlfqtop();
int mlfqenqueue(int level, struct proc* p);
struct proc* mlfqueuetop(int level);
int mlfqdequeue(int level);
int mlfqueuesize(int level);
void strideinit(struct stridescheduler* ss, int maxticket);
int stridepush(struct stridescheduler* ss, void* value, int usage);
void* stridetop(struct stridescheduler* ss);
int stridenext(struct stridescheduler* ss);
int stridefindindex(struct stridescheduler* ss, void* value);
void strideupdateminusage(struct stridescheduler* ss);
int stridechangeusage(struct stridescheduler* ss, int index, int usage);
int strideremove(struct stridescheduler* ss, void* value);
void strideprint(struct stridescheduler* stride);
void pqprint(struct priorityqueue* pq);
int set_cpu_share(struct proc* p, int usage);

struct stridescheduler mainstride;
struct stridescheduler masterscheduler;

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
#ifdef DEBUGFLAG
    for (int level = 0; level < NLEVEL; ++level)
    {
        cprintf("level = %d / %d, %d, %d", level, mlfq.q[level].rear, mlfq.q[level].front, mlfq.q[level].qsize);
        for (int j = 1; j <= mlfq.q[level].qsize; ++j)
        {
            cprintf(" %p(%d)", mlfq.q[level].q[(mlfq.q[level].front + j) % mlfq.q[level].capacity], mlfq.q[level].q[(mlfq.q[level].front + j) % mlfq.q[level].capacity]->schedule.level);
        }
        cprintf("\n");
    }
    cprintf("\n");
#endif
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
    int flag = 1;
    int level = p->schedule.level;
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
        else
        {
            flag = 0;
        }
    }

    if (flag)
    {
        panic("mlfqremove: no proc");
    }
}

void schedremoveproc(struct proc* p)
{
    switch (p->schedule.sched)
    {
        case SCHEDMLFQ:
            mlfqremove(p);
            break;
        case SCHEDSTRIDE:
        {
            int usage = strideremove(&mainstride, p);
            if (usage == -1)
            {
                panic("schedremoveproc: usage == -1");
            }
            
            int mlfqidx = stridefindindex(&masterscheduler, (void*)SCHEDMLFQ);
            int mlfqusage = (int)masterscheduler.pq.data[mlfqidx].usage;
            int newmlfqusage = mlfqusage + usage;

            int strideidx = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
            int strideusage = (int)masterscheduler.pq.data[strideidx].usage;
            int newstrideusage = strideusage - usage;
#ifdef DEBUGFLAG
            cprintf("[schedremoveproc]\n");
            cprintf("removed usage: %d\n", usage);
            cprintf("mlfq usage: %d -> %d\n", mlfqusage, newmlfqusage);
            cprintf("stride usage: %d -> %d\n", strideusage, newstrideusage);
#endif

            if (newmlfqusage + newstrideusage != masterscheduler.maxticket || newmlfqusage < 20 || newstrideusage < 0)
            {
                panic("schedremoveproc: invalid usage rate");
            }

            if (stridechangeusage(&masterscheduler, strideidx, newstrideusage))
            {
                panic("schedremoveproc: strideidx change failure");
            }
            if(stridechangeusage(&masterscheduler, mlfqidx, newmlfqusage))
            {
                panic("schedremoveproc: mlfqidx change failure");
            }

            strideupdateminusage(&masterscheduler);
            break;
        }
        default:
            panic("schedremoveproc: invalid sched");
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

            p->schedule.executionticks = 0;
            
            if (mlfqenqueue(0, p) == QFAILURE)
            {
                panic("mlfqboost: mlfqenqueue failure");
            }
        }
    }
}

int mlfqpush(struct proc* p)
{
    memset(&p->schedule, 0, sizeof(p->schedule));
    p->schedule.sched = SCHEDMLFQ;
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

    int executiontick = end - start + ((p->schedule.yield) ? 1 : 0);
    p->schedule.executionticks += executiontick;

    int level = p->schedule.level;
    uint executionticks = p->schedule.executionticks;

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

    int result = (executiontick >= mlfq.quantum[level]) || p->schedule.yield || p->state == SLEEPING;
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
    p->schedule.level = level;
    p->schedule.yield = 0;

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

    ss->totalusage += usage;
    if (ss->minusage > usage)
    {
        ss->minusage = usage;
    }

    int min = pqtop(&ss->pq).key;

    struct pqelement element;
    element.key = min + ss->minusage;
    element.value = value;
    element.usage = usage;
    
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
    result.key += ss->stride[(int)result.usage];
    pqupdatetop(&ss->pq, result);
    return 1;
}

// failure: return -1
int stridefindindex(struct stridescheduler* ss, void* value)
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
    return find;
}

void strideupdateminusage(struct stridescheduler* ss)
{
    int minusage = ss->maxticket + 1;
    for (int index = 0; index < ss->pq.size; ++index)
    {
        minusage = (minusage > (int)ss->pq.data[index].usage) ? (int)ss->pq.data[index].usage : minusage;
    }
    ss->minusage = minusage;
}

int stridechangeusage(struct stridescheduler* ss, int index, int usage)
{
    if (usage == 0)
    {
        return strideremove(ss, ss->pq.data[index].value) == -1;
    }

    int oldusage = (int)ss->pq.data[index].usage;
    int newtotal = ss->totalusage - oldusage + usage;
    if (usage <= 0 || newtotal <= 0 || newtotal > ss->maxticket)
    {
        return -1;
    }

    ss->pq.data[index].usage = usage;
    ss->totalusage = newtotal;

    if (oldusage == ss->minusage)
    {
        strideupdateminusage(ss);
    }

    return 0;
}

int strideremove(struct stridescheduler* ss, void* value)
{
    int find = stridefindindex(ss, value);
    if (find == -1)
    {
        return -1;
    }

    int usage = (int)ss->pq.data[find].usage;
    ss->pq.data[find].key = ss->pq.data[0].key - 1;

    pqshiftup(&ss->pq, find);
    pqpop(&ss->pq);

    ss->totalusage -= usage;

    if (usage == ss->minusage)
    {
        if (ss->totalusage != 0)
        {
            strideupdateminusage(ss);
        }
    }

    return usage;
}

void strideprint(struct stridescheduler* stride)
{
#ifdef DEBUGFLAG
    cprintf("[%s]\n", ((stride == &masterscheduler) ? "master" : "stride"));
    pqprint(&stride->pq);
    cprintf("\n");
#endif
}

void pqprint(struct priorityqueue* pq)
{
#ifdef DEBUGFLAG
    for (int index = 0; index < pq->size; ++index)
    {
        cprintf("%d(%d, %d) ", (int)(pq->data[index].key * 100), pq->data[index].value, pq->data[index].usage);
    }
    cprintf("\n");
#endif
}

int set_cpu_share(struct proc* p, int usage)
{
    //TODO: p 유효성 검사
    if (p->schedule.sched != SCHEDMLFQ)
    {
        return -1;
    }

    // MLFQ가 최소한 MLFQMINTICKET 만큼의 ticket을 점유해야 한다
    int mlfqidx = stridefindindex(&masterscheduler, (void*)SCHEDMLFQ);
    int mlfqusage = (int)masterscheduler.pq.data[mlfqidx].usage;
    int newmlfqusage = mlfqusage - usage;
    int strideidx = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);

    if (newmlfqusage < MLFQMINTICKET)
    {
        return -2;
    }

    if (stridepush(&mainstride, p, usage) == QFAILURE)
    {
        return -3;
    }

    if (stridechangeusage(&masterscheduler, mlfqidx, newmlfqusage))
    {
        panic("set_cpu_share: strridechangeusage failure");
    }
    if (strideidx == -1)
    {
        if (stridepush(&masterscheduler, (void*)SCHEDSTRIDE, usage) == QFAILURE)
        {   
            panic("set_cpu_share: stridepush failure");
        }
        strideidx = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
        strideprint(&masterscheduler);
    }
    else
    {
        int strideusage = (int)masterscheduler.pq.data[strideidx].usage;
        int newstrideusage = strideusage + usage;
        
        if (newmlfqusage + newstrideusage != masterscheduler.maxticket)
        {
            panic("set_cpu_share: invalid ticket");
        }

        if (stridechangeusage(&masterscheduler, strideidx, newstrideusage))
        {
            panic("set_cpu_share: stridechangeusage failure");
        }
    }
    

    mlfqremove(p);
    p->schedule.sched = SCHEDSTRIDE;
    // if (stridepush(&mainstride, p, usage) == QFAILURE)
    // {
    //     panic("set_cpu_share: stridepush 2 failure");
    // }

    strideprint(&masterscheduler);

    return 0;
}