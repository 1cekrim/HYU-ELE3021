struct proc;

void lmfqueueinit();
int lmfqueuepush(struct proc*);
struct proc* lmfqueuetop();
int lmfqueuepop();