#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
test_mnt_ns(void)
{
  int parent_id = getmntnsid();
  if(parent_id < 0){
    printf("nsmounttest: getmntnsid failed\n");
    exit(1);
  }

  int fds[2];
  if(pipe(fds) < 0){
    printf("nsmounttest: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("nsmounttest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    if(unshare(CLONE_NEWNS) < 0){
      printf("nsmounttest: unshare mount failed\n");
      exit(1);
    }
    int child_id = getmntnsid();
    if(child_id < 0){
      printf("nsmounttest: child getmntnsid failed\n");
      exit(1);
    }
    if(write(fds[1], &child_id, sizeof(child_id)) != sizeof(child_id)){
      printf("nsmounttest: write failed\n");
      exit(1);
    }
    close(fds[1]);
    exit(0);
  }

  close(fds[1]);
  int child_id = -1;
  if(read(fds[0], &child_id, sizeof(child_id)) != sizeof(child_id)){
    printf("nsmounttest: read failed\n");
    exit(1);
  }
  close(fds[0]);

  int now_id = getmntnsid();
  if(now_id != parent_id){
    printf("nsmounttest: parent id changed %d -> %d\n", parent_id, now_id);
    exit(1);
  }
  if(child_id == parent_id){
    printf("nsmounttest: child id equals parent id %d\n", child_id);
    exit(1);
  }

  int st = 0;
  wait(&st);
  if(st != 0){
    printf("nsmounttest: child failed\n");
    exit(1);
  }

  printf("nsmounttest: mount namespace ok (parent %d child %d)\n", parent_id, child_id);
}

int
main(void)
{
  test_mnt_ns();
  printf("nsmounttest: ok\n");
  exit(0);
}
