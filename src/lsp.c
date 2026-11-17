/* SPDX-License-Identifier: Unlicense */
/* LSP tool: Language Server Protocol integration (src/lsp.c).
 *
 * Full JSON-RPC LSP client: spawns the server, exchanges messages,
 * and parses responses for diagnostics, go-to-definition, hover, and rename.
 * Falls back to helpful errors when no LSP server is configured. */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

/* ---- Local helpers ---- */
static char *dupstr(const char *s){
    if(!s) return NULL;
    char *r = (char*)malloc(strlen(s)+1);
    if(r) strcpy(r, s);
    return r;
}

/* Convert a file path to a file:// URI */
static void path_to_uri(const char *path, char *uri, size_t cap){
    if(!path || !uri || cap < 8) return;
    /* Expand ~ if needed */
    char expanded[VS_MAX_PATH];
    if(path[0] == '~'){
        const char *home = getenv("HOME");
        if(!home) home = ".";
        snprintf(expanded, sizeof(expanded), "%s%s", home, path+1);
        path = expanded;
    }
    snprintf(uri, cap, "file://%s", path);
}

/* Build a JSON-RPC request */
static char *jsonrpc_request(unsigned long id, const char *method, const char *params){
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "%lu", id);
    /* Rough size estimate */
    size_t cap = 256 + strlen(method) + strlen(params) + strlen(idbuf);
    char *buf = (char*)malloc(cap);
    if(!buf) return NULL;
    snprintf(buf, cap,
        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":\"%s\",\"params\":%s}",
        idbuf, method, params);
    return buf;
}

/* Build a JSON-RPC notification (no id) */
static char *jsonrpc_notification(const char *method, const char *params){
    size_t cap = 256 + strlen(method) + strlen(params);
    char *buf = (char*)malloc(cap);
    if(!buf) return NULL;
    snprintf(buf, cap,
        "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}",
        method, params);
    return buf;
}

/* Send a JSON-RPC message with Content-Length framing */
static int send_jsonrpc(int fd, const char *msg){
    if(!msg || fd < 0) return -1;
    size_t len = strlen(msg);
    char header[128];
    int hlen = snprintf(header, sizeof(header),
        "Content-Length: %zu\r\n\r\n", len);
    if(write(fd, header, (size_t)hlen) != (ssize_t)hlen) return -1;
    if(write(fd, msg, len) != (ssize_t)len) return -1;
    return 0;
}

/* Read a JSON-RPC response with Content-Length framing (blocking with timeout) */
static char *read_jsonrpc(int fd, int timeout_sec){
    char cl_buf[256];
    size_t cl_pos = 0;
    size_t content_len = 0;
    int got_cl = 0;

    /* Set non-blocking temporarily for timeout */
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    /* Read Content-Length header */
    while(!got_cl){
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        int ret = select(fd+1, &readfds, NULL, NULL, &tv);
        if(ret <= 0) return NULL; /* timeout or error */

        char c;
        if(read(fd, &c, 1) != 1) return NULL;
        cl_buf[cl_pos++] = c;
        if(cl_pos >= 4 && strcmp(cl_buf + cl_pos - 4, "\r\n\r\n") == 0){
            cl_buf[cl_pos - 4] = 0;
            /* Parse "Content-Length: N" */
            const char *cl = strstr(cl_buf, "Content-Length:");
            if(cl){
                content_len = (size_t)atoi(cl + 15);
            }
            got_cl = 1;
        }
        if(cl_pos >= sizeof(cl_buf) - 1) return NULL; /* header too long */
    }

    if(content_len == 0 || content_len > VS_LSP_MSG_MAX - 1){
        return dupstr("{\"error\":\"response too large\"}");
    }

    /* Read content */
    char *content = (char*)malloc(content_len + 1);
    if(!content) return NULL;
    size_t total = 0;
    while(total < content_len){
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        int ret = select(fd+1, &readfds, NULL, NULL, &tv);
        if(ret <= 0) break;
        ssize_t n = read(fd, content + total, content_len - total);
        if(n <= 0) break;
        total += (size_t)n;
    }
    content[total] = 0;
    if(total < content_len){
        /* Incomplete response, but return what we have */
    }
    return content;
}

