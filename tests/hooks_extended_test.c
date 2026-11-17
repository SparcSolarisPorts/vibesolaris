/* SPDX-License-Identifier: Unlicense */
/* Hook enhancements tests (tests/hooks_extended_test.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass = 0;
static int fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) { pass++; }
    else { fail++; printf("FAIL: %s\n", msg); }
}

int main(void)
{
    /* Test 1: MessageDisplay hook type */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        vs_hooks_add(&store, "*", "echo", 0);
        store.hooks[0].hook_type = VS_HOOK_MESSAGE_DISPLAY;
        int rc = vs_hooks_run_message_display(&store, "hello world");
        check(rc == 0, "run_message_display returns 0");
    }

    /* Test 2: MessageDisplay skips non-matching hooks */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        vs_hooks_add(&store, "*", "echo", 1);
        store.hooks[0].hook_type = VS_HOOK_PRE_TOOL_USE;
        int rc = vs_hooks_run_message_display(&store, "text");
        check(rc == 0, "message_display skips pre-tool hooks");
    }

    /* Test 3: Prompt hook modifies text */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        vs_hooks_add(&store, "*", "echo modified", 0);
        store.hooks[0].hook_type = VS_HOOK_PROMPT;
        char modified[1024];
        int rc = vs_hooks_run_prompt(&store, "original", modified, sizeof(modified));
        check(rc == 0, "run_prompt returns 0");
        check(strstr(modified, "modified") != NULL, "prompt hook modifies text");
    }

    /* Test 4: Prompt hook NULL safety */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        int rc = vs_hooks_run_prompt(NULL, "text", NULL, 0);
        check(rc == 0, "run_prompt handles NULL store");
    }

    /* Test 5: HTTP webhook NULL safety */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        int rc = vs_hooks_send_webhook(NULL, "event", "payload");
        check(rc == -1, "send_webhook handles NULL store");
    }

    /* Test 6: HTTP webhook skips non-HTTP hooks */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        vs_hooks_add(&store, "*", "echo", 0);
        store.hooks[0].hook_type = VS_HOOK_PRE_TOOL_USE;
        int rc = vs_hooks_send_webhook(&store, "event", "payload");
        check(rc == 0, "send_webhook skips non-HTTP hooks");
    }

    /* Test 7: Hook type constants */
    {
        check(VS_HOOK_PRE_TOOL_USE == 0, "PRE_TOOL_USE is 0");
        check(VS_HOOK_POST_TOOL_USE == 1, "POST_TOOL_USE is 1");
        check(VS_HOOK_MESSAGE_DISPLAY == 2, "MESSAGE_DISPLAY is 2");
        check(VS_HOOK_PROMPT == 3, "PROMPT is 3");
        check(VS_HOOK_HTTP == 4, "HTTP is 4");
    }

    /* Test 8: VSHook struct has new fields */
    {
        VSHook hook;
        memset(&hook, 0, sizeof(hook));
        hook.hook_type = VS_HOOK_HTTP;
        hook.async = 1;
        strncpy(hook.webhook_url, "http://example.com/hook", sizeof(hook.webhook_url) - 1);
        check(hook.hook_type == VS_HOOK_HTTP, "hook_type field works");
        check(hook.async == 1, "async field works");
        check(strcmp(hook.webhook_url, "http://example.com/hook") == 0, "webhook_url field works");
    }

    /* Test 9: MessageDisplay with empty store */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        int rc = vs_hooks_run_message_display(&store, "text");
        check(rc == 0, "message_display on empty store");
    }

    /* Test 10: Prompt hook with empty store */
    {
        VSHooksStore store;
        vs_hooks_init(&store, NULL);
        char modified[1024];
        int rc = vs_hooks_run_prompt(&store, "original", modified, sizeof(modified));
        check(rc == 0, "prompt on empty store");
        check(strcmp(modified, "original") == 0, "prompt unchanged with no hooks");
    }

    printf("\nResult: %d/%d tests passed\n", pass, pass + fail);
    return fail > 0 ? 1 : 0;
}

/* Stub for util.c (agent tests do not link it). */
char *vs_shell_quote(const char *s){const char *p;size_t n=2;char *o,*w;if(!s)s="";for(p=s;*p;p++)n+=(*p=='\'')?4:1;o=(char*)malloc(n+1);if(!o)return NULL;w=o;*w++='\'';for(p=s;*p;p++){if(*p=='\''){memcpy(w,"'\\''",4);w+=4;}else *w++=*p;}*w++='\'';*w=0;return o;}
