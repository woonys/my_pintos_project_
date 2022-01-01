#include <stdio.h>

int main(void) {
    int pid;
    pid = fork();
    if (pid > 0) {
        printf("\nHello, I'm parent!\n");
    }
    else if (pid == 0) {
        printf("\nHello, I'm child!\n");
    }
}