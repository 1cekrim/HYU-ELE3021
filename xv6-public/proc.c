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

struct clone_args
{
  enum CLONEMODE mode;
  void* (*start_routine)(void*);
  void* args;
  struct spinlock* lock;
};

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
extern int sys_uptime(void);

static void wakeup1(void* chan);
int ps(void);
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  mlfqinit();
}

void
dump_pgroup(struct proc* head)
{
  struct proc* p = head->pgroup_master;
  cprintf("%d", p->pid);
  for (struct linked_list* pos = p->pgroup.next; pos != &head->pgroup;
       pos                     = pos->next)
  {
    p = container_of(pos, struct proc, pgroup);
    cprintf("->%d", p->pid);
  }
  cprintf("\n");
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

static inline int
is_pgroup_master(struct proc* p)
{
  return p->pgroup_master == p;
}

static inline int
is_pgroup_have_pid(struct proc* node, int pid)
{
  if (node->pid == pid)
  {
    return 1;
  }

  for (struct linked_list* pos = node->pgroup.next; pos != &node->pgroup;
       pos                     = pos->next)
  {
    struct proc* p = container_of(pos, struct proc, pgroup);
    if (p->pid == pid)
    {
      return 1;
    }
  }

  return 0;
}

// PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
#define ACQUIRE \
  if (lock)     \
  acquire(lock)
#define RELEASE \
  if (lock)     \
  release(lock)
static struct proc*
allocproc(enum CLONEMODE mode, struct spinlock* lock)
{
  struct proc* p;
  char* sp;

  ACQUIRE;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  RELEASE;
  return 0;

found:
  p->state = EMBRYO;
  p->pid   = nextpid++;

  p->pgroup_current_execute = (mode & CLONE_THREAD) ? (struct proc*)0xdeadbbbb : p;
  p->pgroup_master = (mode & CLONE_THREAD) ? myproc()->pgroup_master : p;
  p->pgid          = p->pgroup_master->pid;
  linked_list_init(&p->pgroup);
  linked_list_init(&p->stackbin);
  if (mode & CLONE_THREAD)
  {
    linked_list_push_back(&p->pgroup, &p->pgroup_master->pgroup);
  } 

  // p가 pgroup_master일 경우 mlfq에 p 추가
  // 최대 process 개수 == mlfq level 0의 크기
  // 따라서 mlfqpush가 실패하면 logic error
  if (is_pgroup_master(p) && mlfqpush(p))
  {
    panic("allocproc: mlfqpush failure");
  }

  RELEASE;

  initlock(&p->pgroup_lock, "pgroup_lock");


  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    if (is_pgroup_master(p))
    {
      ACQUIRE;
      schedremoveproc(p);
      RELEASE;
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

  p = allocproc(CLONE_NONE, &ptable.lock);

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
  struct proc* pgmaster = myproc()->pgroup_master;
  acquire(&pgmaster->pgroup_lock);
  sz = pgmaster->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(pgmaster->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(pgmaster->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  pgmaster->sz = sz;

  release(&pgmaster->pgroup_lock);
  return 0;
}

int
clone(struct clone_args args)
{
  int i, pid;
  struct proc* np;
  struct proc* curproc = myproc();
  // TrapFrame을 제외한 모든 값을 pgmaster로부터 상속해야한다!
  struct proc* pgmaster = curproc->pgroup_master;

  enum CLONEMODE mode           = args.mode;
  void* (*start_routine)(void*) = args.start_routine;
  void* arg                     = args.args;
  struct spinlock* lock         = args.lock;

  // Allocate process.
  if ((np = allocproc(mode, args.lock)) == 0)
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

    ACQUIRE;

    // page directory를 공유함
    // type(pgdir): struct pde_t*
    // 단순 값 복사로도 같은 객체를 공유하게 할 수 있음
    np->pgdir = pgmaster->pgdir;

    // alloc stack
    np->sz =
        allocpageuvm(pgmaster->pgdir, &pgmaster->stackbin, pgmaster->sz, 2);
    if (!np->sz)
    {
      // ROLLBACK
      kfree(np->kstack);
      np->kstack = 0;
      np->state  = UNUSED;
      linked_list_remove(&np->pgroup);
      RELEASE;
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
    pgmaster->sz = np->sz > pgmaster->sz ? np->sz : pgmaster->sz;

    // parent 같음
    np->parent = pgmaster->parent;

    // trapframe 복사
    *np->tf = *curproc->tf;

    // esp -> new stack으로 / eip -> start_routine으로
    np->tf->esp = np->sz;
    np->tf->eip = (uint)start_routine;

    // start_routine(arg);
    // return 0xdeadbeef
    // TODO: 과제에서는 thread가 항상 exit를 호출하지만, 실제로는 그러지 않을 수
    // 있으므로 예외처리 필요
    np->tf->esp -= sizeof(void*);
    *((uint*)np->tf->esp) = (uint)arg;
    np->tf->esp -= sizeof(void*);
    *((uint*)np->tf->esp) = 0xdeadbeef;
    RELEASE;

    for (i = 0; i < NOFILE; i++)
      if (pgmaster->ofile[i])
        np->ofile[i] = pgmaster->ofile[i];
    np->cwd = pgmaster->cwd;
  }
  else
  {
    // Copy process state from proc.
    if ((np->pgdir = copyuvm(pgmaster->pgdir, pgmaster->sz)) == 0)
    {
      kfree(np->kstack);
      np->kstack = 0;
      np->state  = UNUSED;
      ACQUIRE;
      schedremoveproc(np);
      RELEASE;
      return -1;
    }

    np->sz     = pgmaster->sz;
    np->parent = pgmaster;
    *np->tf    = *curproc->tf;

    for (i = 0; i < NOFILE; i++)
      if (pgmaster->ofile[i])
        np->ofile[i] = filedup(pgmaster->ofile[i]);
    np->cwd = idup(pgmaster->cwd);
  }
  // Clear %eax so that fork returns 0 in the child.

  np->tf->eax = 0;

  safestrcpy(np->name, pgmaster->name, sizeof(pgmaster->name));

  pid = np->pid;
  ACQUIRE;

  np->state = RUNNABLE;

  RELEASE;

  return pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  struct clone_args args = { .mode          = CLONE_NONE,
                              .args          = 0,
                              .start_routine = 0,
                              .lock          = &ptable.lock };
  return clone(args);
}

void
clear_threads_exec(void)
{
  struct proc* curproc = myproc();

  // 자기자신을 제외한 모든 쓰레드를 exit
  for (struct linked_list *pos = curproc->pgroup.next, *next = pos->next;
      pos != &curproc->pgroup; pos = next, next = pos->next)
  {
    struct proc* p = container_of(pos, struct proc, pgroup);
      for (int fd = 0; fd < NOFILE; fd++)
      {
        p->ofile[fd] = 0;
      }
    p->cwd = 0;
  }

  free_threads(curproc);
  curproc->pgroup_master = curproc;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc* curproc = myproc();
  struct proc* pgmaster = curproc->pgroup_master;
  if (!is_pgroup_master(curproc))
  {
    acquire(&ptable.lock);
    set_killed(pgmaster, 1);
    pgmaster->state = RUNNABLE;
    curproc->chan  = (void*)-1;
    curproc->state = SLEEPING;
    pgroup_sched();
    panic("exit pgroup sched");
  }
  // file stream: pgroup 내에서만 공유되는 자원
  // 따라서 ptable lock이 아닌 pgroup_lock으로도 충분하다
  struct proc* p;
  int fd;

  if (pgmaster == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (pgmaster->ofile[fd])
    {
      fileclose(pgmaster->ofile[fd]);
      pgmaster->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(pgmaster->cwd);
  end_op();
  pgmaster->cwd = 0;

  // release - acquire 사이에 다른 스레드가 실행되는 것을 방지
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(pgmaster->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == pgmaster)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // TODO: abandoned thread 들도 init로 넘겨야 함

  // Jump into the scheduler, never to return.
  pgmaster->state = ZOMBIE;

  // pgroup 전체를 ZOMBIE로 만듦 (exit된 thread가 동작하는 것을 방지)
  for (struct linked_list* pos       = pgmaster->pgroup.next;
       pos != &pgmaster->pgroup; pos = pos->next)
  {
    container_of(pos, struct proc, pgroup)->state = ZOMBIE;
  }

  sched();
  panic("zombie exit");
}

void
free_threads(struct proc* p)
{
  for (struct linked_list *pos = p->pgroup.next, *next = pos->next;
       pos != &p->pgroup; pos = next, next = pos->next)
  {
    struct proc* t = container_of(pos, struct proc, pgroup);
    kfree(t->kstack);
    t->kstack  = 0;
    t->pid     = 0;
    t->pgid    = 0;
    t->parent  = 0;
    t->name[0] = 0;
    set_killed(t, 0);
    t->state = UNUSED;

    linked_list_remove(pos);
    linked_list_init(&t->pgroup);
    t->pgroup_master          = 0;
    t->pgroup_current_execute = 0;
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc* p;
  int havekids, pid;
  struct proc* pgmaster = myproc()->pgroup_master;
  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != pgmaster)
        continue;
      havekids = 1;

      // 오직 pgroup master 만이 exit-wait의 대상이 될 수 있다
      if (p->state == ZOMBIE && is_pgroup_master(p))
      {
        // Found one.
        pid = p->pid;

        // free kstack
        kfree(p->kstack);
        p->kstack = 0;

        // free vm
        freevm(p->pgdir);

        // free threads
        free_threads(p);

        p->pid     = 0;
        p->parent  = 0;
        p->name[0] = 0;
        set_killed(p, 0);
        p->state = UNUSED;
        schedremoveproc(p);
        p->pgroup_master          = 0;
        p->pgroup_current_execute = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || is_killed(pgmaster))
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(pgmaster, &ptable.lock); // DOC: wait-sleep
  }
}

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
  mycpu()->proc    = new_lwp;
  popcli();

  int intena = mycpu()->intena;
  swtch(&(old_lwp->context), new_lwp->context);
  mycpu()->intena = intena;
}

// timer interrupt시 호출됨
void
pgroup_irq_trap(void)
{
  acquire(&ptable.lock);
  myproc()->state = RUNNABLE;
  pgroup_sched();
  release(&ptable.lock);
}

void
pgroup_sched(void)
{
  struct proc* curproc  = myproc();
  struct proc* pgmaster = curproc->pgroup_master;
  // dump_pgroup(curproc);
  // single thread이거나, time quantum을 넘어 scheduling이 필요할 경우
  if (linked_list_is_empty(&pgmaster->pgroup) || isexhaustedprocess(pgmaster))
  {
    pgmaster->schedule.yield = 1;
    sched();
    return;
  }

  // round robin으로 다음 타겟을 선정
  struct proc* target =
      get_runnable(container_of(curproc->pgroup.next, struct proc, pgroup));

  // pgroup에 runnable한 프로세스가 없을 경우
  if (!target)
  {
    pgmaster->schedule.yield = 1;
    sched();
    return;
  }

  if (target->state != RUNNABLE)
  {
    panic("no...");
  }

  if (curproc == target)
  {
    return;
  }

  pgmaster->pgroup_current_execute = target;
  
  swtch_pgroup(curproc, target);
}

// PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
struct proc* get_runnable(struct proc*);
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

    if (!p && !expired)
    {
      panic("invalid p expired");
    }
    // expired가 true일 때 스케줄러에서 p을 받아옴
    if (!p || expired || p->state != RUNNABLE || p->schedule.sched != schedidx)
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

      struct proc* rp = get_runnable(p->pgroup_current_execute);
      if (rp)
      {
        p->pgroup_current_execute = rp;
        c->proc                   = rp;
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
  pgroup_sched();
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

  pgroup_sched();

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
      set_killed(p, 1);
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
  struct clone_args args = { .mode          = CLONE_THREAD,
                             .args          = arg,
                             .start_routine = start_routine,
                             .lock          = &ptable.lock };
  // ptable.lock을 전역으로 잡을 필요가 없음 (놀랍게도)
  // acquire(&ptable.lock);
  int lwpid = clone(args);
  // release(&ptable.lock);

  if (lwpid)
  {
    *thread = lwpid;
    return 0;
  }
  // child (LWP)
  // 아래처럼 동작시키려면 clone에서 thread_create를 호출한 context의 스택을
  // 복사해야 함 그런데 구현에 실패해서 그냥 start_routine과 arg를 넘기고 스택에
  // 직접 적도록 구현...
  panic("thread_create");
  void* ret = start_routine(arg);
  thread_exit(ret);

  return -1;
}

void
thread_exit(void* retval)
{
  struct proc* curproc = myproc();

  // for (int fd = 0; fd < NOFILE; fd++)
  // {
  //   if (curproc->ofile[fd])
  //   {
  //     curproc->ofile[fd] = 0;
  //   }
  // }

  // curproc->cwd = 0;

  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      // fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }
  // begin_op();
  // iput(curproc->cwd);
  // end_op();

  curproc->cwd = 0;
  acquire(&ptable.lock);
  curproc->retval = retval;
  curproc->state  = ZOMBIE;
  wakeup1((void*)curproc->pid);

  pgroup_sched();
  panic("thread_exit");
}

int
thread_join(thread_t thread, void** retval)
{
  acquire(&ptable.lock);
  struct proc* curproc  = myproc();
  struct proc* pgmaster = curproc->pgroup_master;
  struct proc* p;
  // 자기 그룹 내의 스레드만 join 할 수 있다
  // thread에 계층 구조는 존재하지 않는다
  for (struct linked_list* pos       = pgmaster->pgroup.next;
       pos != &pgmaster->pgroup; pos = pos->next)
  {
    p = container_of(pos, struct proc, pgroup);
    if (p->pid == thread)
    {
      goto found;
    }
  }
  release(&ptable.lock);
  return -1;
found:
  for (;;)
  {
    if (p->state == ZOMBIE)
    {
      linked_list_remove(&p->pgroup);
      if (is_pgroup_have_pid(p->pgroup_master, p->pid))
      {
        panic("remove./..");
      }

      setpteu(pgmaster->pgdir, (char*)(p->sz - 2 * PGSIZE));
      freepageuvm(pgmaster->pgdir, &pgmaster->stackbin, p->sz, 2);

      *retval = p->retval;
      kfree(p->kstack);
      p->kstack  = 0;
      p->pid     = 0;
      p->pgid    = 0;
      p->parent  = 0;
      p->name[0] = 0;
      set_killed(p, 0);
      p->state = UNUSED;

      linked_list_init(&p->pgroup);
      p->pgroup_master          = 0;
      p->pgroup_current_execute = 0;

      release(&ptable.lock);
      return 0;
    }

    if (is_killed(curproc))
    {
      release(&ptable.lock);
      return -1;
    }

    sleep((void*)thread, &ptable.lock);
  }
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

  mlfqprint();
  strideprint(&mainstride);

  return 0;
}