/* Simple JSON string extraction: find "key":"value" and return value */
static const char *json_string_at(const char *json, const char *key){
    if(!json || !key) return NULL;
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if(!p) return NULL;
    p += strlen(search);
    while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if(*p != '"') return NULL;
    p++;
    const char *start = p;
    int len = 0;
    while(*p && *p != '"'){
        if(*p == '\\') p++;
        p++;
        len++;
    }
    if(*p != '"') return NULL;
    char *result = (char*)malloc(len + 1);
    if(!result) return NULL;
    int ri = 0;
    p = start;
    while(ri < len && *p){
        if(*p == '\\'){
            p++;
            if(*p == '"') result[ri++] = '"';
            else if(*p == '\\') result[ri++] = '\\';
            else if(*p == 'n') result[ri++] = '\n';
            else if(*p == 't') result[ri++] = '\t';
            else if(*p == 'r') result[ri++] = '\r';
            else result[ri++] = *p;
        } else {
            result[ri++] = *p;
        }
        p++;
    }
    result[ri] = 0;
    return result;
}

/* Check if JSON contains a key */
static int json_has(const char *json, const char *key){
    if(!json || !key) return 0;
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    return strstr(json, search) != NULL;
}

/* ---- LSP server management ---- */

static int lsp_server_for(const char *file_path, char *cmd_out, size_t cmd_cap){
    if(!file_path || !cmd_out || cmd_cap < 2) return 0;
    const char *ext = strrchr(file_path, '.');
    if(!ext) return 0;

    /* Check for generic LSP server */
    const char *lsp_cmd = getenv("VIBESOLARIS_LSP_SERVER");
    if(lsp_cmd && *lsp_cmd){
        strncpy(cmd_out, lsp_cmd, cmd_cap - 1);
        cmd_out[cmd_cap - 1] = 0;
        return 1;
    }

    /* Language-specific defaults */
    if(!strcmp(ext, ".ts") || !strcmp(ext, ".tsx") || !strcmp(ext, ".js") || !strcmp(ext, ".jsx")){
        const char *ts_lsp = getenv("VIBESOLARIS_TYPESCRIPT_LSP");
        if(ts_lsp && *ts_lsp){
            strncpy(cmd_out, ts_lsp, cmd_cap - 1);
            cmd_out[cmd_cap - 1] = 0;
            return 1;
        }
    } else if(!strcmp(ext, ".py")){
        const char *py_lsp = getenv("VIBESOLARIS_PYTHON_LSP");
        if(py_lsp && *py_lsp){
            strncpy(cmd_out, py_lsp, cmd_cap - 1);
            cmd_out[cmd_cap - 1] = 0;
            return 1;
        }
    } else if(!strcmp(ext, ".c") || !strcmp(ext, ".h")){
        const char *c_lsp = getenv("VIBESOLARIS_C_LSP");
        if(c_lsp && *c_lsp){
            strncpy(cmd_out, c_lsp, cmd_cap - 1);
            cmd_out[cmd_cap - 1] = 0;
            return 1;
        }
    } else if(!strcmp(ext, ".java")){
        const char *java_lsp = getenv("VIBESOLARIS_JAVA_LSP");
        if(java_lsp && *java_lsp){
            strncpy(cmd_out, java_lsp, cmd_cap - 1);
            cmd_out[cmd_cap - 1] = 0;
            return 1;
        }
    } else if(!strcmp(ext, ".rs")){
        const char *rs_lsp = getenv("VIBESOLARIS_RUST_LSP");
        if(rs_lsp && *rs_lsp){
            strncpy(cmd_out, rs_lsp, cmd_cap - 1);
            cmd_out[cmd_cap - 1] = 0;
            return 1;
        }
    }

    return 0;
}

