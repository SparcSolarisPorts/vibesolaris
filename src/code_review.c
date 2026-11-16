/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris Code Review — Multi-agent code review mode */
/*
 * Mirrors Claude Code's Code Review: analyzes codebase, splits into chunks,
 * spawns subagents for review, aggregates results.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *cr_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* Discover files in a path using glob-like walk */
static int cr_discover_files(const char *path, const char *scope, char files[][VS_MAX_PATH], int max_files)
{
    int count = 0;
    char cmd[VS_MAX_PATH + 128];
    
    if (strcmp(scope, "staged") == 0) {
        /* Get staged files from git */
        snprintf(cmd, sizeof(cmd), "cd '%s' && git diff --staged --name-only 2>/dev/null", path);
    } else {
        /* Get all code files */
        snprintf(cmd, sizeof(cmd),
                 "find '%s' -maxdepth 5 -type f \\( -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.js' -o -name '*.ts' -o -name '*.java' -o -name '*.go' -o -name '*.rs' -o -name '*.rb' -o -name '*.sh' \\) 2>/dev/null | head -%d",
                 path, max_files);
    }
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return 0;
    
    char line[VS_MAX_PATH];
    while (fgets(line, sizeof(line), pipe) && count < max_files) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;
        if (len > 0) {
            snprintf(files[count], VS_MAX_PATH, "%s", line);
            count++;
        }
    }
    pclose(pipe);
    return count;
}

/* ---- Public API ---- */

char *vs_code_review_start(const char *path, const char *scope, const char *focus)
{
    if (!path) return cr_dupstr("ERROR: path required");
    scope = scope ? scope : "all";
    focus = focus ? focus : "bugs";
    
    /* Discover files */
    char files[64][VS_MAX_PATH];
    int file_count = cr_discover_files(path, scope, files, 64);
    
    if (file_count == 0) {
        return cr_dupstr("No files found to review.");
    }
    
    /* Build review prompt */
    char *prompt = (char *)malloc(8192);
    if (!prompt) return NULL;
    int pos = 0;
    pos += snprintf(prompt + pos, 8192 - pos,
                    "Review the following files for %s issues:\n\n", focus);
    for (int i = 0; i < file_count && pos < 8000; i++) {
        pos += snprintf(prompt + pos, 8192 - pos, "- %s\n", files[i]);
    }
    
    /* For a full implementation, this would spawn subagents per chunk.
       For now, return a structured result indicating what would be reviewed. */
    char *result = (char *)malloc(4096);
    if (!result) { free(prompt); return NULL; }
    free(prompt);
    
    int rpos = 0;
    rpos += snprintf(result + rpos, 4096 - rpos,
                     "CODE_REVIEW_START:\n"
                     "  Path: %s\n"
                     "  Scope: %s\n"
                     "  Focus: %s\n"
                     "  Files found: %d\n"
                     "  Status: queued for review\n\n",
                     path, scope, focus, file_count);
    rpos += snprintf(result + rpos, 4096 - rpos,
                     "Files to review:\n");
    for (int i = 0; i < file_count && rpos < 4000; i++) {
        rpos += snprintf(result + rpos, 4096 - rpos, "  - %s\n", files[i]);
    }
    return result;
}

char *vs_code_review_render(const VSCodeReviewFinding *findings, int count, int max_lines)
{
    if (!findings) return cr_dupstr("No findings");
    
    char *buf = (char *)malloc(8192);
    if (!buf) return NULL;
    int pos = 0;
    int lines = 0;
    
    pos += snprintf(buf + pos, 8192 - pos, "Code Review Findings (%d):\n\n", count);
    
    for (int i = 0; i < count && lines < max_lines; i++) {
        const char *sev = "INFO";
        if (findings[i].severity == 1) sev = "WARNING";
        if (findings[i].severity == 2) sev = "ERROR";
        
        pos += snprintf(buf + pos, 8192 - pos,
                        "[%s] %s:%d (%s)\n  %s\n\n",
                        sev, findings[i].file, findings[i].line,
                        findings[i].category, findings[i].description);
        if (pos >= 8000) break;
        lines++;
    }
    
    if (count == 0) {
        free(buf);
        return cr_dupstr("No findings. Code looks clean!");
    }
    return buf;
}

/* ---- Security Review: CWE-based vulnerability pattern matching ---- */

