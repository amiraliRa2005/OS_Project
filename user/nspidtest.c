#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
test_parent_child_visibility(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    printf("nspidtest: pipe failed\n");
    exit(1);
  }

  int parent_pid = getpid();
  int pid = fork();
  if(pid < 0){
    printf("nspidtest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // child
    close(fds[1]);
    if(unshare(CLONE_NEWPID) < 0){
      printf("nspidtest: unshare pid failed\n");
      exit(1);
    }
    int self = getpid();
    if(self != 1){
      printf("nspidtest: child pid expected 1 got %d\n", self);
      exit(1);
    }
    int pp = -1;
    if(read(fds[0], &pp, sizeof(pp)) != sizeof(pp)){
      printf("nspidtest: read parent pid failed\n");
      exit(1);
    }
    close(fds[0]);

    if(pp != self){
      if(kill(pp) == 0){
        printf("nspidtest: child could kill parent pid %d\n", pp);
        exit(1);
      }
    } else {
      // parent pid is 1 (unlikely in test environment); skip to avoid self-kill
      printf("nspidtest: skip parent kill (pid 1)\n");
    }
    exit(0);
  }

  // parent
  close(fds[0]);
  if(write(fds[1], &parent_pid, sizeof(parent_pid)) != sizeof(parent_pid)){
    printf("nspidtest: write parent pid failed\n");
    exit(1);
  }
  close(fds[1]);

  int st = 0;
  int w = wait(&st);
  if(w != pid){
    printf("nspidtest: wait returned %d expected %d\n", w, pid);
    exit(1);
  }
  if(st != 0){
    printf("nspidtest: child failed\n");
    exit(1);
  }
  printf("nspidtest: parent sees child ok (pid %d)\n", w);
}

int
main(void)
{
  test_parent_child_visibility();
  printf("nspidtest: ok\n");
  exit(0);
}
