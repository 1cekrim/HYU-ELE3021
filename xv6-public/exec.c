#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"


static inline int
is_pgroup_master(struct proc* p)
{
  return p->pgroup_master == p;
}

extern void change_sched(struct proc*, struct proc*);

void mlfqprint(void);
int
exec(char* path, char** argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3 + MAXARG + 1];
  struct elfhdr elf;
  struct inode* ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc* curproc = myproc();
  // TODO: 현재는 동작하지 않음
  // TODO: 현재 컨텍스트가 pgmaster가 아니면 trap 14 발생
  // TODO: 실행 중인 스레드를 process group master로 승격시키는 것이 가장 쉬울 듯
  pushcli();
  if (!is_pgroup_master(curproc))
  {
    change_sched(curproc->pgroup_master, curproc);
  }
  clear_threads_exec();
  popcli();
  begin_op();
  
  if ((ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if (readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if (elf.magic != ELF_MAGIC)
    goto bad;

  if ((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {
    if (readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if ((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    if (loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if ((sz = allocuvm(pgdir, sz, sz + 2 * PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2 * PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for (argc = 0; argv[argc]; argc++)
  {
    if (argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if (copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3 + argc] = sp;
  }
  ustack[3 + argc] = 0;

  ustack[0] = 0xffffffff; // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc + 1) * 4; // argv pointer

  sp -= (3 + argc + 1) * 4;
  if (copyout(pgdir, sp, ustack, (3 + argc + 1) * 4) < 0)
    goto bad;


  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir         = curproc->pgdir;
  curproc->pgdir   = pgdir;
  curproc->sz      = sz;
  curproc->tf->eip = elf.entry; // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

bad:
  if (pgdir)
    freevm(pgdir);
  if (ip)
  {
    iunlockput(ip);
    end_op();
  }
  return -1;
}
