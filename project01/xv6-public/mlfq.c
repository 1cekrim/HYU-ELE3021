#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "mlfq.h"

struct
{
    struct proc* q[NPROC];
    int rear;
    int front;
} lmfqueue;

void mlfqinit()
{

}

int lmfqueuepush(struct proc* p)
{
    return 0;
}

struct proc* lmfqueuetop()
{
    return 0;
}

int lmfqueuepop()
{
    return 0;
}