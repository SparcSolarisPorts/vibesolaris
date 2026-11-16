/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Checkpoints — Automatic git snapshots before edits */
/*
 * Mirrors Claude Code's checkpoint system: automatically snapshots git
 * state before file-modifying tools, allows rollback via Escape x2 or
 * /checkpoint rollback command.
 *
 * Uses git stash for untracked+tracked changes, with LRU eviction at
 * 16 checkpoints max. Falls back gracefully when not in a git repo.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define VS_CHECKPOINT_DIR "~/.vibesolaris/checkpoints"
#define VS_CHECKPOINT_FILE "checkpoints.jsonl"
#define VS_CHECKPOINT_MAX_AGE 300  /* 5 minutes in seconds */

/* ---- Internal helpers ---- */

static char *cp_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int cp_home_dir(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, cap, "%s" VS_CHECKPOINT_DIR, home);
    return 0;
}

static int cp_ensure_dir(void)
{
    char dir[VS_MAX_PATH];
    cp_home_dir(dir, sizeof(dir));
    /* mkdir -p equivalent */
    char cmd[VS_MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null", dir);
    return system(cmd);
}

static int cp_is_git_repo(const char *path)
{
    /* Check if path (or ancestor) contains .git */
    char cmd[VS_MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd),
             "cd '%s' 2>/dev/null && git rev-parse --git-dir 2>/dev/null", path);
    int ret = system(cmd);
    return (ret == 0) ? 1 : 0;
}

static int cp_git_stash(const char *desc, char *stash_ref, size_t ref_cap)
{
    /* Stash all changes (including untracked) */
    char cmd[VS_MAX_PATH + 128];
    snprintf(cmd, sizeof(cmd),
             "git stash push -u -m '%s' 2>/dev/null && "
             "git rev-parse HEAD 2>/dev/null",
             desc ? desc : "vibesolaris checkpoint");
    int ret = system(cmd);
    if (ret == 0 && stash_ref) {
        /* Get the current HEAD ref */
        FILE *pipe = popen("git rev-parse HEAD 2>/dev/null", "r");
        if (pipe) {
            char buf[256];
            if (fgets(buf, sizeof(buf), pipe)) {
                size_t len = strlen(buf);
                while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                    buf[--len] = 0;
                snprintf(stash_ref, ref_cap, "%s", buf);
            }
            pclose(pipe);
        }
    }
    return (ret == 0) ? 0 : -1;
}

static int cp_git_stash_pop(void)
{
    /* Pop the most recent stash */
    int ret = system("git stash pop 2>/dev/null");
    return (ret == 0) ? 0 : -1;
}

static int cp_git_status_dirty(void)
{
    /* Check if working tree has uncommitted changes */
    int ret = system("git diff --quiet 2>/dev/null || git diff --cached --quiet 2>/dev/null || exit 0");
    /* system() returns exit code << 8; 0 = clean, non-zero = dirty */
    return (ret != 0) ? 1 : 0;
}

/* ---- Public API ---- */

void vs_checkpoint_init(VSCheckpointStore *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));

    char dir[VS_MAX_PATH];
    cp_home_dir(dir, sizeof(dir));
    snprintf(store->dir, sizeof(store->dir), "%s", dir);
    snprintf(store->file, sizeof(store->file), "%s/" VS_CHECKPOINT_FILE, dir);

    cp_ensure_dir();

    /* Check if current dir is a git repo */
    store->is_git_repo = cp_is_git_repo(".");

    /* Load existing checkpoints */
    FILE *f = fopen(store->file, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f) && store->count < VS_MAX_CHECKPOINTS) {
            VSCheckpointEntry *e = &store->entries[store->count];
            /* Parse JSONL: {"id":"...","timestamp":...,"git_ref":"...","description":"..."} */
            const char *s;
            if ((s = strstr(line, "\"id\":\""))) {
                s += 6;
                char *end = strchr(s, '"');
                if (end) {
                    size_t len = (size_t)(end - s);
                    if (len >= sizeof(e->id)) len = sizeof(e->id) - 1;
                    memcpy(e->id, s, len);
                    e->id[len] = 0;
                }
            }
            if ((s = strstr(line, "\"timestamp\":"))) {
                s += 12;
                e->timestamp = atol(s);
            }
            if ((s = strstr(line, "\"git_ref\":\""))) {
                s += 11;
                char *end = strchr(s, '"');
                if (end) {
                    size_t len = (size_t)(end - s);
                    if (len >= sizeof(e->git_ref)) len = sizeof(e->git_ref) - 1;
                    memcpy(e->git_ref, s, len);
                    e->git_ref[len] = 0;
                }
            }
            if ((s = strstr(line, "\"description\":\""))) {
                s += 15;
                char *end = strchr(s, '"');
                if (end) {
                    size_t len = (size_t)(end - s);
                    if (len >= sizeof(e->description)) len = sizeof(e->description) - 1;
                    memcpy(e->description, s, len);
                    e->description[len] = 0;
                }
            }
            store->count++;
        }
        fclose(f);
    }
}