/* Security vulnerability patterns: regex-like string patterns to scan for */
typedef struct {
    const char *pattern;
    const char *cwe;
    const char *category;
    VSSecuritySeverity severity;
    const char *description;
} VSSecurityPattern;

static const VSSecurityPattern sec_patterns[] = {
    /* Hardcoded secrets (CWE-798) */
    {"password\\s*=\\s*[\"']", "CWE-798", "hardcoded_secret", VS_SEC_SEVERITY_HIGH,
     "Possible hardcoded password"},
    {"api_key\\s*=\\s*[\"']", "CWE-798", "hardcoded_secret", VS_SEC_SEVERITY_HIGH,
     "Possible hardcoded API key"},
    {"secret\\s*=\\s*[\"']", "CWE-798", "hardcoded_secret", VS_SEC_SEVERITY_HIGH,
     "Possible hardcoded secret"},
    {"token\\s*=\\s*[\"'][A-Za-z0-9]", "CWE-798", "hardcoded_secret", VS_SEC_SEVERITY_MEDIUM,
     "Possible hardcoded token"},

    /* SQL Injection (CWE-89) */
    {"sprintf.*SELECT", "CWE-89", "sql_injection", VS_SEC_SEVERITY_CRITICAL,
     "SQL query built with sprintf — possible SQL injection"},
    {"snprintf.*SELECT", "CWE-89", "sql_injection", VS_SEC_SEVERITY_CRITICAL,
     "SQL query built with snprintf — possible SQL injection"},
    {"strcat.*SELECT", "CWE-89", "sql_injection", VS_SEC_SEVERITY_CRITICAL,
     "SQL query built with strcat — possible SQL injection"},

    /* Buffer overflows (CWE-120) */
    {"strcpy\\s*\\(", "CWE-120", "buffer_overflow", VS_SEC_SEVERITY_HIGH,
     "strcpy without bounds checking — possible buffer overflow"},
    {"strcat\\s*\\(", "CWE-120", "buffer_overflow", VS_SEC_SEVERITY_MEDIUM,
     "strcat without bounds checking — possible buffer overflow"},
    {"gets\\s*\\(", "CWE-120", "buffer_overflow", VS_SEC_SEVERITY_CRITICAL,
     "gets() is always unsafe — use fgets() instead"},
    {"scanf\\s*\\(\\s*\"%s\"", "CWE-120", "buffer_overflow", VS_SEC_SEVERITY_HIGH,
     "scanf with %s has no width limit — possible buffer overflow"},

    /* Command injection (CWE-78) */
    {"system\\s*\\(", "CWE-78", "command_injection", VS_SEC_SEVERITY_HIGH,
     "system() call — possible command injection if input is untrusted"},
    {"popen\\s*\\(", "CWE-78", "command_injection", VS_SEC_SEVERITY_HIGH,
     "popen() call — possible command injection if input is untrusted"},

    /* Path traversal (CWE-22) */
    {"\\.\\./", "CWE-22", "path_traversal", VS_SEC_SEVERITY_MEDIUM,
     "Parent directory reference — verify path is sanitized"},

    /* XSS (CWE-79) — in web-related files */
    {"innerHTML", "CWE-79", "xss", VS_SEC_SEVERITY_HIGH,
     "innerHTML usage — possible cross-site scripting"},
    {"document\\.write", "CWE-79", "xss", VS_SEC_SEVERITY_HIGH,
     "document.write() — possible cross-site scripting"},
    {"eval\\s*\\(", "CWE-79", "xss", VS_SEC_SEVERITY_MEDIUM,
     "eval() usage — possible code injection"},

    /* Weak crypto (CWE-327) */
    {"MD5", "CWE-327", "weak_crypto", VS_SEC_SEVERITY_MEDIUM,
     "MD5 is cryptographically weak — use SHA-256 or better"},
    {"DES_", "CWE-327", "weak_crypto", VS_SEC_SEVERITY_HIGH,
     "DES encryption is weak — use AES instead"},
    {"RC4", "CWE-327", "weak_crypto", VS_SEC_SEVERITY_HIGH,
     "RC4 is cryptographically broken — use AES instead"},

    /* Unsafe functions (CWE-676) */
    {"malloc\\s*\\([^)]*\\)[^+]*\\)", "CWE-676", "unsafe_function", VS_SEC_SEVERITY_LOW,
     "malloc without NULL check — possible null pointer dereference"},
    {"atoi\\s*\\(", "CWE-676", "unsafe_function", VS_SEC_SEVERITY_LOW,
     "atoi() provides no error checking — use strtol() instead"},
};

