struct proc;

void lmfqueueinit();
int mlfqueuepush(struct proc*);
struct proc* mlfqueuetop();
int lmfqueuepop();