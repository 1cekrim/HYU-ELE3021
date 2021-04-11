struct proc;

void mlfqueueinit(int);
int mlfqueuepush(int, struct proc*);
struct proc* mlfqueuetop(int);
int mlfqueuepop(int);