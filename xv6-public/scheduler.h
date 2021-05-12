#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include "spinlock.h"

typedef unsigned int uint;
struct proc;

#define PQCAPACITY      NPROC
#define STRIDEMAXTICKET 100

struct pqelement
{
  double key;
  void* value;
  int usage;
};

struct priorityqueue
{
  struct pqelement data[PQCAPACITY];
  int size;
  int capacity;
};

struct stridescheduler
{
  struct priorityqueue pq;
  int totalusage;
  int maxticket;
  int minusage;
  double stride[STRIDEMAXTICKET + 1];
  struct spinlock lock;
};

int isexhaustedprocess(struct proc*);

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext(struct proc*, uint, uint);
void mlfqboost();

void strideinit(struct stridescheduler*, int);
int stridepush(struct stridescheduler*, void*, int);
void* stridetop(struct stridescheduler*);

void schedremoveproc(struct proc*);
int set_cpu_share(struct proc*, int);

void mlfqprint();
void strideprint(struct stridescheduler* stride);

#endif