#define QFAILURE 1
#define QSUCCESS 0
#define MSIZE    NPROC + 1
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

int
pqparent(int index)
{
  return (index - 1) / 2;
}

int
pqleftchild(int index)
{
  return 2 * index + 1;
}

int
pqrightchild(int index)
{
  return 2 * index + 2;
}

void
pqinit(struct priorityqueue* pq)
{
  memset(pq->data, 0, sizeof(pq->data));
  pq->size     = 0;
  pq->capacity = PQCAPACITY;
}

struct pqelement
pqtop(struct priorityqueue* pq)
{
  return pq->data[0];
}

void
pqshiftup(struct priorityqueue* pq, int index)
{
  int parent = pqparent(index);
  while (index > 0 && pq->data[parent].key > pq->data[index].key)
  {
    struct pqelement tmp = pq->data[parent];
    pq->data[parent]     = pq->data[index];
    pq->data[index]      = tmp;
    index                = parent;
    parent               = pqparent(index);
  }
}

void
pqshiftdown(struct priorityqueue* pq, int index)
{
  int minidx = index;
  int left   = pqleftchild(index);
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
    pq->data[minidx]     = pq->data[index];
    pq->data[index]      = tmp;
    pqshiftdown(pq, minidx);
  }
}

int
pqpush(struct priorityqueue* pq, struct pqelement element)
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

void
pqupdatetop(struct priorityqueue* pq, struct pqelement element)
{
  pq->data[0] = element;
  pqshiftdown(pq, 0);
}

int
pqpop(struct priorityqueue* pq)
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

void
mlfqprint()
{
  for (int level = 0; level < NLEVEL; ++level)
  {
    cprintf("level = %d / %d, %d, %d", level, mlfq.q[level].rear,
            mlfq.q[level].front, mlfq.q[level].qsize);
    for (int j = 1; j <= mlfq.q[level].qsize; ++j)
    {
      cprintf(" %p(%d)",
              mlfq.q[level]
                  .q[(mlfq.q[level].front + j) % mlfq.q[level].capacity]
                  ->pid,
              mlfq.q[level]
                  .q[(mlfq.q[level].front + j) % mlfq.q[level].capacity]
                  ->schedule.level);
    }
    cprintf("\n");
  }
  cprintf("\n");
}

void
mlfqinit()
{
  static int quantum[NLEVEL]       = { 1, 2, 4 };
  static int allotment[NLEVEL - 1] = { 5, 10 };
  static int boostingperiod        = 100;

  memset(&mlfq, 0, sizeof(mlfq));
  memmove(mlfq.quantum, quantum, NLEVEL);
  memmove(mlfq.allotment, allotment, NLEVEL - 1);
  mlfq.boostingperiod = boostingperiod;

  for (int level = 0; level < NLEVEL; ++level)
  {
    mlfq.q[level].front    = -1;
    mlfq.q[level].rear     = -1;
    mlfq.q[level].capacity = MSIZE;
  }
}

int
mlfqisfull(int level)
{
  return mlfq.q[level].qsize == (mlfq.q[level].capacity - 1);
}

int
mlfqisempty(int level)
{
  return !mlfq.q[level].qsize;
}

void
mlfqremove(struct proc* p)
{
  int flag  = 1;
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
    mlfqprint();
    panic("mlfqremove: no proc");
  }
}