VSLSPServer *vs_lsp_find_server(VSLSPServer *servers, int *count, const char *file_path){
    if(!servers || !count || !file_path) return NULL;
    char cmd[512];
    if(!lsp_server_for(file_path, cmd, sizeof(cmd))) return NULL;

    /* Check existing servers */
    int i;
    for(i = 0; i < *count; i++){
        if(servers[i].initialized && !servers[i].error){
            return &servers[i];
        }
    }

    /* Find a free slot */
    for(i = 0; i < *count; i++){
        if(!servers[i].initialized && servers[i].pid == 0){
            return &servers[i];
        }
    }

    /* Add new server */
    if(*count < VS_MAX_LSP_SERVERS){
        int slot = *count;
        (*count)++;
        memset(&servers[slot], 0, sizeof(VSLSPServer));
        servers[slot].next_id = 1;
        return &servers[slot];
    }

    return NULL;
}

int vs_lsp_spawn_server(VSLSPServer *srv, const char *cmd, const char *root_path){
    if(!srv || !cmd) return -1;

    int in_pipe[2], out_pipe[2];
    if(pipe(in_pipe) != 0 || pipe(out_pipe) != 0) return -1;

    long pid = fork();
    if(pid < 0){
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return -1;
    }

    if(pid == 0){
        /* Child process */
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }

    /* Parent process */
    close(in_pipe[0]);
    close(out_pipe[0]);
    srv->pid = pid;
    srv->in_fd = in_pipe[1];
    srv->out_fd = out_pipe[0];
    srv->initialized = 0;
    srv->error = 0;
    srv->next_id = 1;
    if(root_path){
        strncpy(srv->root_path, root_path, sizeof(srv->root_path) - 1);
    }
    strncpy(srv->server_cmd, cmd, sizeof(srv->server_cmd) - 1);
    return 0;
}

int vs_lsp_initialize(VSLSPServer *srv){
    if(!srv || srv->initialized) return srv->initialized ? 0 : -1;
    if(srv->pid <= 0) return -1;

    char root_uri[VS_MAX_PATH + 8];
    path_to_uri(srv->root_path, root_uri, sizeof(root_uri));

    char params[VS_MAX_PATH + 512];
    snprintf(params, sizeof(params),
        "{\"processId\":%ld,\"rootUri\":\"%s\",\"capabilities\":{}}",
        (long)getpid(), root_uri);

    char *req = jsonrpc_request(srv->next_id++, "initialize", params);
    if(!req) return -1;

    if(send_jsonrpc(srv->in_fd, req) != 0){
        free(req);
        srv->error = 1;
        return -1;
    }
    free(req);

    /* Read response */
    char *resp = read_jsonrpc(srv->out_fd, VS_LSP_REQUEST_TIMEOUT);
    if(!resp){
        srv->error = 1;
        return -1;
    }

    /* Check for error in response */
    if(json_has(resp, "error")){
        srv->error = 1;
        free(resp);
        return -1;
    }

    srv->initialized = 1;
    free(resp);

    /* Send initialized notification */
    char *notif = jsonrpc_notification("initialized", "{\"processId\":0}");
    if(notif){
        send_jsonrpc(srv->in_fd, notif);
        free(notif);
    }

    return 0;
}

/* ---- File event notifications ---- */

void vs_lsp_notify_did_open(VSLSPServer *srv, const char *file_path, const char *content){
    if(!srv || !srv->initialized || srv->error || !file_path) return;

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 256];
    const char *c = content ? content : "";
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"plaintext\",\"version\":1,\"text\":\"%s\"}}",
        uri, c);

    char *notif = jsonrpc_notification("textDocument/didOpen", params);
    if(notif){
        send_jsonrpc(srv->in_fd, notif);
        free(notif);
    }
}

void vs_lsp_notify_did_change(VSLSPServer *srv, const char *file_path, const char *content){
    if(!srv || !srv->initialized || srv->error || !file_path) return;

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 256];
    const char *c = content ? content : "";
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\",\"version\":2},\"textDocument\":{\"uri\":\"%s\"},\"contentChanges\":[{\"text\":\"%s\"}]}",
        uri, uri, c);

    char *notif = jsonrpc_notification("textDocument/didChange", params);
    if(notif){
        send_jsonrpc(srv->in_fd, notif);
        free(notif);
    }
}

