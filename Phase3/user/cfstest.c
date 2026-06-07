#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int logfd;   // pipe write-end shared by children

/* busy-wait delay */
void delay(int n) {
  for (int i = 0; i < n * 1000000; i++) {
    __asm__("nop");
  }
}

/* int -> string */
int itoa(int x, char *buf) {
  int i = 0;
  int neg = 0;

  if (x == 0) {
    buf[i++] = '0';
    return i;
  }

  if (x < 0) {
    neg = 1;
    x = -x;
  }

  while (x > 0) {
    buf[i++] = '0' + (x % 10);
    x /= 10;
  }

  if (neg)
    buf[i++] = '-';

  // reverse
  for (int j = 0; j < i / 2; j++) {
    char t = buf[j];
    buf[j] = buf[i - j - 1];
    buf[i - j - 1] = t;
  }

  return i;
}

/* send one complete log line to logger (NO console access here) */
void send_log(const char *tag, int pid, int nice, int round) {
  char buf[128];
  int p = 0;

  while (*tag)
    buf[p++] = *tag++;

  buf[p++] = ' ';
  buf[p++] = 'p'; buf[p++] = 'i'; buf[p++] = 'd'; buf[p++] = '=';
  p += itoa(pid, buf + p);

  buf[p++] = ' ';
  buf[p++] = 'n'; buf[p++] = 'i'; buf[p++] = 'c'; buf[p++] = 'e'; buf[p++] = '=';
  p += itoa(nice, buf + p);

  buf[p++] = ' ';
  buf[p++] = 'r'; buf[p++] = 'o'; buf[p++] = 'u';
  buf[p++] = 'n'; buf[p++] = 'd'; buf[p++] = '=';
  p += itoa(round, buf + p);

  buf[p++] = '\n';

  write(logfd, buf, p);   // write to pipe (safe)
}

/* CPU-bound task */
void cpu_bound(int niceval) {
  chpnice(getpid(), niceval);

  for (int r = 0; r < 5; r++) {
    volatile int x = 0;
    for (int i = 0; i < 50000000; i++)
      x += i;

    send_log("[CPU]", getpid(), niceval, r);
  }
  exit(0);
}

/* IO-bound task */
void io_bound(int niceval) {
  chpnice(getpid(), niceval);

  for (int r = 0; r < 5; r++) {
    send_log("[ IO]", getpid(), niceval, r);
    delay(5);
  }
  exit(0);
}

/* the ONLY process allowed to write to console */
void logger(int fd) {
  char buf[128];
  int n;

  write(1, "===== CFS NICE TEST START =====\n", 33);

  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    write(1, buf, n);   // no race: single process
  }

  write(1, "===== CFS NICE TEST END =====\n", 31);
  exit(0);
}

int main() {
  int p[2];
  pipe(p);

  // logger process
  if (fork() == 0) {
    close(p[1]);
    logger(p[0]);
  }

  // workers
  close(p[0]);
  logfd = p[1];

  for (int i = 0; i < 3; i++) {
    if (fork() == 0)
      cpu_bound(i * 5);   // nice: 0, 5, 10
  }

  for (int i = 0; i < 2; i++) {
    if (fork() == 0)
      io_bound(-5);
  }

  for (int i = 0; i < 5; i++)
    wait(0);

  close(logfd);   // tell logger we're done
  wait(0);        // wait for logger
  exit(0);
}
