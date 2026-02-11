#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "namespace.h"

struct pid_namespace *root_pid_ns;
struct mnt_namespace *root_mnt_ns;
struct uts_namespace *root_uts_ns;
struct ipc_namespace *root_ipc_ns;

static int next_nsid = 1;
static struct spinlock nsid_lock;

static int
alloc_nsid(void)
{
  int id;
  acquire(&nsid_lock);
  id = next_nsid++;
  release(&nsid_lock);
  return id;
}

static void
init_ns_lock(struct spinlock *lk, char *name)
{
  initlock(lk, name);
}

void
nsinit(void)
{
  initlock(&nsid_lock, "nsid");

  root_pid_ns = pid_ns_alloc(0);
  root_mnt_ns = mnt_ns_alloc(0);
  root_uts_ns = uts_ns_alloc(0);
  root_ipc_ns = ipc_ns_alloc(0);
}

struct pid_namespace*
pid_ns_alloc(struct pid_namespace *parent)
{
  struct pid_namespace *ns = (struct pid_namespace*)kalloc();
  if(ns == 0)
    return 0;
  memset(ns, 0, sizeof(*ns));
  init_ns_lock(&ns->lock, "pidns");
  ns->ref = 1;
  ns->nextpid = 1;
  ns->nsid = alloc_nsid();
  ns->init = 0;
  if(parent){
    int level = parent->level + 1;
    if(level >= PIDNS_MAX_DEPTH){
      kfree(ns);
      return 0;
    }
    ns->parent = parent;
    ns->level = level;
    pid_ns_incref(parent);
  } else {
    ns->parent = 0;
    ns->level = 0;
  }
  return ns;
}

void
pid_ns_incref(struct pid_namespace *ns)
{
  if(ns == 0) return;
  acquire(&ns->lock);
  ns->ref++;
  release(&ns->lock);
}

void
pid_ns_decref(struct pid_namespace *ns)
{
  int freeit = 0;
  struct pid_namespace *parent = 0;
  if(ns == 0) return;
  acquire(&ns->lock);
  if(--ns->ref == 0)
    freeit = 1;
  if(freeit)
    parent = ns->parent;
  release(&ns->lock);
  if(freeit){
    kfree(ns);
    if(parent)
      pid_ns_decref(parent);
  }
}

int
pid_ns_allocpid(struct pid_namespace *ns)
{
  int pid;
  if(ns == 0) return -1;
  acquire(&ns->lock);
  pid = ns->nextpid++;
  release(&ns->lock);
  return pid;
}

struct mnt_namespace*
mnt_ns_alloc(struct mnt_namespace *parent)
{
  struct mnt_namespace *ns = (struct mnt_namespace*)kalloc();
  if(ns == 0)
    return 0;
  memset(ns, 0, sizeof(*ns));
  init_ns_lock(&ns->lock, "mntns");
  ns->ref = 1;
  ns->nsid = alloc_nsid();
  if(parent){
    acquire(&parent->lock);
    ns->root = idup(parent->root);
    release(&parent->lock);
  } else {
    ns->root = 0;
  }
  return ns;
}

void
mnt_ns_incref(struct mnt_namespace *ns)
{
  if(ns == 0) return;
  acquire(&ns->lock);
  ns->ref++;
  release(&ns->lock);
}

void
mnt_ns_decref(struct mnt_namespace *ns)
{
  int freeit = 0;
  if(ns == 0) return;
  acquire(&ns->lock);
  if(--ns->ref == 0)
    freeit = 1;
  release(&ns->lock);
  if(freeit){
    if(ns->root)
      iput(ns->root);
    kfree(ns);
  }
}

struct uts_namespace*
uts_ns_alloc(struct uts_namespace *parent)
{
  struct uts_namespace *ns = (struct uts_namespace*)kalloc();
  if(ns == 0)
    return 0;
  memset(ns, 0, sizeof(*ns));
  init_ns_lock(&ns->lock, "utsns");
  ns->ref = 1;
  ns->nsid = alloc_nsid();
  if(parent){
    acquire(&parent->lock);
    safestrcpy(ns->hostname, parent->hostname, UTS_HOSTNAME_MAX);
    release(&parent->lock);
  } else {
    safestrcpy(ns->hostname, "xv6", UTS_HOSTNAME_MAX);
  }
  return ns;
}

void
uts_ns_incref(struct uts_namespace *ns)
{
  if(ns == 0) return;
  acquire(&ns->lock);
  ns->ref++;
  release(&ns->lock);
}

void
uts_ns_decref(struct uts_namespace *ns)
{
  int freeit = 0;
  if(ns == 0) return;
  acquire(&ns->lock);
  if(--ns->ref == 0)
    freeit = 1;
  release(&ns->lock);
  if(freeit)
    kfree(ns);
}

struct ipc_namespace*
ipc_ns_alloc(struct ipc_namespace *parent)
{
  struct ipc_namespace *ns = (struct ipc_namespace*)kalloc();
  if(ns == 0)
    return 0;
  memset(ns, 0, sizeof(*ns));
  init_ns_lock(&ns->lock, "ipcns");
  ns->ref = 1;
  ns->nsid = alloc_nsid();
  if(parent){
    // no state to copy for now
  }
  return ns;
}

void
ipc_ns_incref(struct ipc_namespace *ns)
{
  if(ns == 0) return;
  acquire(&ns->lock);
  ns->ref++;
  release(&ns->lock);
}

void
ipc_ns_decref(struct ipc_namespace *ns)
{
  int freeit = 0;
  if(ns == 0) return;
  acquire(&ns->lock);
  if(--ns->ref == 0)
    freeit = 1;
  release(&ns->lock);
  if(freeit)
    kfree(ns);
}