void
schedremoveproc(struct proc* p)
{
  switch (p->schedule.sched)
  {
  case SCHEDMLFQ:
    mlfqremove(p);
    break;
  case SCHEDSTRIDE:
  {
#ifdef DEBUGFLAG
    cprintf("\n[schedremoveproc]\n");
    cprintf("p: %p\n", p);
    strideprint(&mainstride);
#endif
    int usage = strideremove(&mainstride, p);
    if (usage == -1)
    {
      panic("schedremoveproc: usage == -1");
    }

    int mlfqidx      = stridefindindex(&masterscheduler, (void*)SCHEDMLFQ);
    int mlfqusage    = (int)masterscheduler.pq.data[mlfqidx].usage;
    int newmlfqusage = mlfqusage + usage;

    int strideidx      = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
    int strideusage    = (int)masterscheduler.pq.data[strideidx].usage;
    int newstrideusage = strideusage - usage;
#ifdef DEBUGFLAG
    cprintf("removed usage: %d\n", usage);
    cprintf("mlfq usage: %d -> %d\n", mlfqusage, newmlfqusage);
    cprintf("stride usage: %d -> %d\n", strideusage, newstrideusage);
    cprintf("result");
    strideprint(&mainstride);
#endif

    if (newmlfqusage + newstrideusage != masterscheduler.maxticket ||
        newmlfqusage < 20 || newstrideusage < 0)
    {
      panic("schedremoveproc: invalid usage rate");
    }

    if (newstrideusage)
    {
      if (stridechangeusage(&masterscheduler, strideidx, newstrideusage))
      {
        panic("schedremoveproc: strideidx change failure");
      }
      if (stridechangeusage(&masterscheduler, mlfqidx, newmlfqusage))
      {
        panic("schedremoveproc: mlfqidx change failure");
      }
    }
    else
    {
      if (strideremove(&masterscheduler,
                       masterscheduler.pq.data[strideidx].value) == -1)
      {
        panic("schedremoveproc: strideremove stridescheduler failure");
      }
      stridechangeusage(&masterscheduler, 0, 100);
    }

    strideupdateminusage(&masterscheduler);
    break;
  }
  default:
    panic("schedremoveproc: invalid sched");
  }
}

void
mlfqboost()
{
  struct proc* p = 0;
  for (int level = 1; level < NLEVEL; ++level)
  {
    int capacity = mlfq.q[level].capacity;
    int front    = mlfq.q[level].front;
    for (int i = 0; i < mlfq.q[level].qsize; ++i)
    {
      front = (front + 1) % capacity;
      p     = mlfq.q[level].q[front];
      mlfqenqueue(0, p);
    }
    mlfq.q[level].front = -1;
    mlfq.q[level].rear  = -1;
    mlfq.q[level].qsize = 0;
  }
}

int
mlfqpush(struct proc* p)
{
  memset(&p->schedule, 0, sizeof(p->schedule));
  p->schedule.sched = SCHEDMLFQ;
  if (mlfqenqueue(0, p) == QFAILURE)
  {
    return QFAILURE;
  }
  return QSUCCESS;
}

extern void procdump(void);

int
mlfqrotatetotarget(int level, struct proc* p)
{
  for (int i = 0; i < mlfq.q[level].qsize; ++i)
  {
    struct proc* top = mlfqueuetop(level);
    if (top == p)
    {
      return QSUCCESS;
    }
    if (mlfqdequeue(level) == QFAILURE)
    {
      cprintf("p: %p\n", top);
      mlfqprint();
      panic("mlfqrotate: mlfqdequeue failure");
    }
    if (mlfqenqueue(level, top) == QFAILURE)
    {
      cprintf("p: %p\n", top);
      mlfqprint();
      panic("mlfqrotate: mlfqenqueue failure");
    }
  }
  return QFAILURE;
}

int
mlfqnext(struct proc* p, uint start, uint end)
{
  static uint nextboostingtick;
  if (p->killed || p->state == ZOMBIE)
  {
    return 1;
  }

  int executiontick = end - start + ((p->schedule.yield) ? 1 : 0);
  p->schedule.executionticks += executiontick;

  int level           = p->schedule.level;
  uint executionticks = p->schedule.executionticks;

  if (level + 1 < NLEVEL && executionticks >= mlfq.allotment[level])
  {
    if (mlfqrotatetotarget(level, p) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext1: rotate failure");
    }
    if (mlfqdequeue(level) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext1: mlfqpop failure");
    }
    if (mlfqenqueue(level + 1, p) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext1: mlfqpush failure");
    }
    return 1;
  }

  int result = (executiontick >= mlfq.quantum[level]) || p->schedule.yield ||
               p->state == SLEEPING;
  if (result)
  {
    if (mlfqrotatetotarget(level, p) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext2: rotate failure");
    }
    if (mlfqdequeue(level) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext2: mlfqpop failure");
    }
    if (mlfqenqueue(level, p) == QFAILURE)
    {
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("mlfqnext2: mlfqpush failure");
    }
  }

  if (nextboostingtick <= end)
  {
    mlfqboost();
    nextboostingtick = end + mlfq.boostingperiod;
  }

  return result;
}

struct proc*
mlfqtop()
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