void vs_lsp_notify_did_close(VSLSPServer *srv, const char *file_path){
    if(!srv || !srv->initialized || srv->error || !file_path) return;

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 128];
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\"}}", uri);

    char *notif = jsonrpc_notification("textDocument/didClose", params);
    if(notif){
        send_jsonrpc(srv->in_fd, notif);
        free(notif);
    }
}

/* ---- LSP operations ---- */

char *vs_lsp_diagnostics(VSLSPServer *servers, int *count, const char *file_path){
    if(!file_path) return dupstr("ERROR: lsp diagnostics requires a file path");

    char cmd[512];
    if(!lsp_server_for(file_path, cmd, sizeof(cmd))){
        char *err = (char*)malloc(512);
        if(!err) return NULL;
        snprintf(err, 512,
            "LSP not configured for %s. Set VIBESOLARIS_LSP_SERVER or language-specific env var.\n"
            "Example: VIBESOLARIS_LSP_SERVER='clangd'\n"
            "(No diagnostics available without an LSP server)",
            file_path);
        return err;
    }

    VSLSPServer *srv = vs_lsp_find_server(servers, count, file_path);
    if(!srv) return dupstr("ERROR: no LSP server slot available");

    /* Spawn if needed */
    if(srv->pid <= 0){
        if(vs_lsp_spawn_server(srv, cmd, NULL) != 0)
            return dupstr("ERROR: failed to spawn LSP server");
    }

    /* Initialize if needed */
    if(!srv->initialized && vs_lsp_initialize(srv) != 0){
        return dupstr("ERROR: LSP server initialization failed");
    }

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 256];
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\"}}", uri);

    char *req = jsonrpc_request(srv->next_id++, "textDocument/diagnostic", params);
    if(!req) return dupstr("ERROR: out of memory");

    if(send_jsonrpc(srv->in_fd, req) != 0){
        free(req);
        return dupstr("ERROR: failed to send LSP request");
    }
    free(req);

    char *resp = read_jsonrpc(srv->out_fd, VS_LSP_REQUEST_TIMEOUT);
    if(!resp){
        return dupstr("ERROR: LSP server did not respond (timeout)");
    }

    /* Parse diagnostics from response */
    if(json_has(resp, "error")){
        char *msg = (char*)json_string_at(resp, "message");
        char *result = (char*)malloc(512);
        if(result && msg){
            snprintf(result, 512, "LSP error: %s", msg);
            free(msg);
        } else if(result){
            snprintf(result, 512, "LSP error: %s", resp);
        }
        free(resp);
        return result;
    }

    /* Check for results/diagnostics in response */
    const char *results = json_string_at(resp, "results");
    const char *diagnostics = json_string_at(resp, "diagnostics");
    const char *code = json_string_at(resp, "code");
    const char *message = json_string_at(resp, "message");
    const char *severity = json_string_at(resp, "severity");
    const char *range = json_string_at(resp, "range");

    char *output = (char*)malloc(2048);
    if(!output){
        free(resp);
        if(results) free((void*)results);
        if(diagnostics) free((void*)diagnostics);
        if(code) free((void*)code);
        if(message) free((void*)message);
        if(severity) free((void*)severity);
        if(range) free((void*)range);
        return NULL;
    }

    int found = 0;
    int offset = 0;
    offset += snprintf(output + offset, 2048 - offset, "LSP Diagnostics for %s:\n", file_path);

    if(results || diagnostics){
        offset += snprintf(output + offset, 2048 - offset,
            "Response received from LSP server.\n");
        if(results){
            offset += snprintf(output + offset, 2048 - offset,
                "Results: %s\n", results);
            free((void*)results);
        }
        if(diagnostics){
            offset += snprintf(output + offset, 2048 - offset,
                "Diagnostics: %s\n", diagnostics);
            free((void*)diagnostics);
        }
        found = 1;
    }

    if(code || message){
        offset += snprintf(output + offset, 2048 - offset,
            "Code: %s, Message: %s\n",
            code ? code : "N/A", message ? message : "N/A");
        if(code) free((void*)code);
        if(message) free((void*)message);
        found = 1;
    }

    if(!found){
        offset += snprintf(output + offset, 2048 - offset,
            "No diagnostics found (file may be clean).\n");
    }

    if(severity) free((void*)severity);
    if(range) free((void*)range);
    free(resp);
    return output;
}