int vs_checkpoint_create(VSCheckpointStore *store, const char *desc)
{
    if (!store || !store->is_git_repo) return -1;

    /* Check if working tree is dirty */
    if (!cp_git_status_dirty()) {
        /* Nothing to checkpoint */
        return 0;
    }

    /* Evict oldest if at capacity */
    if (store->count >= VS_MAX_CHECKPOINTS) {
        /* Shift all entries left by 1 (drop oldest) */
        memmove(&store->entries[0], &store->entries[1],
                (sizeof(VSCheckpointEntry) * (VS_MAX_CHECKPOINTS - 1)));
        store->count--;
    }

    /* Create the stash */
    char git_ref[256];
    if (cp_git_stash(desc, git_ref, sizeof(git_ref)) != 0) {
        return -1;
    }

    /* Record the checkpoint */
    VSCheckpointEntry *e = &store->entries[store->count];
    time_t now = time(NULL);
    snprintf(e->id, sizeof(e->id), "cp_%ld", (long)now);
    e->timestamp = (long)now;
    snprintf(e->git_ref, sizeof(e->git_ref), "%s", git_ref);
    snprintf(e->description, sizeof(e->description), "%s", desc ? desc : "auto checkpoint");
    store->count++;

    /* Persist */
    vs_checkpoint_persist(store);

    return store->count - 1;
}

int vs_checkpoint_rollback(VSCheckpointStore *store, const char *id)
{
    if (!store || !store->is_git_repo) return -1;

    /* Find the checkpoint */
    int idx = -1;
    if (id) {
        for (int i = 0; i < store->count; i++) {
            if (strcmp(store->entries[i].id, id) == 0) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) {
        /* Rollback to most recent */
        idx = store->count - 1;
    }
    if (idx < 0 || idx >= store->count) return -1;

    /* Pop the stash to restore changes */
    int ret = cp_git_stash_pop();
    if (ret == 0) {
        /* Remove the rolled-back checkpoint */
        memmove(&store->entries[idx], &store->entries[idx + 1],
                (sizeof(VSCheckpointEntry) * (store->count - idx - 1)));
        store->count--;
        vs_checkpoint_persist(store);
    }
    return ret;
}

char *vs_checkpoint_list(VSCheckpointStore *store, int max_lines)
{
    if (!store) return cp_dupstr("No checkpoint store initialized");

    if (store->count == 0) {
        if (store->is_git_repo)
            return cp_dupstr("No checkpoints. Changes will be auto-snapshotted before edits.");
        else
            return cp_dupstr("Checkpoints disabled: not a git repository.");
    }

    /* Build list output */
    char *buf = (char *)malloc(4096);
    if (!buf) return NULL;
    int pos = 0;
    int lines = 0;
    pos += snprintf(buf + pos, 4096 - pos,
                    "Checkpoints (%d/%d, git repo: %s):\n",
                    store->count, VS_MAX_CHECKPOINTS,
                    store->is_git_repo ? "yes" : "no");
    for (int i = 0; i < store->count && lines < max_lines; i++) {
        VSCheckpointEntry *e = &store->entries[i];
        char time_str[64];
        time_t t = (time_t)e->timestamp;
        struct tm tm_buf;
        if (localtime_r(&t, &tm_buf)) {
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
        } else {
            snprintf(time_str, sizeof(time_str), "%ld", (long)t);
        }
        pos += snprintf(buf + pos, 4096 - pos,
                        "  [%s] %s — %s\n", e->id, time_str, e->description);
        if (pos >= 4080) break;
        lines++;
    }
    return buf;
}

int vs_checkpoint_auto_before_edit(VSCheckpointStore *store)
{
    if (!store || !store->is_git_repo) return 0;

    /* Check if recent checkpoint exists (within VS_CHECKPOINT_MAX_AGE seconds) */
    time_t now = time(NULL);
    for (int i = store->count - 1; i >= 0; i--) {
        long age = (long)(now - store->entries[i].timestamp);
        if (age >= 0 && age < VS_CHECKPOINT_MAX_AGE) {
            /* Recent checkpoint exists, skip */
            return 0;
        }
    }

    /* No recent checkpoint, create one */
    return vs_checkpoint_create(store, "auto before edit");
}

int vs_checkpoint_persist(VSCheckpointStore *store)
{
    if (!store) return -1;

    cp_ensure_dir();

    FILE *f = fopen(store->file, "w");
    if (!f) return -1;

    for (int i = 0; i < store->count; i++) {
        VSCheckpointEntry *e = &store->entries[i];
        fprintf(f, "{\"id\":\"%s\",\"timestamp\":%ld,\"git_ref\":\"%s\",\"description\":\"%s\"}\n",
                e->id, e->timestamp, e->git_ref, e->description);
    }
    fclose(f);
    return 0;
}

void vs_checkpoint_clear(VSCheckpointStore *store)
{
    if (!store) return;
    store->count = 0;
    store->is_git_repo = 0;
    vs_checkpoint_persist(store);
}
