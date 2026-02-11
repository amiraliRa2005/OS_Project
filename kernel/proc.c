#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "proc_tree.h"
#include "namespace.h"
#include "swap.h"

int nice_to_weight[40] = {
 /* -20 */ 88761, 71755, 56483, 46273, 36291,
 /* -15 */ 29154, 23254, 18705, 14949, 11916,
 /* -10 */  9548,  7620,  6100,  4904,  3906,
 /* -5 */  3121,  2501,  1991,  1586,  1277,
 /* 0 */  1024,   820,   655,   526,   423,
 /* 5 */   335,   272,   215,   172,   137,
 /* 10 */   110,    87,    70,    56,    45,
 /* 15 */    36,    29,    23,    18,    15
};

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
void kproc_start(void);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  nsinit();
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

static int
pid_ns_is_ancestor(struct pid_namespace *ancestor, struct pid_namespace *ns)
{
  while(ns){
    if(ns == ancestor)
      return 1;
    ns = ns->parent;
  }
  return 0;
}

static int
pid_in_ns(struct proc *p, struct pid_namespace *ns)
{
  if(p == 0 || ns == 0 || p->pid_ns == 0)
    return -1;
  if(!pid_ns_is_ancestor(ns, p->pid_ns))
    return -1;
  if(ns->level < 0 || ns->level >= p->pid_map_len)
    return -1;
  return p->pid_map[ns->level];
}

static int
pid_assign_newproc(struct proc *p, struct pid_namespace *ns)
{
  if(p == 0 || ns == 0)
    return -1;
  if(ns->level >= PIDNS_MAX_DEPTH)
    return -1;

  for(int i = 0; i < PIDNS_MAX_DEPTH; i++)
    p->pid_map[i] = 0;
  p->pid_map_len = ns->level + 1;

  for(struct pid_namespace *cur = ns; cur; cur = cur->parent){
    p->pid_map[cur->level] = pid_ns_allocpid(cur);
  }
  p->ns_pid = p->pid_map[ns->level];
  return 0;
}

static int
pid_assign_unshare(struct proc *p, struct pid_namespace *ns)
{
  if(p == 0 || ns == 0)
    return -1;
  if(ns->level >= PIDNS_MAX_DEPTH)
    return -1;
  if(p->pid_map_len <= ns->level)
    p->pid_map_len = ns->level + 1;
  p->pid_map[ns->level] = pid_ns_allocpid(ns);
  p->ns_pid = p->pid_map[ns->level];
  return 0;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Namespace and tracing defaults
  p->pid_ns = 0;
  p->mnt_ns = 0;
  p->uts_ns = 0;
  p->ipc_ns = 0;
  p->ns_pid = 0;
  p->pid_map_len = 0;
  for(int i = 0; i < PIDNS_MAX_DEPTH; i++)
    p->pid_map[i] = 0;
  p->trace_mask = 0;
  p->trace_count = 0;
  p->is_kernel = 0;
  p->is_swapd = 0;
  p->swapping = 0;
  p->kentry = 0;
  p->kentry = 0;
  
  // Initialize CFS default values for new processes
  p->nice = 0;
  p->weight = 1024;          // NICE_0_LOAD
  p->vruntime = 0;
  p->timeslice = 10;         // Initial default slice
  p->spent_ticks = 0;
  p->last_sched = ticks;
  return p;
}


int
create_kernel_process(void (*entry)(void), char *name)
{
  struct proc *p;

  if((p = allocproc()) == 0)
    return -1;

  p->is_kernel = 1;
  p->pid_ns = root_pid_ns;
  p->mnt_ns = root_mnt_ns;
  p->uts_ns = root_uts_ns;
  p->ipc_ns = root_ipc_ns;
  pid_ns_incref(p->pid_ns);
  mnt_ns_incref(p->mnt_ns);
  uts_ns_incref(p->uts_ns);
  ipc_ns_incref(p->ipc_ns);
  if(pid_assign_newproc(p, p->pid_ns) < 0){
    freeproc(p);
    release(&p->lock);
    return -1;
  }
  p->pid_ns->init = p;

  safestrcpy(p->name, name, sizeof(p->name));

  p->kentry = entry;
  memset(p->trapframe, 0, sizeof(*p->trapframe));
  p->context.ra = (uint64)kproc_start;
  p->context.sp = p->kstack + PGSIZE;

  p->state = RUNNABLE;
  release(&p->lock);

  return p->pid;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable){
    release(&p->lock);
    swap_free_all(p);
    acquire(&p->lock);
  }
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->ns_pid = 0;
  p->pid_map_len = 0;
  for(int i = 0; i < PIDNS_MAX_DEPTH; i++)
    p->pid_map[i] = 0;
  if(p->pid_ns)
    pid_ns_decref(p->pid_ns);
  if(p->mnt_ns)
    mnt_ns_decref(p->mnt_ns);
  if(p->uts_ns)
    uts_ns_decref(p->uts_ns);
  if(p->ipc_ns)
    ipc_ns_decref(p->ipc_ns);
  p->pid_ns = 0;
  p->mnt_ns = 0;
  p->uts_ns = 0;
  p->ipc_ns = 0;
  p->trace_mask = 0;
  p->trace_count = 0;
  p->is_kernel = 0;
  p->is_swapd = 0;
  p->swapping = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

