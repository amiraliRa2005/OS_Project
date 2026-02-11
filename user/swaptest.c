#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define CHILDREN 20
#define ITERS 10
#define PAGESZ 4096

static void
fill_page(uchar *p, int pid, int iter)
{
  for(int i = 0; i < PAGESZ; i++)
    p[i] = (uchar)((pid ^ iter ^ i) & 0xff);
}

static int
check_page(uchar *p, int pid, int iter)
{
  for(int i = 0; i < PAGESZ; i++){
    if(p[i] != (uchar)((pid ^ iter ^ i) & 0xff))
      return -1;
  }
  return 0;
}

static uint
page_checksum(uchar *p)
{
  uint sum = 0;
  for(int i = 0; i < PAGESZ; i++)
    sum = (sum * 131) + p[i];
  return sum;
}

int
main(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    printf("swaptest: pipe failed\n");
    exit(1);
  }

  for(int c = 0; c < CHILDREN; c++){
    int pid = fork();
    if(pid < 0){
      printf("swaptest: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      close(fds[0]);
      void *pages[ITERS];
      int mypid = getpid();
      uint sums[ITERS];
      for(int i = 0; i < ITERS; i++){
        pages[i] = malloc(PAGESZ);
        if(pages[i] == 0){
          printf("swaptest: malloc failed pid=%d iter=%d\n", mypid, i);
          exit(1);
        }
        fill_page((uchar*)pages[i], mypid, i);
        sums[i] = page_checksum((uchar*)pages[i]);
      }
      for(int i = 0; i < ITERS; i++){
        if(check_page((uchar*)pages[i], mypid, i) < 0){
          printf("swaptest: data corrupt pid=%d iter=%d\n", mypid, i);
          exit(1);
        }
      }
      uint total = 0;
      for(int i = 0; i < ITERS; i++)
        total ^= sums[i];
      int msg[2];
      msg[0] = mypid;
      msg[1] = (int)total;
      if(write(fds[1], msg, sizeof(msg)) != sizeof(msg)){
        printf("swaptest: write failed pid=%d\n", mypid);
        exit(1);
      }
      for(int i = 0; i < ITERS; i++)
        free(pages[i]);
      close(fds[1]);
      exit(0);
    }
  }

  close(fds[1]);
  for(int i = 0; i < CHILDREN; i++){
    int msg[2];
    int n = read(fds[0], msg, sizeof(msg));
    if(n != sizeof(msg)){
      printf("swaptest: read failed\n");
      exit(1);
    }
    printf("swaptest: child pid=%d ok checksum=%u\n", msg[0], (uint)msg[1]);
  }
  close(fds[0]);

  for(int c = 0; c < CHILDREN; c++){
    int st = 0;
    int pid = wait(&st);
    if(pid < 0){
      printf("swaptest: wait failed\n");
      exit(1);
    }
    if(st != 0){
      printf("swaptest: child %d failed\n", pid);
      exit(1);
    }
  }

  printf("swaptest: ok\n");
  exit(0);
}
