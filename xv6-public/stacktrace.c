#include "stacktrace.h"

extern void cprintf(char*, ...);
extern void panic(char*);
struct tracest globaltracest;

void raisepanic(struct tracest* st, char* message)
{
    cprintf("Traceback:\n");
    for (int i = 0; i < st->size; ++i)
    {
        cprintf("  File \"%s\", line %d, in %s\n", st->data[i].filename, st->data[i].line, st->data[i].signature);
    }
    cprintf("MESSAGE: %s\n", message);
}

struct tracest* pushtracest(char* filename, const char* signature, uint line)
{
    struct tracest* st = &globaltracest;
    if (st->size >= TRACEDEPTH)
    {
        panic("maximum call depth exceeded");
    }

    st->data[st->size++] = (struct stelement){filename, signature, line};
    return st;
}

void poptracest(struct tracest** st)
{
    if ((*st)->size == 0)
    {
        panic("DO NOT USE poptracest");
    }
    --(*st)->size;
}