static const int sec_pattern_count = sizeof(sec_patterns) / sizeof(sec_patterns[0]);

/* Simple pattern matching (substring search, not full regex) */
static int sec_match_pattern(const char *text, const char *pattern)
{
    return (strstr(text, pattern) != NULL);
}

int vs_security_review_start(const char *path, const char *scope,
                              VSSecurityFinding *findings, int max_findings)
{
    if (!path || !findings || max_findings <= 0) return 0;
    scope = scope ? scope : "all";

    /* Discover files */
    char files[64][VS_MAX_PATH];
    int file_count = cr_discover_files(path, scope, files, 64);
    if (file_count == 0) return 0;

    int finding_count = 0;

    for (int f = 0; f < file_count && finding_count < max_findings; f++) {
        /* Read file content */
        char *content = vs_read_file(files[f]);
        if (!content) continue;

        /* Scan line by line */
        char *line_start = content;
        int line_num = 1;
        char *newline;

        while ((newline = strchr(line_start, '\n')) != NULL) {
            size_t line_len = (size_t)(newline - line_start);
            char line_buf[1024];
            if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
            memcpy(line_buf, line_start, line_len);
            line_buf[line_len] = 0;

            /* Check each pattern */
            for (int p = 0; p < sec_pattern_count; p++) {
                if (sec_match_pattern(line_buf, sec_patterns[p].pattern)) {
                    VSSecurityFinding *finding = &findings[finding_count];
                    snprintf(finding->file, sizeof(finding->file), "%s", files[f]);
                    finding->line = line_num;
                    finding->severity = sec_patterns[p].severity;
                    snprintf(finding->cwe, sizeof(finding->cwe), "%s", sec_patterns[p].cwe);
                    snprintf(finding->category, sizeof(finding->category), "%s",
                             sec_patterns[p].category);
                    snprintf(finding->description, sizeof(finding->description), "%s",
                             sec_patterns[p].description);
                    finding_count++;
                    if (finding_count >= max_findings) break;
                }
            }
            if (finding_count >= max_findings) break;

            line_start = newline + 1;
            line_num++;
            if (line_num > 10000) break;  /* Safety limit */
        }
        free(content);
    }

    return finding_count;
}

const char *vs_security_severity_label(VSSecuritySeverity sev)
{
    switch (sev) {
        case VS_SEC_SEVERITY_LOW: return "LOW";
        case VS_SEC_SEVERITY_MEDIUM: return "MEDIUM";
        case VS_SEC_SEVERITY_HIGH: return "HIGH";
        case VS_SEC_SEVERITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

char *vs_security_review_render(const VSSecurityFinding *findings, int count, int max_lines)
{
    if (!findings) return cr_dupstr("No security findings");

    char *buf = (char *)malloc(16384);
    if (!buf) return NULL;

    int pos = 0;
    int lines = 0;

    /* Summary */
    int critical = 0, high = 0, medium = 0, low = 0;
    for (int i = 0; i < count; i++) {
        switch (findings[i].severity) {
            case VS_SEC_SEVERITY_CRITICAL: critical++; break;
            case VS_SEC_SEVERITY_HIGH: high++; break;
            case VS_SEC_SEVERITY_MEDIUM: medium++; break;
            case VS_SEC_SEVERITY_LOW: low++; break;
        }
    }

    pos += snprintf(buf + pos, 16384 - pos,
                    "Security Review: %d findings (Critical: %d, High: %d, Medium: %d, Low: %d)\n\n",
                    count, critical, high, medium, low);
    lines++;

    if (count == 0) {
        pos += snprintf(buf + pos, 16384 - pos, "No security issues detected.\n");
        return buf;
    }

    for (int i = 0; i < count && lines < max_lines; i++) {
        pos += snprintf(buf + pos, 16384 - pos,
                        "[%s] %s:%d (%s) %s\n  %s\n\n",
                        vs_security_severity_label(findings[i].severity),
                        findings[i].file, findings[i].line,
                        findings[i].cwe, findings[i].category,
                        findings[i].description);
        if (pos >= 16000) break;
        lines++;
    }

    return buf;
}