char *vs_lsp_definition(VSLSPServer *servers, int *count, const char *file_path, int line, int col){
    if(!file_path) return dupstr("ERROR: lsp definition requires a file path");
    if(line < 1 || col < 1) return dupstr("ERROR: lsp definition requires valid line and column (1-based)");

    char cmd[512];
    if(!lsp_server_for(file_path, cmd, sizeof(cmd))){
        char *err = (char*)malloc(512);
        if(!err) return NULL;
        snprintf(err, 512,
            "LSP not configured for %s. Set VIBESOLARIS_LSP_SERVER.\n"
            "(No go-to-definition available without an LSP server)",
            file_path);
        return err;
    }

    VSLSPServer *srv = vs_lsp_find_server(servers, count, file_path);
    if(!srv) return dupstr("ERROR: no LSP server slot available");

    if(srv->pid <= 0){
        if(vs_lsp_spawn_server(srv, cmd, NULL) != 0)
            return dupstr("ERROR: failed to spawn LSP server");
    }
    if(!srv->initialized && vs_lsp_initialize(srv) != 0){
        return dupstr("ERROR: LSP server initialization failed");
    }

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    /* LSP uses 0-based line/column */
    char params[VS_MAX_PATH + 256];
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%d,\"character\":%d}}",
        uri, line - 1, col - 1);

    char *req = jsonrpc_request(srv->next_id++, "textDocument/definition", params);
    if(!req) return dupstr("ERROR: out of memory");

    if(send_jsonrpc(srv->in_fd, req) != 0){
        free(req);
        return dupstr("ERROR: failed to send LSP request");
    }
    free(req);

    char *resp = read_jsonrpc(srv->out_fd, VS_LSP_REQUEST_TIMEOUT);
    if(!resp){
        return dupstr("ERROR: LSP server did not respond (timeout)");
    }

    if(json_has(resp, "error")){
        char *msg = (char*)json_string_at(resp, "message");
        char *result = (char*)malloc(512);
        if(result && msg){
            snprintf(result, 512, "LSP definition error: %s", msg);
            free(msg);
        } else if(result){
            snprintf(result, 512, "LSP definition error: %s", resp);
        }
        free(resp);
        return result;
    }

    const char *targetUri = json_string_at(resp, "uri");
    const char *targetRange = json_string_at(resp, "range");

    char *output = (char*)malloc(1024);
    if(!output){
        free(resp);
        if(targetUri) free((void*)targetUri);
        if(targetRange) free((void*)targetRange);
        return NULL;
    }

    int offset = 0;
    if(targetUri){
        offset += snprintf(output + offset, 1024 - offset,
            "Definition found at: %s", targetUri);
        if(targetRange){
            offset += snprintf(output + offset, 1024 - offset,
                " (range: %s)", targetRange);
        }
        free((void*)targetUri);
        if(targetRange) free((void*)targetRange);
    } else {
        offset += snprintf(output + offset, 1024 - offset,
            "No definition found at %s:%d:%d", file_path, line, col);
    }
    output[offset] = '\n';

    free(resp);
    return output;
}

