#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *a) {
    fprintf(stderr, "Usage: %s <pid>\n", a);
    exit(1);
}

static int isnum(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return 0;
    }
    return 1;
}

static FILE *open_proc_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s: %s\n", path, strerror(errno));
    }
    return f;
}

static int read_cmdline(const char *path, char *out, size_t out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }

    size_t n = fread(out, 1, out_sz - 1, f);
    fclose(f);
    out[n] = '\0';

    if (n == 0) {
        out[0] = '\0';
        return 1;
    }

    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\0') out[i] = ' ';
    }

    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[n - 1] = '\0';
        n--;
    }

    return 1;
}

static int parse_stat_line(const char *line,
                           char *state_out,
                           long *ppid_out,
                           unsigned long long *utime_out,
                           unsigned long long *stime_out) {
    const char *rp = strrchr(line, ')');
    if (!rp) return 0;

    const char *p = rp + 2; // skip ") "
    if (*p == '\0') return 0;

    char state = '\0';
    long ppid = -1;
    if (sscanf(p, "%c %ld", &state, &ppid) != 2) return 0;

    char *tmp = strdup(p);
    if (!tmp) return 0;

    // After comm, token indices:
    // idx0=state(3), idx1=ppid(4), ... idx11=utime(14), idx12=stime(15)
    unsigned long long utime = 0, stime = 0;
    int idx = 0;
    char *saveptr = NULL;

    for (char *tok = strtok_r(tmp, " ", &saveptr); tok; tok = strtok_r(NULL, " ", &saveptr)) {
        if (idx == 11) utime = strtoull(tok, NULL, 10);
        if (idx == 12) {
            stime = strtoull(tok, NULL, 10);
            break;
        }
        idx++;
    }

    free(tmp);
    if (idx < 12) return 0;

    *state_out = state;
    *ppid_out = ppid;
    *utime_out = utime;
    *stime_out = stime;
    return 1;
}

static int read_vmrss_kb(const char *path, long *vmrss_kb_out) {
    FILE *f = open_proc_file(path);
    if (!f) return 0;

    char line[512];
    long kb = -1;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            char *p = line + 6;
            while (*p && isspace((unsigned char)*p)) p++;
            errno = 0;
            long val = strtol(p, NULL, 10);
            if (errno == 0) kb = val;
            break;
        }
    }

    fclose(f);

    if (kb < 0) return 0;
    *vmrss_kb_out = kb;
    return 1;
}

int main(int c, char **v) {
    if (c != 2 || !isnum(v[1])) usage(v[0]);

    const char *pid = v[1];

    char stat_path[256], status_path[256], cmdline_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid);
    snprintf(status_path, sizeof(status_path), "/proc/%s/status", pid);
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", pid);

    FILE *sf = open_proc_file(stat_path);
    if (!sf) return 1;

    char statline[4096];
    if (!fgets(statline, sizeof(statline), sf)) {
        fprintf(stderr, "Error: failed to read %s\n", stat_path);
        fclose(sf);
        return 1;
    }
    fclose(sf);

    char state = '?';
    long ppid = -1;
    unsigned long long utime = 0, stime = 0;

    if (!parse_stat_line(statline, &state, &ppid, &utime, &stime)) {
        fprintf(stderr, "Error: failed to parse %s\n", stat_path);
        return 1;
    }

    long vmrss_kb = -1;
    if (!read_vmrss_kb(status_path, &vmrss_kb)) {
        vmrss_kb = -1;
    }

    char cmdline[4096];
    if (!read_cmdline(cmdline_path, cmdline, sizeof(cmdline))) {
        strncpy(cmdline, "(unavailable)", sizeof(cmdline));
        cmdline[sizeof(cmdline) - 1] = '\0';
    }

    long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) ticks = 100;

    double cpu_sec = (double)(utime + stime) / (double)ticks;

    printf("PID: %s\n", pid);
    printf("Process state: %c\n", state);
    printf("Parent PID: %ld\n", ppid);
    printf("Command line: %s\n", cmdline[0] ? cmdline : "(empty)");
    printf("CPU time (user+system): %.3f seconds\n", cpu_sec);
    if (vmrss_kb >= 0) {
        printf("Resident memory (VmRSS): %ld kB\n", vmrss_kb);
    } else {
        printf("Resident memory (VmRSS): (unknown)\n");
    }

    return 0;
}
