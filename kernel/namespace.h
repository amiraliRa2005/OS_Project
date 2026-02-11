#ifndef NAMESPACE_H
#define NAMESPACE_H

#include "spinlock.h"
struct inode;
struct proc;

#define CLONE_NEWPID 0x1
#define CLONE_NEWNS  0x2
#define CLONE_NEWUTS 0x4
#define CLONE_NEWIPC 0x8

#define PIDNS_MAX_DEPTH 8
#define UTS_HOSTNAME_MAX 64

struct pid_namespace {
  struct spinlock lock;
  int ref;
  int nextpid;
  int nsid;
  struct proc *init;
  struct pid_namespace *parent;
  int level;
};

struct mnt_namespace {
  struct spinlock lock;
  int ref;
  int nsid;
  struct inode *root;
};

struct uts_namespace {
  struct spinlock lock;
  int ref;
  int nsid;
  char hostname[UTS_HOSTNAME_MAX];
};

struct ipc_namespace {
  struct spinlock lock;
  int ref;
  int nsid;
};

extern struct pid_namespace *root_pid_ns;
extern struct mnt_namespace *root_mnt_ns;
extern struct uts_namespace *root_uts_ns;
extern struct ipc_namespace *root_ipc_ns;

void nsinit(void);

struct pid_namespace* pid_ns_alloc(struct pid_namespace *parent);
void pid_ns_incref(struct pid_namespace *ns);
void pid_ns_decref(struct pid_namespace *ns);
int pid_ns_allocpid(struct pid_namespace *ns);

struct mnt_namespace* mnt_ns_alloc(struct mnt_namespace *parent);
void mnt_ns_incref(struct mnt_namespace *ns);
void mnt_ns_decref(struct mnt_namespace *ns);

struct uts_namespace* uts_ns_alloc(struct uts_namespace *parent);
void uts_ns_incref(struct uts_namespace *ns);
void uts_ns_decref(struct uts_namespace *ns);

struct ipc_namespace* ipc_ns_alloc(struct ipc_namespace *parent);
void ipc_ns_incref(struct ipc_namespace *ns);
void ipc_ns_decref(struct ipc_namespace *ns);

#endif
