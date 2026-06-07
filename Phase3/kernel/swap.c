#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "stat.h"
#include "sleeplock.h"
#include "file.h"
#include "swap.h"

static int swap_ready = 0;
static int fs_ready = 0;
static struct spinlock fs_lock;

struct swap_request {
  struct spinlock lock;
  int pending;
  int done;
  int op;
  struct proc *p;
  uint64 va;
  int result;
};

static struct swap_request swapreq;

extern struct proc proc[NPROC];

static void swapd(void);

int
swap_enabled(void)
{
  return swap_ready;
}

void
swap_fs_ready(void)
{
  acquire(&fs_lock);
  fs_ready = 1;
  wakeup(&fs_ready);
  release(&fs_lock);
}

static void
wait_fs_ready(void)
{
  acquire(&fs_lock);
  while(!fs_ready)
    sleep(&fs_ready, &fs_lock);
  release(&fs_lock);
}

void
swapinit(void)
{
  initlock(&swapreq.lock, "swapreq");
  initlock(&fs_lock, "swapfs");
  swap_ready = 1;
  create_kernel_process(swapd, "kswapd");
}

static int
u64toa(uint64 val, int base, char *buf, int buflen)
{
  char tmp[32];
  int i = 0;
  int len = 0;

  if(buflen < 2)
    return -1;

  if(val == 0){
    tmp[i++] = '0';
  } else {
    while(val && i < (int)sizeof(tmp)){
      int d = val % base;
      tmp[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
      val /= base;
    }
  }

  while(i > 0 && len < buflen - 1){
    buf[len++] = tmp[--i];
  }
  buf[len] = 0;
  return len;
}

static int
swap_path(char *buf, int buflen, int pid, uint64 va, int alt)
{
  int len = 0;
  int n;

  if(buflen < 8)
    return -1;

  buf[len++] = '/';
  n = u64toa((uint64)pid, 10, buf + len, buflen - len);
  if(n < 0) return -1;
  len += n;
  if(len >= buflen - 1) return -1;
  buf[len++] = '_';
  n = u64toa(va, alt ? 10 : 16, buf + len, buflen - len);
  if(n < 0) return -1;
  len += n;
  if(len + 4 >= buflen) return -1;
  buf[len++] = '.';
  buf[len++] = 's';
  buf[len++] = 'w';
  buf[len++] = 'p';
  buf[len] = 0;
  return 0;
}

static struct inode*
swap_create(char *path)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(ip->type == T_FILE || ip->type == T_DEVICE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, T_FILE)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  iupdate(ip);

  if(dirlink(dp, name, ip->inum) < 0){
    ip->nlink = 0;
    iupdate(ip);
    iunlockput(ip);
    iunlockput(dp);
    return 0;
  }

  iunlockput(dp);
  return ip;
}

static int
swap_unlink(char *path)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ];
  uint off;

  if((dp = nameiparent(path, name)) == 0)
    return -1;

  ilock(dp);

  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("swap_unlink: nlink < 1");
  if(ip->type == T_DIR)
    goto bad2;

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("swap_unlink: writei");

  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  return 0;

bad2:
  iunlockput(ip);
bad:
  iunlockput(dp);
  return -1;
}

