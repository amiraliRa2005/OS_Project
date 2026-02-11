#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "namespace.h"
extern struct proc proc[NPROC];
extern int nice_to_weight[40];


uint64
sys_chpnice(void)
{
  int pid, n_value;
  struct proc *p;
  
  argint(0, &pid);
  argint(1, &n_value);

  if(n_value < -20 || n_value > 19) return -1;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->nice = n_value;
      p->weight = nice_to_weight[n_value + 20];
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

uint64
sys_trace(void)
{
  int mask;
  argint(0, &mask);
  struct proc *p = myproc();
  if(mask == 0)
    trace_flush(p);
  p->trace_mask = (uint64)mask;
  return 0;
}

uint64
sys_unshare(void)
{
  int flags;
  argint(0, &flags);
  return kunshare(flags);
}

uint64
sys_setHostname(void)
{
  char buf[UTS_HOSTNAME_MAX];
  if(argstr(0, buf, UTS_HOSTNAME_MAX) < 0)
    return -1;
  struct uts_namespace *uts = myproc()->uts_ns;
  acquire(&uts->lock);
  safestrcpy(uts->hostname, buf, UTS_HOSTNAME_MAX);
  release(&uts->lock);
  return 0;
}

uint64
sys_getHostname(void)
{
  uint64 addr;
  int len;
  char buf[UTS_HOSTNAME_MAX];

  argaddr(0, &addr);
  argint(1, &len);
  if(len <= 0)
    return -1;

  struct uts_namespace *uts = myproc()->uts_ns;
  acquire(&uts->lock);
  safestrcpy(buf, uts->hostname, UTS_HOSTNAME_MAX);
  release(&uts->lock);

  int n = strlen(buf) + 1;
  if(len < n)
    n = len;
  if(copyout(myproc()->pagetable, addr, buf, n) < 0)
    return -1;
  return 0;
}

uint64
sys_getmntnsid(void)
{
  struct mnt_namespace *mnt = myproc()->mnt_ns;
  if(mnt == 0)
    return -1;
  acquire(&mnt->lock);
  int id = mnt->nsid;
  release(&mnt->lock);
  return id;
}


uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->ns_pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//added
extern struct proc proc[NPROC];
uint64
sys_sysclcnt(void)
{
  extern uint64 syscall_count;
  return syscall_count;
}

//added
uint64
sys_ptree(void)
{
  int pid;
  uint64 tree_addr;

  argint(0, &pid);
  argaddr(1, &tree_addr);

  if (pid < 0 || tree_addr == 0)
    return -1;

  struct proc_tree tree;
  if (build_process_tree(&tree, pid) < 0) {
    printf("build_process_tree failed\n");
    return -1;
  }

  struct proc *p = myproc();
  if (copyout(p->pagetable, tree_addr, (char *)&tree, sizeof(tree)) < 0) {
    printf("copyout failed\n");
    return -1;
  }

  return 0;
}