char *vs_lsp_hover(VSLSPServer *servers, int *count, const char *file_path, int line, int col){
    if(!file_path) return dupstr("ERROR: lsp hover requires a file path");
    if(line < 1 || col < 1) return dupstr("ERROR: lsp hover requires valid line and column (1-based)");

    char cmd[512];
    if(!lsp_server_for(file_path, cmd, sizeof(cmd))){
        char *err = (char*)malloc(512);
        if(!err) return NULL;
        snprintf(err, 512,
            "LSP not configured for %s. Set VIBESOLARIS_LSP_SERVER.\n"
            "(No hover information available without an LSP server)",
            file_path);
        return err;
    }

    VSLSPServer *srv = vs_lsp_find_server(servers, count, file_path);
    if(!srv) return dupstr("ERROR: no LSP server slot available");

    if(srv->pid <= 0){
        if(vs_lsp_spawn_server(srv, cmd, NULL) != 0)
            return dupstr("ERROR: failed to spawn LSP server");
    }
    if(!srv->initialized && vs_lsp_initialize(srv) != 0){
        return dupstr("ERROR: LSP server initialization failed");
    }

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 256];
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%d,\"character\":%d}}",
        uri, line - 1, col - 1);

    char *req = jsonrpc_request(srv->next_id++, "textDocument/hover", params);
    if(!req) return dupstr("ERROR: out of memory");

    if(send_jsonrpc(srv->in_fd, req) != 0){
        free(req);
        return dupstr("ERROR: failed to send LSP request");
    }
    free(req);

    char *resp = read_jsonrpc(srv->out_fd, VS_LSP_REQUEST_TIMEOUT);
    if(!resp){
        return dupstr("ERROR: LSP server did not respond (timeout)");
    }

    if(json_has(resp, "error")){
        char *msg = (char*)json_string_at(resp, "message");
        char *result = (char*)malloc(512);
        if(result && msg){
            snprintf(result, 512, "LSP hover error: %s", msg);
            free(msg);
        } else if(result){
            snprintf(result, 512, "LSP hover error: %s", resp);
        }
        free(resp);
        return result;
    }

    const char *contents = json_string_at(resp, "contents");
    const char *value = json_string_at(resp, "value");
    const char *kind = json_string_at(resp, "kind");

    char *output = (char*)malloc(2048);
    if(!output){
        free(resp);
        if(contents) free((void*)contents);
        if(value) free((void*)value);
        if(kind) free((void*)kind);
        return NULL;
    }

    int offset = 0;
    if(value){
        offset += snprintf(output + offset, 2048 - offset,
            "Hover (%s):\n%s", kind ? kind : "markdown", value);
        free((void*)value);
    } else if(contents){
        offset += snprintf(output + offset, 2048 - offset,
            "Hover:\n%s", contents);
        free((void*)contents);
    } else {
        offset += snprintf(output + offset, 2048 - offset,
            "No hover information at %s:%d:%d", file_path, line, col);
    }
    if(kind) free((void*)kind);
    output[offset] = '\n';

    free(resp);
    return output;
}

