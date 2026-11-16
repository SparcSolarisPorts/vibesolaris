/* SPDX-License-Identifier: Unlicense */
/* Monitor tool: background process management (src/monitor.c).
 *
 * Forks a process, captures stdout/stderr via pipe, stores output
 * in a bounded buffer. Supports kill and output retrieval. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* ---- Local helpers ---- */
static char *dupstr(const char *s){char *r=(char*)malloc(strlen(s)+1);if(r)strcpy(r,s);return r;}

/* Monitor slot structure */
typedef struct {
    long pid;
    char command[256];
    char output[VS_MONITOR_OUTPUT_MAX];
    size_t output_pos;
    int read_fd;
    int write_fd;
    int active;
    int exited;
    int exit_status;
} VSMonitorSlot;

static VSMonitorSlot monitors[VS_MAX_MONITORS];
static int monitors_initialized = 0;

static void monitor_init(void)
{
    if (!monitors_initialized) {
        int i;
        for (i = 0; i < VS_MAX_MONITORS; i++) {
            monitors[i].pid = 0;
            monitors[i].active = 0;
            monitors[i].exited = 0;
            monitors[i].read_fd = -1;
            monitors[i].write_fd = -1;
            monitors[i].output_pos = 0;
            monitors[i].output[0] = 0;
            monitors[i].command[0] = 0;
        }
        monitors_initialized = 1;
    }
}

/* Reap any exited monitors */
static void monitor_reap(void)
{
    int i;
    for (i = 0; i < VS_MAX_MONITORS; i++) {
        if (monitors[i].active && !monitors[i].exited) {
            int status;
            pid_t result = waitpid((pid_t)monitors[i].pid, &status, WNOHANG);
            if (result > 0) {
                monitors[i].exited = 1;
                monitors[i].exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                /* Read any remaining output */
                char buf[4096];
                ssize_t n;
                while ((n = read(monitors[i].read_fd, buf, sizeof(buf))) > 0) {
                    size_t room = VS_MONITOR_OUTPUT_MAX - monitors[i].output_pos - 1;
                    if ((size_t)n > room) n = (ssize_t)room;
                    memcpy(monitors[i].output + monitors[i].output_pos, buf, (size_t)n);
                    monitors[i].output_pos += (size_t)n;
                    monitors[i].output[monitors[i].output_pos] = 0;
                }
                close(monitors[i].read_fd);
                monitors[i].read_fd = -1;
            }
        }
    }
}

/* ---- Public API ---- */

int vs_monitor_start(const char *cmd)
{
    int slot = -1;
    int pipefd[2];
    pid_t pid;

    monitor_init();

    if (!cmd || !*cmd) return -1;

    /* Find a free slot */
    int i;
    for (i = 0; i < VS_MAX_MONITORS; i++) {
        if (!monitors[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* Create pipe */
    if (pipe(pipefd) != 0) return -1;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);
    monitors[slot].pid = (long)pid;
    monitors[slot].read_fd = pipefd[0];
    monitors[slot].write_fd = -1;
    monitors[slot].active = 1;
    monitors[slot].exited = 0;
    monitors[slot].output_pos = 0;
    monitors[slot].output[0] = 0;
    strncpy(monitors[slot].command, cmd, sizeof(monitors[slot].command) - 1);
    monitors[slot].command[sizeof(monitors[slot].command) - 1] = 0;

    /* Read any initial output */
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        size_t room = VS_MONITOR_OUTPUT_MAX - monitors[slot].output_pos - 1;
        if ((size_t)n > room) n = (ssize_t)room;
        memcpy(monitors[slot].output + monitors[slot].output_pos, buf, (size_t)n);
        monitors[slot].output_pos += (size_t)n;
        monitors[slot].output[monitors[slot].output_pos] = 0;
    }

    return slot;
}

char *vs_monitor_output(int monitor_id)
{
    monitor_init();
    monitor_reap();

    if (monitor_id < 0 || monitor_id >= VS_MAX_MONITORS) {
        return dupstr("ERROR: invalid monitor id");
    }

    if (!monitors[monitor_id].active) {
        return dupstr("ERROR: monitor not active");
    }

    /* Read new output */
    char buf[4096];
    ssize_t n;
    while ((n = read(monitors[monitor_id].read_fd, buf, sizeof(buf))) > 0) {
        size_t room = VS_MONITOR_OUTPUT_MAX - monitors[monitor_id].output_pos - 1;
        if ((size_t)n > room) n = (ssize_t)room;
        memcpy(monitors[monitor_id].output + monitors[monitor_id].output_pos, buf, (size_t)n);
        monitors[monitor_id].output_pos += (size_t)n;
        monitors[monitor_id].output[monitors[monitor_id].output_pos] = 0;
    }

    char *result = dupstr(monitors[monitor_id].output);
    if (!result) return dupstr("ERROR: out of memory");

    /* Check if process has exited */
    if (monitors[monitor_id].exited) {
        char *with_status = (char *)malloc(strlen(result) + 256);
        if (with_status) {
            snprintf(with_status, strlen(result) + 256,
                     "%s\n[monitor exited with status %d]",
                     result, monitors[monitor_id].exit_status);
            free(result);
            return with_status;
        }
    }
    return result;
}

int vs_monitor_kill(int monitor_id)
{
    monitor_init();

    if (monitor_id < 0 || monitor_id >= VS_MAX_MONITORS) return -1;
    if (!monitors[monitor_id].active) return -1;

    if (monitors[monitor_id].pid > 0) {
        kill((pid_t)monitors[monitor_id].pid, SIGTERM);
        usleep(100000); /* 100ms grace period */
        kill((pid_t)monitors[monitor_id].pid, SIGKILL);
    }

    if (monitors[monitor_id].read_fd >= 0) {
        close(monitors[monitor_id].read_fd);
        monitors[monitor_id].read_fd = -1;
    }

    monitors[monitor_id].active = 0;
    monitors[monitor_id].exited = 1;
    monitors[monitor_id].exit_status = -1;
    return 0;
}

int vs_monitor_kill_all(void)
{
    monitor_init();
    int i;
    for (i = 0; i < VS_MAX_MONITORS; i++) {
        if (monitors[i].active) {
            vs_monitor_kill(i);
        }
    }
    return 0;
}

int vs_monitor_is_running(int monitor_id)
{
    monitor_init();
    monitor_reap();
    if (monitor_id < 0 || monitor_id >= VS_MAX_MONITORS) return 0;
    return monitors[monitor_id].active && !monitors[monitor_id].exited;
}
