#ifndef __PROC_H__
#define __PROC_H__

#include "linked_list.h"

// Per-CPU state
struct cpu
{
  uchar apicid;              // Local APIC ID
  struct context* scheduler; // swtch() here to enter scheduler
  struct taskstate ts;       // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS]; // x86 global descriptor table
  volatile uint started;     // Has the CPU started?
  int ncli;                  // Depth of pushcli nesting.
  int intena;                // Were interrupts enabled before pushcli?
  struct proc* proc;         // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

// PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context
{
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate
{
  UNUSED,
  EMBRYO,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};

enum schedulerenum
{
  SCHEDMLFQ,
  SCHEDSTRIDE
};

enum CLONEMODE
{
  CLONE_NONE = 1,
  CLONE_THREAD = 2
};

// Per-process state
struct proc
{
  uint sz;                    // Size of process memory (bytes)
  pde_t* pgdir;               // Page table
  char* kstack;               // Bottom of kernel stack for this process
  enum procstate state;       // Process state
  int pid;                    // Process ID
  struct proc* parent;        // Parent process
  struct trapframe* tf;       // Trap frame for current syscall
  struct context* context;    // swtch() here to run process
  void* chan;                 // If non-zero, sleeping on chan
  int killed;                 // If non-zero, have been killed
  struct file* ofile[NOFILE]; // Open files
  struct inode* cwd;          // Current directory
  char name[16];              // Process name (debugging)

  int pgid;
  struct linked_list pgroup;
  struct proc* pgroup_master;
  struct proc* pgroup_next_execute;
  void* retval;

  struct
  {
    uint lastscheduledtick;
    uint executionticks;
    int level;
    int yield;
    enum schedulerenum sched;
  } schedule;
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

int thread_create(thread_t* thread, void* (*start_routine)(void*), void* arg);
void thread_exit(void* retval);
int thread_join(thread_t thread, void** retval);

static inline int is_killed(struct proc* p)
{
  return p->pgroup_master->killed;
}

static inline void set_killed(struct proc* p, int killed)
{
  p->pgroup_master->killed = killed;
}

#endif