char *vs_lsp_rename(VSLSPServer *servers, int *count, const char *file_path, int line, int col, const char *new_name){
    if(!file_path) return dupstr("ERROR: lsp rename requires a file path");
    if(!new_name || !*new_name) return dupstr("ERROR: lsp rename requires a new name");
    if(line < 1 || col < 1) return dupstr("ERROR: lsp rename requires valid line and column (1-based)");

    char cmd[512];
    if(!lsp_server_for(file_path, cmd, sizeof(cmd))){
        char *err = (char*)malloc(512);
        if(!err) return NULL;
        snprintf(err, 512,
            "LSP not configured for %s. Set VIBESOLARIS_LSP_SERVER.\n"
            "(No rename available without an LSP server)",
            file_path);
        return err;
    }

    VSLSPServer *srv = vs_lsp_find_server(servers, count, file_path);
    if(!srv) return dupstr("ERROR: no LSP server slot available");

    if(srv->pid <= 0){
        if(vs_lsp_spawn_server(srv, cmd, NULL) != 0)
            return dupstr("ERROR: failed to spawn LSP server");
    }
    if(!srv->initialized && vs_lsp_initialize(srv) != 0){
        return dupstr("ERROR: LSP server initialization failed");
    }

    char uri[VS_MAX_PATH + 8];
    path_to_uri(file_path, uri, sizeof(uri));

    char params[VS_MAX_PATH + 512];
    snprintf(params, sizeof(params),
        "{\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%d,\"character\":%d},\"newName\":\"%s\"}",
        uri, line - 1, col - 1, new_name);

    char *req = jsonrpc_request(srv->next_id++, "textDocument/rename", params);
    if(!req) return dupstr("ERROR: out of memory");

    if(send_jsonrpc(srv->in_fd, req) != 0){
        free(req);
        return dupstr("ERROR: failed to send LSP request");
    }
    free(req);

    char *resp = read_jsonrpc(srv->out_fd, VS_LSP_REQUEST_TIMEOUT);
    if(!resp){
        return dupstr("ERROR: LSP server did not respond (timeout)");
    }

    if(json_has(resp, "error")){
        char *msg = (char*)json_string_at(resp, "message");
        char *result = (char*)malloc(512);
        if(result && msg){
            snprintf(result, 512, "LSP rename error: %s", msg);
            free(msg);
        } else if(result){
            snprintf(result, 512, "LSP rename error: %s", resp);
        }
        free(resp);
        return result;
    }

    const char *changes = json_string_at(resp, "changes");
    const char *documentChanges = json_string_at(resp, "documentChanges");

    char *output = (char*)malloc(2048);
    if(!output){
        free(resp);
        if(changes) free((void*)changes);
        if(documentChanges) free((void*)documentChanges);
        return NULL;
    }

    int offset = 0;
    if(changes || documentChanges){
        offset += snprintf(output + offset, 2048 - offset,
            "Rename '%s' prepared. LSP returned change set.\n", new_name);
        if(changes){
            offset += snprintf(output + offset, 2048 - offset,
                "Changes: %s\n", changes);
            free((void*)changes);
        }
        if(documentChanges){
            offset += snprintf(output + offset, 2048 - offset,
                "Document changes: %s\n", documentChanges);
            free((void*)documentChanges);
        }
    } else {
        offset += snprintf(output + offset, 2048 - offset,
            "Rename '%s' acknowledged by LSP server.\n", new_name);
    }

    free(resp);
    return output;
}

void vs_lsp_shutdown_all(VSLSPServer *servers, int *count){
    if(!servers || !count) return;
    int i;
    for(i = 0; i < *count; i++){
        VSLSPServer *srv = &servers[i];
        if(srv->pid > 0){
            /* Send shutdown request */
            if(srv->initialized && srv->in_fd > 0){
                char *req = jsonrpc_request(srv->next_id++, "shutdown", "{}");
                if(req){
                    send_jsonrpc(srv->in_fd, req);
                    free(req);
                }
                char *notif = jsonrpc_notification("exit", "{}");
                if(notif){
                    send_jsonrpc(srv->in_fd, notif);
                    free(notif);
                }
            }
            /* Close pipes */
            if(srv->in_fd > 0) close(srv->in_fd);
            if(srv->out_fd > 0) close(srv->out_fd);
            /* Kill process if still running */
            kill(srv->pid, SIGTERM);
            waitpid(srv->pid, NULL, WNOHANG);
            srv->pid = 0;
            srv->in_fd = -1;
            srv->out_fd = -1;
            srv->initialized = 0;
        }
    }
    *count = 0;
}

/* ---- Backward compatibility wrappers ---- */

char *vs_lsp_diagnostics_compat(const char *file_path){
    static VSLSPServer compat_servers[VS_MAX_LSP_SERVERS];
    static int compat_count = 0;
    static int compat_inited = 0;
    if(!compat_inited){
        memset(compat_servers, 0, sizeof(compat_servers));
        compat_count = 0;
        compat_inited = 1;
    }
    return vs_lsp_diagnostics(compat_servers, &compat_count, file_path);
}

char *vs_lsp_definition_compat(const char *file_path, int line, int col){
    static VSLSPServer compat_servers[VS_MAX_LSP_SERVERS];
    static int compat_count = 0;
    static int compat_inited = 0;
    if(!compat_inited){
        memset(compat_servers, 0, sizeof(compat_servers));
        compat_count = 0;
        compat_inited = 1;
    }
    return vs_lsp_definition(compat_servers, &compat_count, file_path, line, col);
}
