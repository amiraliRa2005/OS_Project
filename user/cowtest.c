#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    char *buf = malloc(4096);
    buf[0] = 'A';

    printf("Parent: initial page number = %d\n", physaddr(&buf[0]));

    int pid = fork();

    if(pid < 0) {
        printf("fork failed\n");
        exit(1);
    }

    if(pid == 0) {
        printf("Child: initial page number = %d\n", physaddr(&buf[0]));
        printf("Child: initial buffer val = %c\n", buf[0]);

        buf[0] = 'C'; 

        printf("Child: page number after writing = %d\n", physaddr(&buf[0]));
        printf("Child: buffer value after write: %c\n", buf[0]);
        exit(0);
    } else {
        wait(0);
        printf("Parent: after child write, page number value = %d\n", physaddr(&buf[0]));
        printf("Parent: after child write, buffer value = %c\n", buf[0]);
    }

    return 0;
}