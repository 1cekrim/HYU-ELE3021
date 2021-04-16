#include "spinlock.h"

typedef unsigned int uint;
struct proc;

#define PQCAPACITY NPROC
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

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext(struct proc*, uint, uint);
void mlfqboost();
void mlfqremove(struct proc*);
void schedremoveproc(struct proc* p);

void pqinit(struct priorityqueue*);
struct pqelement pqtop(struct priorityqueue*);
int pqpush(struct priorityqueue*, struct pqelement);
int pqpop(struct priorityqueue*);
void pqupdatetop(struct priorityqueue*, struct pqelement);

void strideinit(struct stridescheduler*, int);
int stridepush(struct stridescheduler*, void*, int);
void* stridetop(struct stridescheduler*);
int stridenext(struct stridescheduler*);
int strideremove(struct stridescheduler*, void*);

int set_cpu_share(struct proc*, int);
void schedremoveproc(struct proc*);

void mlfqprint();
void strideprint(struct stridescheduler* stride);