void
trace_append(struct proc *p, int num, int ret)
{
  if(p == 0)
    return;
  acquire(&p->lock);
  if(p->trace_count < NELEM(p->trace_entries)){
    p->trace_entries[p->trace_count].pid = p->pid;
    p->trace_entries[p->trace_count].num = num;
    p->trace_entries[p->trace_count].ret = ret;
    p->trace_count++;
  }
  release(&p->lock);
}

void
trace_flush(struct proc *p)
{
  if(p == 0)
    return;
  struct trace_entry local[NELEM(p->trace_entries)];
  int n = 0;

  acquire(&p->lock);
  n = p->trace_count;
  if(n > NELEM(p->trace_entries))
    n = NELEM(p->trace_entries);
  for(int i = 0; i < n; i++)
    local[i] = p->trace_entries[i];
  p->trace_count = 0;
  release(&p->lock);

  for(int i = 0; i < n; i++){
    const char *name = syscall_name(local[i].num);
    printf("%d: syscall %s -> %d\n", local[i].pid, name, local[i].ret);
  }
}


// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->pid_ns = root_pid_ns;
  p->mnt_ns = root_mnt_ns;
  p->uts_ns = root_uts_ns;
  p->ipc_ns = root_ipc_ns;
  pid_ns_incref(p->pid_ns);
  mnt_ns_incref(p->mnt_ns);
  uts_ns_incref(p->uts_ns);
  ipc_ns_incref(p->ipc_ns);
  if(pid_assign_newproc(p, p->pid_ns) < 0)
    panic("pid_assign userinit");
  p->pid_ns->init = p;

  p->cwd = namei("/");
  if(p->mnt_ns && p->mnt_ns->root == 0){
    acquire(&p->mnt_ns->lock);
    if(p->mnt_ns->root == 0)
      p->mnt_ns->root = idup(p->cwd);
    release(&p->mnt_ns->lock);
  }

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    uint64 newsz = sz + n;
    swap_free_range(p, newsz, sz);
    sz = uvmdealloc(p->pagetable, sz, newsz);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // inherit namespaces
  np->pid_ns = p->pid_ns;
  np->mnt_ns = p->mnt_ns;
  np->uts_ns = p->uts_ns;
  np->ipc_ns = p->ipc_ns;
  pid_ns_incref(np->pid_ns);
  mnt_ns_incref(np->mnt_ns);
  uts_ns_incref(np->uts_ns);
  ipc_ns_incref(np->ipc_ns);
  if(pid_assign_newproc(np, np->pid_ns) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  // inherit syscall trace mask
  np->trace_mask = p->trace_mask;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = pid_in_ns(np, p->pid_ns);
  if(pid < 0)
    pid = np->ns_pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Create a kernel process that starts executing at entry.
// Unshare namespaces for the current process.
int
kunshare(int flags)
{
  struct proc *p = myproc();
  struct pid_namespace *new_pid = 0;
  struct mnt_namespace *new_mnt = 0;
  struct uts_namespace *new_uts = 0;
  struct ipc_namespace *new_ipc = 0;

  if(flags & CLONE_NEWPID)
    new_pid = pid_ns_alloc(p->pid_ns);
  if(flags & CLONE_NEWNS)
    new_mnt = mnt_ns_alloc(p->mnt_ns);
  if(flags & CLONE_NEWUTS)
    new_uts = uts_ns_alloc(p->uts_ns);
  if(flags & CLONE_NEWIPC)
    new_ipc = ipc_ns_alloc(p->ipc_ns);

  if((flags & CLONE_NEWPID) && new_pid == 0)
    goto fail;
  if((flags & CLONE_NEWNS) && new_mnt == 0)
    goto fail;
  if((flags & CLONE_NEWUTS) && new_uts == 0)
    goto fail;
  if((flags & CLONE_NEWIPC) && new_ipc == 0)
    goto fail;

  acquire(&p->lock);
  if(new_pid){
    pid_ns_decref(p->pid_ns);
    p->pid_ns = new_pid;
    pid_assign_unshare(p, p->pid_ns);
    p->pid_ns->init = p;
  }
  if(new_mnt){
    mnt_ns_decref(p->mnt_ns);
    p->mnt_ns = new_mnt;
  }
  if(new_uts){
    uts_ns_decref(p->uts_ns);
    p->uts_ns = new_uts;
  }
  if(new_ipc){
    ipc_ns_decref(p->ipc_ns);
    p->ipc_ns = new_ipc;
  }
  release(&p->lock);

  return 0;

fail:
  if(new_pid) pid_ns_decref(new_pid);
  if(new_mnt) mnt_ns_decref(new_mnt);
  if(new_uts) uts_ns_decref(new_uts);
  if(new_ipc) ipc_ns_decref(new_ipc);
  return -1;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = p->parent ? p->parent : initproc; //added for reparenting
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // flush any pending syscall traces before exiting
  trace_flush(p);

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pid_in_ns(pp, p->pid_ns);
          if(pid < 0)
            pid = pp->ns_pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

//   Calculate the dynamic time quantum (timeslice) for a process.
//   Based on the CFS algorithm (Algorithm 1, line 62 in project doc).
//   Formula: ideal_runtime = target_latency * (p->weight / total_runnable_weight)
//   This ensures high-priority processes (higher weight) receive longer 
//   execution windows, reducing context switch overhead.
int
timeslice_for(struct proc *p)
{
  int total_weight = 0;
  struct proc *rp;

  for(rp = proc; rp < &proc[NPROC]; rp++) {
    if(rp->state == RUNNABLE)
      total_weight += rp->weight;
  }
  
  if(total_weight == 0) return 10; 
  
  int target_latency = 20; 
  int ideal = (target_latency * p->weight) / total_weight;
  int min_granularity = 2;
  
  return (ideal < min_granularity) ? min_granularity : ideal;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    intr_on();

    struct proc *best_p = 0;

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE && !p->swapping) {
        if(best_p == 0 || p->vruntime < best_p->vruntime) {
          if(best_p)
            release(&best_p->lock);
          best_p = p;
          continue;
        }
      }
      release(&p->lock);
    }

    if(best_p){
      // Dynamic Timeslice Allocation: Calculate quota before context switch
      best_p->timeslice = timeslice_for(best_p);
      best_p->spent_ticks = 0; 

      best_p->state = RUNNING;
      c->proc = best_p;
      best_p->last_sched = ticks; 
      
      // Perform context switch to the selected process
      swtch(&c->context, &best_p->context);
      c->proc = 0;
      release(&best_p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  uint64 delta_exec = ticks - p->last_sched;
  if(delta_exec > 0) {
    p->vruntime += (delta_exec * 1024) / p->weight;
  }
  p->last_sched = ticks;
  p->spent_ticks = 0;
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);
    swap_fs_ready();
    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Entry wrapper for kernel processes. Releases p->lock before running.
void
kproc_start(void)
{
  struct proc *p = myproc();
  void (*fn)(void) = p->kentry;

  release(&p->lock);
  if(fn)
    fn();
  panic("kproc_start");
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;
  struct proc *cur = myproc();
  struct pid_namespace *ns = cur ? cur->pid_ns : 0;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(ns && pid_in_ns(p, ns) == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

/*addedd*/
int build_process_tree(struct proc_tree *tree, int root_pid)   
{
  struct proc *p;
  int count = 0;

  tree->count = 0;
  for(int i = 0; i < MAX_PROCS; i++){
    tree->procs[i].pid = 0;
    tree->procs[i].ppid = -1;
    tree->procs[i].state = 0;
    tree->procs[i].child_count = 0;
    for(int j = 0; j < MAX_CHILDREN; j++) tree->procs[i].children[j] = -1;
  }

  int pid_to_index[NPROC];
  for(int i = 0; i < NPROC; i++) pid_to_index[i] = -1;

  for(int i = 0; i < NPROC && count < MAX_PROCS; i++){
    p = &proc[i];
    acquire(&p->lock);
    if(p->state != UNUSED){
      safestrcpy(tree->procs[count].name, p->name, PROC_NAME_LEN);
      tree->procs[count].pid = p->pid;
      tree->procs[count].ppid = p->parent ? p->parent->pid : -1;
      tree->procs[count].state = p->state;
      tree->procs[count].child_count = 0;
      for(int j = 0; j < MAX_CHILDREN; j++) tree->procs[count].children[j] = -1;

      if(p->pid >= 0 && p->pid < NPROC) pid_to_index[p->pid] = count;
      count++;
    }
    release(&p->lock);
  }
  tree->count = count;

  for(int i = 0; i < tree->count; i++){
    int parent_pid = tree->procs[i].ppid;
    if(parent_pid != -1 && parent_pid < NPROC){
      int idx = pid_to_index[parent_pid];
      if(idx != -1){
        if(tree->procs[idx].child_count < MAX_CHILDREN){
          tree->procs[idx].children[ tree->procs[idx].child_count++ ] = tree->procs[i].pid;
        }
      }
    }
  }

  int root_found = 0;
  if(root_pid >= 0){
    for(int i = 0; i < tree->count; i++){
      if(tree->procs[i].pid == root_pid) { root_found = 1; break; }
    }
    if(!root_found){
      printf("build_process_tree: root pid %d not found\n", root_pid);
    }
  }

  printf("build_process_tree: built tree with %d processes\n", tree->count);
  return 0;
}
