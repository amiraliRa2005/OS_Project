#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
test_uts_isolation(void)
{
  char buf[UTS_HOSTNAME_MAX];
  if(setHostname("parent") < 0){
    printf("nsutstest: setHostname parent failed\n");
    exit(1);
  }
  if(getHostname(buf, sizeof(buf)) < 0){
    printf("nsutstest: getHostname parent failed\n");
    exit(1);
  }
  if(strcmp(buf, "parent") != 0){
    printf("nsutstest: parent hostname mismatch got %s\n", buf);
    exit(1);
  }

  int fds[2];
  if(pipe(fds) < 0){
    printf("nsutstest: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("nsutstest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    if(unshare(CLONE_NEWUTS) < 0){
      printf("nsutstest: unshare uts failed\n");
      exit(1);
    }
    if(setHostname("child") < 0){
      printf("nsutstest: setHostname child failed\n");
      exit(1);
    }
    if(getHostname(buf, sizeof(buf)) < 0){
      printf("nsutstest: getHostname child failed\n");
      exit(1);
    }
    if(strcmp(buf, "child") != 0){
      printf("nsutstest: child hostname mismatch got %s\n", buf);
      exit(1);
    }
    char ok = 1;
    write(fds[1], &ok, 1);
    close(fds[1]);
    exit(0);
  }

  close(fds[1]);
  char ok = 0;
  if(read(fds[0], &ok, 1) != 1 || ok != 1){
    printf("nsutstest: child did not confirm\n");
    exit(1);
  }
  close(fds[0]);

  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nsutstest: child failed\n");
    exit(1);
  }

  if(getHostname(buf, sizeof(buf)) < 0){
    printf("nsutstest: getHostname parent failed (post)\n");
    exit(1);
  }
  if(strcmp(buf, "parent") != 0){
    printf("nsutstest: parent hostname changed to %s\n", buf);
    exit(1);
  }

  printf("nsutstest: uts isolation ok\n");
}

int
main(void)
{
  test_uts_isolation();
  printf("nsutstest: ok\n");
  exit(0);
}
