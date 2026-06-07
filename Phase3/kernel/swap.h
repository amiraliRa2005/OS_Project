#ifndef SWAP_H
#define SWAP_H

#include "types.h"
#include "proc.h"

#define SWAP_OP_OUT 1
#define SWAP_OP_IN 2

void swapinit(void);
void swap_fs_ready(void);
int swap_request(int op, struct proc *p, uint64 va);
int swap_free_range(struct proc *p, uint64 start, uint64 end);
void swap_free_all(struct proc *p);
int swap_enabled(void);

#endif
