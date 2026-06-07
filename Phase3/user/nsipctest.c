#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
test_ipc_isolation(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    printf("nsipctest: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("nsipctest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    if(unshare(CLONE_NEWIPC) < 0){
      printf("nsipctest: unshare ipc failed\n");
      exit(1);
    }
    char c = 'x';
    int w = write(fds[1], &c, 1);
    if(w >= 0){
      printf("nsipctest: write succeeded after unshare\n");
      exit(1);
    }
    close(fds[1]);
    exit(0);
  }

  close(fds[0]);
  close(fds[1]);
  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nsipctest: child failed\n");
    exit(1);
  }
  printf("nsipctest: ipc isolation ok\n");
}

int
main(void)
{
  test_ipc_isolation();
  printf("nsipctest: ok\n");
  exit(0);
}