int
mlfqenqueue(int level, struct proc* p)
{
#ifdef DEBUGFLAG
  if (mlfq.q[level].qsize < 0 ||
      mlfq.q[level].qsize == mlfq.q[level].capacity ||
      mlfq.q[level].rear < -2 || mlfq.q[level].rear >= 65 ||
      mlfq.q[level].front < -2 || mlfq.q[level].front >= 65)
  {
    panic("mlfdqenqueue: outbound");
  }
  for (int i = 0; i < NLEVEL; ++i)
  {
    int cnt = 0;
    while (cnt < mlfq.q[i].qsize)
    {
      if (mlfq.q[i].q[(mlfq.q[i].front + 1 + cnt) % mlfq.q[i].capacity] == p)
      {
        cprintf("error: %p\n", p);
        mlfqprint();
        return QFAILURE;
      }
      ++cnt;
    }
  }
#endif
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

struct proc*
mlfqueuetop(int level)
{
  return mlfqisempty(level)
             ? 0
             : mlfq.q[level]
                   .q[(mlfq.q[level].front + 1) % mlfq.q[level].capacity];
}

int
mlfqdequeue(int level)
{
  if (mlfq.q[level].qsize < 0 ||
      mlfq.q[level].qsize == mlfq.q[level].capacity ||
      mlfq.q[level].rear < -2 || mlfq.q[level].rear >= 65 ||
      mlfq.q[level].front < -2 || mlfq.q[level].front >= 65)
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

int
mlfqueuesize(int level)
{
  return mlfq.q[level].qsize;
}

void
strideinit(struct stridescheduler* ss, int maxticket)
{
  pqinit(&ss->pq);
  initlock(&ss->lock, "stridescheduler");
  ss->totalusage = 0;
  ss->maxticket  = maxticket;

  ss->stride[0] = 0;
  ss->minusage  = 100;
  for (int i = 1; i <= maxticket; ++i)
  {
    ss->stride[i] = (double)1 / i;
  }
}

int
stridepush(struct stridescheduler* ss, void* value, int usage)
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

  int min = ss->pq.size ? pqtop(&ss->pq).key : 0;

  struct pqelement element;
  element.key   = min + ss->stride[ss->minusage];
  element.value = value;
  element.usage = usage;

  pqpush(&ss->pq, element);

  return 0;
}

void*
stridetop(struct stridescheduler* ss)
{
  if (ss == &masterscheduler)
  {
    struct pqelement result = pqtop(&ss->pq);
    return result.value;
  }
  else
  {
    // 임시 코드
    if (!ss->pq.size)
    {
      return 0;
    }

    int minidx      = -1;
    double minvalue = ss->pq.data[ss->pq.size - 1].key;

    for (int i = 0; i < ss->pq.size; ++i)
    {
      if (ss->pq.data[i].key <= minvalue &&
          ((struct proc*)ss->pq.data[i].value)->state == RUNNABLE)
      {
        minvalue = ss->pq.data[i].key;
        minidx   = i;
      }
    }
    if (minidx == -1)
    {
      return 0;
    }

    struct proc* result = ss->pq.data[minidx].value;
    ss->pq.data[minidx].key += ss->stride[(int)ss->pq.data[minidx].usage];
    pqshiftdown(&ss->pq, minidx);

    return result;
  }
}

int
stridenext(struct stridescheduler* ss)
{
  if (ss == &masterscheduler)
  {
    struct pqelement result = pqtop(&ss->pq);
    result.key += ss->stride[(int)result.usage];
    pqupdatetop(&ss->pq, result);
    return 1;
  }
  return 1;
}

// failure: return -1
int
stridefindindex(struct stridescheduler* ss, void* value)
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

void
strideupdateminusage(struct stridescheduler* ss)
{
  int minusage = ss->maxticket + 1;
  for (int index = 0; index < ss->pq.size; ++index)
  {
    minusage = (minusage > (int)ss->pq.data[index].usage)
                   ? (int)ss->pq.data[index].usage
                   : minusage;
  }
  ss->minusage = minusage;
}

void
strideremoveusagezero(struct stridescheduler* ss)
{
  int flag = 0;
  do
  {
    flag = 0;
    for (int index = 0; index < ss->pq.size; ++index)
    {
      if (ss->pq.data[index].usage == 0)
      {
        if (strideremove(ss, ss->pq.data[index].value) == -1)
        {
          panic("strideremoveusagezero: == -1");
        }
        flag = 1;
        break;
      }
    }
  } while (flag);
}

int
stridechangeusage(struct stridescheduler* ss, int index, int usage)
{
  int oldusage = (int)ss->pq.data[index].usage;
  int newtotal = ss->totalusage - oldusage + usage;
  if (usage <= 0 || newtotal <= 0 || newtotal > ss->maxticket)
  {
    return -1;
  }

  ss->pq.data[index].usage = usage;
  ss->totalusage           = newtotal;

  if (oldusage == ss->minusage)
  {
    strideupdateminusage(ss);
  }

  return 0;
}

int
strideremove(struct stridescheduler* ss, void* value)
{
  int find = stridefindindex(ss, value);
  if (find == -1)
  {
    return -1;
  }

#ifdef DEBUGFLAG
  cprintf("strideremove find: %d\n", find);
#endif

  int usage             = (int)ss->pq.data[find].usage;
  ss->pq.data[find].key = -1;

#ifdef DEBUGFLAG
  strideprint(ss);
#endif

  pqshiftup(&ss->pq, find);
  pqpop(&ss->pq);

#ifdef DEBUGFLAG
  strideprint(ss);
#endif

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

void
strideprint(struct stridescheduler* stride)
{
  cprintf("[%s]\n", ((stride == &masterscheduler) ? "master" : "stride"));
  cprintf("maxticket: %d\nminusage: %d\ntotalusage: %d\n", stride->maxticket,
          stride->minusage, stride->totalusage);
  pqprint(&stride->pq);
  cprintf("}\n");
}

void
pqprint(struct priorityqueue* pq)
{
  cprintf("size: %d\n", pq->size);
  for (int index = 0; index < pq->size; ++index)
  {
    cprintf("%d(%p, %d) ", (int)(pq->data[index].key * 100),
            pq->data[index].value, pq->data[index].usage);
  }
  cprintf("\n");
}

int
set_cpu_share(struct proc* p, int usage)
{
  if (p->schedule.sched != SCHEDMLFQ)
  {
    return -1;
  }

  acquire(&masterscheduler.lock);
  // TODO: p 유효성 검사

  // MLFQ가 최소한 MLFQMINTICKET 만큼의 ticket을 점유해야 한다
  int mlfqidx      = stridefindindex(&masterscheduler, (void*)SCHEDMLFQ);
  int mlfqusage    = (int)masterscheduler.pq.data[mlfqidx].usage;
  int newmlfqusage = mlfqusage - usage;

  if (newmlfqusage < MLFQMINTICKET)
  {
    return -2;
  }

  if (stridepush(&mainstride, p, usage) == QFAILURE)
  {
    return -3;
  }

  // newmlfqusage는 0일 수 없음. 따라서 따로 처리 x
  if (stridechangeusage(&masterscheduler, mlfqidx, newmlfqusage))
  {
    panic("set_cpu_share: strridechangeusage failure");
  }

  int strideidx = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
  if (strideidx == -1)
  {
    if (stridepush(&masterscheduler, (void*)SCHEDSTRIDE, usage) == QFAILURE)
    {
      cprintf("[failure3]\nusage: %d\n", usage);
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("set_cpu_share: stridepush failure");
    }
    strideidx = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
  }
  else
  {
    int strideusage    = (int)masterscheduler.pq.data[strideidx].usage;
    int newstrideusage = strideusage + usage;

    if (newmlfqusage + newstrideusage != masterscheduler.maxticket)
    {
      cprintf("[failure1]\nnewmlfqusage: %d\nnewstrideusage: %d\nstrideusage: "
              "%d, mlfqusage: %d, mlfqindex: %d\nstrideindex: %d\nusage: %d\n",
              newmlfqusage, newstrideusage, strideusage, mlfqusage, mlfqidx,
              strideidx, usage);
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("set_cpu_share: invalid ticket");
    }

    if (stridechangeusage(&masterscheduler, strideidx, newstrideusage))
    {
      cprintf("[failure2]\nnewstrideusage: %d\n", newstrideusage);
      strideprint(&masterscheduler);
      strideprint(&mainstride);
      panic("set_cpu_share: stridechangeusage failure");
    }
  }

  mlfqremove(p);
  p->schedule.sched = SCHEDSTRIDE;
  release(&masterscheduler.lock);
  return 0;
}