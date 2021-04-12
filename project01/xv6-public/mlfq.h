struct proc;

void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext(struct proc*, uint, uint);
void mlfqboost();
void mlfqremove(struct proc*);
void mlfqprint();