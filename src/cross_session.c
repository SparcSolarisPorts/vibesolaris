/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Cross-Session Messaging — Instance discovery via shared filesystem */
/*
 * Registers this instance's PID in a shared directory, discovers other
 * running instances, and sends/receives messages via files.
 *
 * Uses ~/.vibesolaris/sessions/ as the shared directory.
 * Each instance creates a file named by its PID containing metadata.
 * Messages are sent as files named <from_pid>-to-<to_pid>.msg.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define VS_SESSION_DIR "~/.vibesolaris/sessions"

/* ---- Internal helpers ---- */

static char *cs_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int cs_ensure_dir(const char *path)
{
    char cmd[VS_MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null", path);
    return system(cmd);
}

static int cs_is_process_alive(long pid)
{
    /* Check if process exists */
    return (kill(pid, 0) == 0);
}

/* ---- Public API ---- */

void vs_cross_session_init(VSCrossSessionStore *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
    store->my_pid = (long)getpid();

    /* Create registration directory */
    char dir[VS_MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s" VS_SESSION_DIR, home);
    cs_ensure_dir(dir);

    /* Register this instance */
    snprintf(store->register_path, sizeof(store->register_path),
             "%s/%ld", dir, store->my_pid);
    FILE *f = fopen(store->register_path, "w");
    if (f) {
        fprintf(f, "{\"pid\":%ld,\"timestamp\":%ld}\n",
                store->my_pid, (long)time(NULL));
        fclose(f);
    }
}

int vs_cross_session_discover(VSCrossSessionStore *store)
{
    if (!store) return 0;

    char dir[VS_MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s" VS_SESSION_DIR, home);

    DIR *d = opendir(dir);
    if (!d) return 0;

    /* Clear stale peers */
    for (int i = 0; i < store->peer_count; i++) {
        store->peers[i].active = 0;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && store->peer_count < VS_MAX_SESSION_PEERS) {
        /* Parse PID from filename */
        char *endp;
        long pid = strtol(entry->d_name, &endp, 10);
        if (*endp != '\0' || pid <= 0) continue;  /* Not a PID file */
        if (pid == store->my_pid) continue;  /* Skip self */

        /* Check if process is alive */
        if (!cs_is_process_alive(pid)) continue;

        /* Find or create peer entry */
        int found = 0;
        for (int i = 0; i < store->peer_count; i++) {
            if (store->peers[i].pid == pid) {
                store->peers[i].active = 1;
                store->peers[i].last_seen = (long)time(NULL);
                found = 1;
                break;
            }
        }
        if (!found && store->peer_count < VS_MAX_SESSION_PEERS) {
            VSSessionPeer *peer = &store->peers[store->peer_count];
            peer->pid = pid;
            peer->active = 1;
            peer->last_seen = (long)time(NULL);
            snprintf(peer->id, sizeof(peer->id), "vibesolaris-%ld", pid);
            store->peer_count++;
        }
    }
    closedir(d);

    /* Clean up inactive peers */
    int write_idx = 0;
    for (int i = 0; i < store->peer_count; i++) {
        if (store->peers[i].active) {
            if (write_idx != i)
                store->peers[write_idx] = store->peers[i];
            write_idx++;
        }
    }
    store->peer_count = write_idx;

    return store->peer_count;
}

int vs_cross_session_send(VSCrossSessionStore *store, long to_pid, const char *message)
{
    if (!store || !message || to_pid <= 0) return -1;

    char dir[VS_MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s" VS_SESSION_DIR, home);

    char msg_path[VS_MAX_PATH];
    snprintf(msg_path, sizeof(msg_path),
             "%s/%ld-to-%ld.msg", dir, store->my_pid, to_pid);

    FILE *f = fopen(msg_path, "w");
    if (!f) return -1;

    fprintf(f, "{\"from\":%ld,\"to\":%ld,\"timestamp\":%ld,\"message\":\"",
            store->my_pid, to_pid, (long)time(NULL));
    /* Simple JSON escaping */
    for (const char *p = message; *p; p++) {
        if (*p == '"') fprintf(f, "\\\"");
        else if (*p == '\\') fprintf(f, "\\\\");
        else if (*p == '\n') fprintf(f, "\\n");
        else if (*p == '\r') fprintf(f, "\\r");
        else if (*p == '\t') fprintf(f, "\\t");
        else fputc(*p, f);
    }
    fprintf(f, "\"}\n");
    fclose(f);

    return 0;
}

int vs_cross_session_receive(VSCrossSessionStore *store)
{
    if (!store) return 0;

    char dir[VS_MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s" VS_SESSION_DIR, home);

    DIR *d = opendir(dir);
    if (!d) return 0;

    int new_messages = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        /* Look for *-to-<my_pid>.msg files */
        char expected[64];
        snprintf(expected, sizeof(expected), "-to-%ld.msg", store->my_pid);
        if (strstr(entry->d_name, expected) == NULL) continue;

        char msg_path[VS_MAX_PATH];
        snprintf(msg_path, sizeof(msg_path), "%s/%s", dir, entry->d_name);

        FILE *f = fopen(msg_path, "r");
        if (!f) continue;

        char buf[VS_SESSION_MESSAGE_MAX];
        if (fgets(buf, sizeof(buf), f)) {
            fclose(f);

            /* Extract from_pid and message */
            const char *from_str = strstr(buf, "\"from\":");
            const char *msg_str = strstr(buf, "\"message\":\"");
            if (!from_str || !msg_str) continue;

            long from_pid = strtol(from_str + 7, NULL, 10);
            msg_str += 11;  /* Skip '"message":"' */
            const char *msg_end = strrchr(msg_str, '"');
            if (!msg_end) continue;

            /* Store in inbox */
            if (store->inbox_count < VS_MAX_SESSION_MESSAGES) {
                VSSessionMessage *msg = &store->inbox[store->inbox_count];
                msg->from_pid = from_pid;
                msg->timestamp = (long)time(NULL);
                size_t mlen = (size_t)(msg_end - msg_str);
                if (mlen >= sizeof(msg->message)) mlen = sizeof(msg->message) - 1;
                memcpy(msg->message, msg_str, mlen);
                msg->message[mlen] = 0;
                store->inbox_count++;
                new_messages++;
            }
        } else {
            fclose(f);
        }

        /* Remove processed message file */
        unlink(msg_path);
    }
    closedir(d);

    return new_messages;
}

char *vs_cross_session_list(VSCrossSessionStore *store, int max_lines)
{
    if (!store) return cs_dupstr("No cross-session store initialized");

    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    pos += snprintf(buf + pos, 4096 - pos,
                    "Sessions: %d peers discovered (my PID: %ld)\n\n",
                    store->peer_count, store->my_pid);
    lines++;

    if (store->peer_count == 0) {
        pos += snprintf(buf + pos, 4096 - pos, "No other sessions found.\n");
        return buf;
    }

    for (int i = 0; i < store->peer_count && lines < max_lines; i++) {
        VSSessionPeer *peer = &store->peers[i];
        char time_str[64];
        time_t t = (time_t)peer->last_seen;
        struct tm tm_buf;
        if (localtime_r(&t, &tm_buf)) {
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
        } else {
            snprintf(time_str, sizeof(time_str), "%ld", (long)t);
        }
        pos += snprintf(buf + pos, 4096 - pos,
                        "  [PID:%ld] %s (last seen: %s)\n",
                        peer->pid, peer->id, time_str);
        if (pos >= 4080) break;
        lines++;
    }

    return buf;
}

char *vs_cross_session_inbox(VSCrossSessionStore *store, int max_lines)
{
    if (!store) return cs_dupstr("No cross-session store initialized");

    char *buf = (char *)malloc(8192);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    pos += snprintf(buf + pos, 8192 - pos,
                    "Inbox: %d messages\n\n", store->inbox_count);
    lines++;

    if (store->inbox_count == 0) {
        pos += snprintf(buf + pos, 8192 - pos, "No messages.\n");
        return buf;
    }

    for (int i = 0; i < store->inbox_count && lines < max_lines; i++) {
        VSSessionMessage *msg = &store->inbox[i];
        char time_str[64];
        time_t t = (time_t)msg->timestamp;
        struct tm tm_buf;
        if (localtime_r(&t, &tm_buf)) {
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
        } else {
            snprintf(time_str, sizeof(time_str), "%ld", (long)t);
        }
        pos += snprintf(buf + pos, 8192 - pos,
                        "  [PID:%ld] %s: %s\n",
                        msg->from_pid, time_str, msg->message);
        if (pos >= 8000) break;
        lines++;
    }

    return buf;
}

void vs_cross_session_shutdown(VSCrossSessionStore *store)
{
    if (!store) return;
    /* Remove registration file */
    if (strlen(store->register_path) > 0) {
        unlink(store->register_path);
    }
    store->peer_count = 0;
}
