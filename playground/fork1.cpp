#include <unistd.h>   // fork, getpid, getppid
#include <cstdio>     // printf

int main() {
    printf("before fork: I am process %d\n", getpid());

    pid_t pid = fork();   // from here on, there are TWO processes running this code

    if (pid == 0) {
        printf("child:  my pid is %d, my parent is %d\n", getpid(), getppid());
    } else {
        printf("parent: my pid is %d, my child is %d\n", getpid(), pid);
    }
    return 0;
}