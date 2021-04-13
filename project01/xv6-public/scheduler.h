typedef unsigned int uint;
struct proc;

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext(struct proc*, uint, uint);
void mlfqboost();
void mlfqremove(struct proc*);

#define PQCAPACITY NPROC

struct pqelement
{
    int key;
    void* value;
};

struct priorityqueue
{
    struct pqelement data[PQCAPACITY];
    int size;
    int capacity;
};

struct stridescheduler
{

};

void pqinit(struct priorityqueue*);
struct pqelement pqtop(struct priorityqueue*);
int pqpush(struct priorityqueue*, int key, void* value);
int pqpop(struct priorityqueue*);
void pqupdatetop(struct priorityqueue*, int key, void* value);