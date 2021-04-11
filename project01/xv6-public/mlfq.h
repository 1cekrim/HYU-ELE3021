struct proc;

void mlfqueueinit();
int mlfqueuepush(struct proc*);
struct proc* mlfqueuetop();
int mlfqueuepop();