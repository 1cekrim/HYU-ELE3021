#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "scheduler.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc* initproc;

// TODO: linked_list를 thread-safe하게 만들어야 함
struct spinlock pgroup_lock;

struct clone_args
{
  enum CLONEMODE mode;
  void* (*start_routine)(void*);
  void* args;
};

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
extern int sys_uptime(void);

static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&pgroup_lock, "pgroup");
  mlfqinit();
}

// Must be called with interrupts disabled
int
cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void)
{
  struct cpu* c;
  struct proc* p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

static int
is_pgroup_master(struct proc* p)
{
  return p->pgroup_master == p;
}

// PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(enum CLONEMODE mode)
{
  struct proc* p;
  char* sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid   = nextpid++;

  p->pgroup_next_execute = p;
  p->pgroup_master = (mode & CLONE_THREAD) ? myproc()->pgroup_master : p;
  p->pgid          = p->pgroup_master->pid;
  linked_list_init(&p->pgroup);

  // p가 pgroup_master일 경우 mlfq에 p 추가
  // 최대 process 개수 == mlfq level 0의 크기
  // 따라서 mlfqpush가 실패하면 logic error
  if (is_pgroup_master(p) && mlfqpush(p))
  {
    panic("allocproc: mlfqpush failure");
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    if (is_pgroup_master(p))
    {
      acquire(&ptable.lock);
      schedremoveproc(p);
      release(&ptable.lock);
    }
    return 0;
  }

  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc* p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc(CLONE_NONE);

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs     = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds     = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es     = p->tf->ds;
  p->tf->ss     = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp    = PGSIZE;
  p->tf->eip    = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc* curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

int
clone(struct clone_args args)
{
  int i, pid;
  struct proc* np;
  struct proc* curproc = myproc();

  enum CLONEMODE mode = args.mode;
  void* (*start_routine)(void*) = args.start_routine;
  void* arg = args.args;


  // Allocate process.
  if ((np = allocproc(mode)) == 0)
  {
    return -1;
  }

  // VM, stack 영역 공유
  if (CLONE_THREAD & mode)
  {
    // pgroup_master
    struct proc* pgmaster = np->pgroup_master;

    // pgroup_master의 pgroup에 np를 추가
    // TODO: lock 범위 수정 or linked_list를 thread-safe하게
    
    acquire(&pgroup_lock);
    linked_list_init(&np->pgroup);
    linked_list_push_back(&np->pgroup, &pgmaster->pgroup);

    // np->pgdir == np->pgdir
    np->pgdir = pgmaster->pgdir;

    // alloc stack
    // TODO: 스레드에서 stack 늘리면 어떻게 됨??
    // TODO: 스레드가 exit 된 다음에 해당 영역 재사용 필요
    np->sz = allocuvm(pgmaster->pgdir, pgmaster->sz, pgmaster->sz + 2 * PGSIZE);
    if (!np->sz)
    {
      // ROLLBACK
      kfree(np->kstack);
      np->kstack = 0;
      np->state  = UNUSED;
      linked_list_remove(&np->pgroup);
      release(&pgroup_lock);
      return -1;
    }

    // | ---------- |  <- np->sz
    // |  new stack |
    // | ---------- |  <- np->sz - 1 * PGSIZE
    // |  guard     |
    // | ---------- |  <- np->sz - 2 * PGSIZE
    // |  stack     |
    // | ---------- |
    clearpteu(pgmaster->pgdir, (char*)(np->sz - 2 * PGSIZE));

    // stack 영역을 공유함
    pgmaster->sz = np->sz;

    // parent 같음
    np->parent = pgmaster->parent;

    // trapframe 복사
    // TODO: 이거 curproc 복사하는게 맞나? pgmaster꺼 복사하면 문제 생길 거 같긴 함
    *np->tf = *curproc->tf;

    // esp -> new stack으로 / eip -> start_routine으로
    np->tf->esp = np->sz;
    np->tf->eip = (uint)start_routine;

    // start_routine(arg);
    // return 0xdeadbeef
    // TODO: 과제에서는 thread가 항상 exit를 호출하지만, 실제로는 그러지 않을 수 있으므로 예외처리 필요
    np->tf->esp -= 4;
    *((uint*)np->tf->esp) = (uint)arg;
    np->tf->esp -= 4;
    *((uint*)np->tf->esp) = 0xdeafbeef;
    release(&pgroup_lock);

  }
  else
  {
    // init는 thread-safe함
    linked_list_init(&np->pgroup);

    // Copy process state from proc.
    if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
    {
      kfree(np->kstack);
      np->kstack = 0;
      np->state  = UNUSED;
      acquire(&ptable.lock);
      schedremoveproc(np);
      release(&ptable.lock);
      return -1;
    }

    np->sz     = curproc->sz;
    np->parent = curproc;
    *np->tf    = *curproc->tf;
  }
  // Clear %eax so that fork returns 0 in the child.

  np->tf->eax = 0;
  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  struct clone_args args = {
    .mode = CLONE_NONE,
    .args = 0,
    .start_routine = 0
  };
  return clone(args);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc* curproc = myproc();
  struct proc* p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc* p;
  int havekids, pid;
  struct proc* curproc = myproc();
  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;

      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid     = 0;
        p->parent  = 0;
        p->name[0] = 0;
        p->killed  = 0;
        p->state   = UNUSED;
        schedremoveproc(p);
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

int ps(void);

// pgroup 멤버들 사이의 swtch (light)
void
swtch_pgroup(struct proc* old_lwp, struct proc* new_lwp)
{
  if (new_lwp->state != RUNNABLE)
  {
    panic("invalid new_lwp");
  }
  if (new_lwp == 0)
  {
    panic("swtch_pgroup: no process");
  }

  if (new_lwp->kstack == 0)
  {
    panic("swtch_pgroup: no kstack");
  }

  new_lwp->state = RUNNING;

  // kstack만 전환하면 됨
  pushcli();
  mycpu()->ts.esp0 = (uint)new_lwp->kstack + KSTACKSIZE;
  mycpu()->proc = new_lwp;
  popcli();

  int intena = mycpu()->intena;
  swtch(&(old_lwp->context), new_lwp->context);
  mycpu()->intena = intena;
}

// pgroup 멤버들 대상으로 rr
struct proc*
pgroup_scheduler(struct proc* pgmaster)
{
  struct proc* selected = myproc();
  struct proc* new_selected =
      container_of(selected->pgroup.next, struct proc, pgroup);

  while (selected != new_selected)
  {
    if (new_selected->state == RUNNABLE)
    {
      return new_selected;
    }
    new_selected = container_of(new_selected->pgroup.next, struct proc, pgroup);
  }

  return new_selected->state == RUNNABLE ? new_selected : 0;
}

// timer interrupt시 호출됨
void
pgroup_itq_timer(void)
{
  struct proc* curproc = myproc();
  struct proc* pgmaster = curproc->pgroup_master;
  // single thread이거나, time quantum을 넘어 scheduling이 필요할 경우
  if (linked_list_is_empty(&pgmaster->pgroup) || isexhaustedprocess(pgmaster))
  {
    yield();
    return;
  }

  acquire(&ptable.lock);
  
  // myproc을 RUNNABLE 하게 변경
  curproc->state = RUNNABLE;

  // round robin으로 다음 타겟을 선정
  struct proc* target = pgroup_scheduler(pgmaster);

  // pgroup에 runnable한 프로세스가 없을 경우 (발생하지 않음)
  if (!target)
  {
    sched();
    release(&ptable.lock);
    return;
  }

  pgmaster->pgroup_next_execute = target;
  swtch_pgroup(curproc, target);
  release(&ptable.lock);
}

// PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
extern struct stridescheduler mainstride;
extern struct stridescheduler masterscheduler;
void
scheduler(void)
{
  struct proc* p = 0;
  struct cpu* c  = mycpu();
  c->proc        = 0;
  int expired    = 1;
  int schedidx   = 0;

  strideinit(&masterscheduler, 100);
  stridepush(&masterscheduler, (void*)SCHEDMLFQ, 100);

  strideinit(&mainstride, 80);

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    schedidx = (int)stridetop(&masterscheduler);

    // expired가 true일 때 스케줄러에서 p을 받아옴
    if (expired || p->state != RUNNABLE || p->schedule.sched != schedidx)
    {
      // update schedidx
      switch (schedidx)
      {
      case SCHEDMLFQ: // mlfq
        p = mlfqtop();
        if (p)
        {
          break;
        }
      case SCHEDSTRIDE: // stride
        p = stridetop(&mainstride);
        if (p)
        {
          schedidx = 1;
          break;
        }
      default:
        break;
        // no process
      }
    }
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    if (p)
    {
      uint start = 0;
      uint end   = 0;

      if (p->state == RUNNABLE)
      {
        struct proc* rp = p->pgroup_next_execute;
        c->proc         = rp;
        switchuvm(rp);
        rp->state                     = RUNNING;
        start                         = sys_uptime();
        p->schedule.lastscheduledtick = start;
        swtch(&(c->scheduler), rp->context);
        end = sys_uptime();
        switchkvm();
      }

      switch (p->schedule.sched)
      {
      case SCHEDMLFQ:
        expired = mlfqnext(p, start, end);
        break;

      case SCHEDSTRIDE:
        expired = 1;
        break;

      default:
        panic("scheduler: invalid schedidx");
      }
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc* p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state          = RUNNABLE;
  myproc()->schedule.yield = 1;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void* chan, struct spinlock* lk)
{
  struct proc* p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan  = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void* chan)
{
  struct proc* p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void* chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc* p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char* states[] = {
    [UNUSED] "unused",   [EMBRYO] "embryo",  [SLEEPING] "sleep ",
    [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE] "zombie"
  };
  int i;
  struct proc* p;
  char* state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint*)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
thread_create(thread_t* thread, void* (*start_routine)(void*), void* arg)
{
  struct clone_args args = {
    .mode = CLONE_THREAD,
    .args = arg,
    .start_routine = start_routine
  };

  int lwpid = clone(args);
  if (lwpid)
  {
    return lwpid;
  }
  // child (LWP)
  // 아래처럼 동작시키려면 clone에서 thread_create를 호출한 context의 스택을 복사해야 함
  // 그런데 구현에 실패해서 그냥 start_routine과 arg를 넘기고 스택에 직접 적도록 구현...
  panic("thread_create");
  void* ret = start_routine(arg);
  thread_exit(ret);

  return -1;
}

void
thread_exit(void* retval)
{
}

int
thread_join(thread_t thread, void** retval)
{
  return 0;
}

extern void cprintf(char* fmt, ...);

int
ps()
{
  static char* text[] = {
    [UNUSED] "UNUSED  ",   [EMBRYO] "EMBRYO  ",  [SLEEPING] "SLEEPING",
    [RUNNABLE] "RUNNABLE", [RUNNING] "RUNNING ", [ZOMBIE] "ZOMBIE  "
  };

  struct proc* p;
  cprintf("PID\tLWPID\tSTATE\t\tNAME\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; ++p)
  {
    if (p->state != UNUSED)
    {
      cprintf("%d\t%d\t%s\t%s\n", p->pgid, p->pid, text[p->state], p->name);
    }
  }

  return 0;
}