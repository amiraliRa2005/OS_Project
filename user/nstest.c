#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
test_pid_basic(void)
{
  int p0 = getpid();
  if(unshare(CLONE_NEWPID) < 0){
    printf("nstest: unshare pid failed\n");
    exit(1);
  }
  int p1 = getpid();
  if(p1 != 1){
    printf("nstest: pid ns expected 1 got %d (old %d)\n", p1, p0);
    exit(1);
  }
  int pid = fork();
  if(pid < 0){
    printf("nstest: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    int cp = getpid();
    if(cp != 2){
      printf("nstest: child pid expected 2 got %d\n", cp);
      exit(1);
    }
    exit(0);
  }
  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nstest: pid ns child failed\n");
    exit(1);
  }
  printf("nstest: pid namespace ok\n");
}

static void
test_pid_visibility(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    printf("nstest: pipe failed\n");
    exit(1);
  }

  int parent_pid = getpid();
  int pid = fork();
  if(pid < 0){
    printf("nstest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[1]);
    if(unshare(CLONE_NEWPID) < 0){
      printf("nstest: unshare pid failed\n");
      exit(1);
    }
    int self = getpid();
    if(self != 1){
      printf("nstest: child pid expected 1 got %d\n", self);
      exit(1);
    }
    int pp = -1;
    if(read(fds[0], &pp, sizeof(pp)) != sizeof(pp)){
      printf("nstest: read parent pid failed\n");
      exit(1);
    }
    close(fds[0]);

    if(pp != self){
      if(kill(pp) == 0){
        printf("nstest: child could kill parent pid %d\n", pp);
        exit(1);
      }
    } else {
      printf("nstest: skip parent kill (pid 1)\n");
    }
    exit(0);
  }

  close(fds[0]);
  if(write(fds[1], &parent_pid, sizeof(parent_pid)) != sizeof(parent_pid)){
    printf("nstest: write parent pid failed\n");
    exit(1);
  }
  close(fds[1]);

  int st = 0;
  int w = wait(&st);
  if(w != pid){
    printf("nstest: wait returned %d expected %d\n", w, pid);
    exit(1);
  }
  if(st != 0){
    printf("nstest: pid visibility child failed\n");
    exit(1);
  }
  printf("nstest: pid visibility ok\n");
}

static void
test_uts(void)
{
  char buf[UTS_HOSTNAME_MAX];
  if(setHostname("nsA") < 0){
    printf("nstest: setHostname failed\n");
    exit(1);
  }
  if(getHostname(buf, sizeof(buf)) < 0){
    printf("nstest: getHostname failed\n");
    exit(1);
  }
  if(strcmp(buf, "nsA") != 0){
    printf("nstest: hostname mismatch got %s\n", buf);
    exit(1);
  }

  if(unshare(CLONE_NEWUTS) < 0){
    printf("nstest: unshare uts failed\n");
    exit(1);
  }
  if(setHostname("nsB") < 0){
    printf("nstest: setHostname nsB failed\n");
    exit(1);
  }
  if(getHostname(buf, sizeof(buf)) < 0){
    printf("nstest: getHostname nsB failed\n");
    exit(1);
  }
  if(strcmp(buf, "nsB") != 0){
    printf("nstest: hostname mismatch nsB got %s\n", buf);
    exit(1);
  }
  printf("nstest: uts namespace ok\n");
}

static void
test_ipc(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    printf("nstest: pipe failed\n");
    exit(1);
  }
  int pid = fork();
  if(pid < 0){
    printf("nstest: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    close(fds[0]);
    if(unshare(CLONE_NEWIPC) < 0){
      printf("nstest: unshare ipc failed\n");
      exit(1);
    }
    char c = 'x';
    int w = write(fds[1], &c, 1);
    if(w < 0){
      close(fds[1]);
      exit(0); // expected failure
    }
    close(fds[1]);
    exit(1); // should not be able to write
  }
  close(fds[0]);
  close(fds[1]);
  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nstest: ipc namespace failed\n");
    exit(1);
  }
  printf("nstest: ipc namespace ok\n");
}

static void
test_mount(void)
{
  int parent_id = getmntnsid();
  if(parent_id < 0){
    printf("nstest: getmntnsid failed\n");
    exit(1);
  }

  int fds[2];
  if(pipe(fds) < 0){
    printf("nstest: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("nstest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    if(unshare(CLONE_NEWNS) < 0){
      printf("nstest: unshare mount failed\n");
      exit(1);
    }
    int child_id = getmntnsid();
    if(child_id < 0){
      printf("nstest: child getmntnsid failed\n");
      exit(1);
    }
    if(write(fds[1], &child_id, sizeof(child_id)) != sizeof(child_id)){
      printf("nstest: write mount id failed\n");
      exit(1);
    }
    close(fds[1]);
    exit(0);
  }

  close(fds[1]);
  int child_id = -1;
  if(read(fds[0], &child_id, sizeof(child_id)) != sizeof(child_id)){
    printf("nstest: read mount id failed\n");
    exit(1);
  }
  close(fds[0]);

  int now_id = getmntnsid();
  if(now_id != parent_id){
    printf("nstest: mount nsid changed %d -> %d\n", parent_id, now_id);
    exit(1);
  }
  if(child_id == parent_id){
    printf("nstest: child mount nsid equals parent %d\n", child_id);
    exit(1);
  }

  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nstest: mount child failed\n");
    exit(1);
  }
  printf("nstest: mount namespace ok\n");
}

int
main(void)
{
  test_pid_visibility();
  test_pid_basic();
  test_uts();
  test_ipc();
  test_mount();
  printf("nstest: ok\n");
  exit(0);
}
