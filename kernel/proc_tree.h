#ifndef PROC_TREE_H
#define PROC_TREE_H

#define MAX_CHILDREN 8
#define MAX_PROCS    16
#define PROC_NAME_LEN 16

struct proc_info {
  char name[PROC_NAME_LEN];
  int pid;
  int ppid;
  int state;
  int child_count;
  int children[MAX_CHILDREN]; // store child PIDs 
};

struct proc_tree {
  int count;
  struct proc_info procs[MAX_PROCS];
};

#endif // PROC_TREE_H
