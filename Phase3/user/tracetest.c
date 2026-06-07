#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"

int
main(void)
{
  int pid;

  printf("=== TRACE TEST START ===\n");

  // Test 1: trace write only
  trace(1 << SYS_write);
  write(1, "A\n", 2);

  // Test 2: mask filtering (open should not be traced)
  open("README", 0);

  // Test 3: multiple syscalls (write + exit)
  trace((1 << SYS_write) | (1 << SYS_exit));
  write(1, "B\n", 2);

  // Test 4: fork inheritance
  trace(1 << SYS_write);
  pid = fork();
  if(pid == 0){
    write(1, "child\n", 6);
    exit(0);
  } else {
    write(1, "parent\n", 7);
    wait(0);
  }

  // Test 5: exec tracing
  trace(1 << SYS_exec);
  pid = fork();
  if(pid == 0){
    char *argv[] = { "echo", "exec-ok", 0 };
    exec("echo", argv);
    exit(1);
  } else {
    wait(0);
  }

  // Test 6: disable tracing
  trace(0);
  write(1, "NO TRACE\n", 9);

  printf("=== TRACE TEST END ===\n");
  exit(0);
}
