#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *a) {
    fprintf(stderr, "Usage: %s <command> [args...]\n", a);
    exit(1);
}

static double diff_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1e9;
}

int main(int c, char **v) {
    if (c < 2) usage(v[0]);

    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        fprintf(stderr, "Error: clock_gettime failed: %s\n", strerror(errno));
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        execvp(v[1], &v[1]);
        fprintf(stderr, "Error: execvp failed for %s: %s\n", v[1], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "Error: waitpid failed: %s\n", strerror(errno));
        return 1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        fprintf(stderr, "Error: clock_gettime failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Child PID: %ld\n", (long)pid);

    if (WIFEXITED(status)) {
        printf("Exit code: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("Terminated by signal: %d (%s)\n", sig, strsignal(sig));
    } else {
        printf("Child ended with unknown status: %d\n", status);
    }

    printf("Elapsed time: %.6f seconds\n", diff_sec(start, end));
    return 0;
}