static int
swap_write_page(int pid, uint64 va, uint64 pa)
{
  char path[MAXPATH];
  struct inode *ip;
  int r;

  if(swap_path(path, sizeof(path), pid, va, 0) < 0)
    return -1;

  begin_op();
  ip = swap_create(path);
  if(ip == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  r = writei(ip, 0, pa, 0, PGSIZE);
  iunlockput(ip);
  end_op();

  return (r == PGSIZE) ? 0 : -1;
}

static int
swap_read_page(int pid, uint64 va, char *mem)
{
  char path[MAXPATH];
  struct inode *ip;
  int r;

  if(swap_path(path, sizeof(path), pid, va, 0) < 0)
    return -1;

  begin_op();
  ip = namei(path);
  if(ip == 0){
    end_op();
    if(swap_path(path, sizeof(path), pid, va, 1) < 0)
      return -1;
    begin_op();
    ip = namei(path);
    if(ip == 0){
      end_op();
      return -1;
    }
  }

  ilock(ip);
  r = readi(ip, 0, (uint64)mem, 0, PGSIZE);
  iunlockput(ip);
  end_op();

  return (r == PGSIZE) ? 0 : -1;
}

static void
swap_remove_file(int pid, uint64 va)
{
  char path[MAXPATH];

  if(swap_path(path, sizeof(path), pid, va, 0) == 0){
    begin_op();
    swap_unlink(path);
    end_op();
    return;
  }

  if(swap_path(path, sizeof(path), pid, va, 1) == 0){
    begin_op();
    swap_unlink(path);
    end_op();
  }
}

static int
swap_out_one(void)
{
  struct proc *p;
  uint64 va;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED || p->is_kernel){
      release(&p->lock);
      continue;
    }
    if(p->state == RUNNING){
      release(&p->lock);
      continue;
    }
    if(p->swapping){
      release(&p->lock);
      continue;
    }

    for(va = 0; va < p->sz; va += PGSIZE){
      pte_t *pte = walk(p->pagetable, va, 0);
      if(pte == 0)
        continue;
      if((*pte & PTE_V) && (*pte & PTE_U) && ((*pte & PTE_SWAP) == 0) && ((*pte & PTE_COW) == 0)){
        uint64 pa = PTE2PA(*pte);
        if(get_ref(pa) > 1)
          continue;
        uint flags = PTE_FLAGS(*pte);
        // mark swapped out
        *pte = (flags & ~PTE_V & ~PTE_COW) | PTE_SWAP;
        p->swapping = 1;
        sfence_vma();
        release(&p->lock);

        if(swap_write_page(p->pid, va, pa) < 0){
          acquire(&p->lock);
          pte = walk(p->pagetable, va, 0);
          if(pte)
            *pte = PA2PTE(pa) | flags;
          p->swapping = 0;
          release(&p->lock);
          return -1;
        }

        kfree((void*)pa);

        acquire(&p->lock);
        p->swapping = 0;
        release(&p->lock);
        return 0;
      }
    }
    release(&p->lock);
  }

  return -1;
}

static int
swap_in_page(struct proc *p, uint64 va)
{
  char *mem;
  pte_t *pte;
  uint flags;

  if(p == 0)
    return -1;

  mem = kalloc();
  if(mem == 0){
    if(swap_out_one() == 0)
      mem = kalloc();
  }
  if(mem == 0)
    return -1;

  if(swap_read_page(p->pid, va, mem) < 0){
    kfree(mem);
    return -1;
  }

  acquire(&p->lock);
  pte = walk(p->pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_SWAP) == 0){
    release(&p->lock);
    kfree(mem);
    return -1;
  }
  flags = PTE_FLAGS(*pte);
  flags &= ~PTE_SWAP;
  flags |= PTE_V;
  *pte = PA2PTE(mem) | (flags & ~PTE_COW);
  sfence_vma();
  release(&p->lock);

  swap_remove_file(p->pid, va);
  return 0;
}

int
swap_request(int op, struct proc *p, uint64 va)
{
  int res;

  if(!swap_ready)
    return -1;

  acquire(&swapreq.lock);
  while(swapreq.pending)
    sleep(&swapreq, &swapreq.lock);

  swapreq.pending = 1;
  swapreq.done = 0;
  swapreq.op = op;
  swapreq.p = p;
  swapreq.va = va;
  wakeup(&swapreq);

  while(!swapreq.done)
    sleep(&swapreq, &swapreq.lock);

  res = swapreq.result;
  swapreq.pending = 0;
  swapreq.done = 0;
  wakeup(&swapreq);
  release(&swapreq.lock);

  return res;
}

static void
swapd(void)
{
  struct proc *p = myproc();
  if(p)
    p->is_swapd = 1;

  for(;;){
    int op;
    struct proc *tp;
    uint64 va;
    int res;

    acquire(&swapreq.lock);
    while(!swapreq.pending)
      sleep(&swapreq, &swapreq.lock);
    op = swapreq.op;
    tp = swapreq.p;
    va = swapreq.va;
    release(&swapreq.lock);

    wait_fs_ready();

    if(op == SWAP_OP_OUT)
      res = swap_out_one();
    else
      res = swap_in_page(tp, va);

    acquire(&swapreq.lock);
    swapreq.result = res;
    swapreq.done = 1;
    wakeup(&swapreq);
    while(swapreq.pending)
      sleep(&swapreq, &swapreq.lock);
    release(&swapreq.lock);
  }
}

int
swap_free_range(struct proc *p, uint64 start, uint64 end)
{
  uint64 a;
  pte_t *pte;

  if(p == 0 || p->pagetable == 0)
    return -1;

  if(end < start)
    return -1;

  a = PGROUNDUP(start);
  for(; a < end; a += PGSIZE){
    pte = walk(p->pagetable, a, 0);
    if(pte && (*pte & PTE_SWAP)){
      swap_remove_file(p->pid, a);
      *pte = 0;
    }
  }
  return 0;
}

void
swap_free_all(struct proc *p)
{
  if(p == 0)
    return;
  swap_free_range(p, 0, p->sz);
}
