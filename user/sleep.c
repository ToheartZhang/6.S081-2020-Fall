#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

void usage(){
    printf("Usage: sleep <NUM> \n");
    printf("NUM must be non-negative\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
    }
    char c = argv[1][0];
    if (c < '0' || c > '9') {
        usage();
    }
    int num = atoi(argv[1]);
    printf("Sleep for %d ticks\n", num);
    sleep(num);

    exit(0);
}
