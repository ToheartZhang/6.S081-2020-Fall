#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

int main() {
    int parent_fd[2];
    int child_fd[2];
    char c;
    pipe(parent_fd);
    pipe(child_fd);
    if (fork() == 0) {
        close(parent_fd[1]);
        close(child_fd[0]);

        int n = read(parent_fd[0], &c, 1);
        if (n != 1) {
            fprintf(2, "Child read error\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());

        write(child_fd[1], &c, 1);

        close(parent_fd[0]);
        close(child_fd[1]);
        exit(0);
    } else {
        close(parent_fd[0]);
        close(child_fd[1]);

        write(parent_fd[1], &c, 1);

        int n = read(child_fd[0], &c, 1);
        if (n != 1) {
            fprintf(2, "Parent read error\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());

        close(parent_fd[1]);
        close(child_fd[0]);
        wait(0);
        exit(0);
    }
}
