/* SPDX-License-Identifier: Unlicense */
/* Unit tests for cross-session messaging (src/cross_session.c). */
#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests = 0;
static int passed = 0;

static void check(int cond, const char *msg) {
    tests++;
    if (cond) {
        passed++;
        printf("  ok: %s\n", msg);
    } else {
        printf("  FAIL: %s\n", msg);
    }
}

int main(void) {
    VSCrossSessionStore store;
    char *result;

    printf("Cross-Session tests:\n");

    /* --- vs_cross_session_init --- */
    printf("  init:\n");
    vs_cross_session_init(&store);
    check(store.my_pid > 0, "init sets my_pid");
    check(store.peer_count == 0, "init clears peer_count");
    check(store.inbox_count == 0, "init clears inbox_count");
    check(strlen(store.register_path) > 0, "init sets register_path");

    /* --- vs_cross_session_discover --- */
    printf("  discover:\n");
    int peers = vs_cross_session_discover(&store);
    check(peers >= 0, "discover returns non-negative");

    /* --- vs_cross_session_send --- */
    printf("  send:\n");
    /* Send may fail if session dir doesn't exist, which is OK in test env */
    int rc = vs_cross_session_send(&store, 99999, "Hello from test");
    check(rc == 0 || rc == -1, "send returns 0 or -1 (dir may not exist)");

    rc = vs_cross_session_send(NULL, 1, "test");
    check(rc == -1, "send with NULL store returns -1");

    rc = vs_cross_session_send(&store, 0, "test");
    check(rc == -1, "send with PID 0 returns -1");

    rc = vs_cross_session_send(&store, 1, NULL);
    check(rc == -1, "send with NULL message returns -1");

    /* --- vs_cross_session_receive --- */
    printf("  receive:\n");
    int msgs = vs_cross_session_receive(&store);
    check(msgs >= 0, "receive returns non-negative");

    msgs = vs_cross_session_receive(NULL);
    check(msgs == 0, "receive with NULL returns 0");

    /* --- vs_cross_session_list --- */
    printf("  list:\n");
    result = vs_cross_session_list(&store, 20);
    check(result != NULL, "list returns non-NULL");
    if (result) {
        check(strstr(result, "Sessions") != NULL, "list shows title");
        check(strstr(result, "peers") != NULL, "list shows peer count");
        free(result);
    }

    /* List with NULL */
    result = vs_cross_session_list(NULL, 10);
    check(result != NULL, "list with NULL returns non-NULL");
    if (result) free(result);

    /* --- vs_cross_session_inbox --- */
    printf("  inbox:\n");
    result = vs_cross_session_inbox(&store, 20);
    check(result != NULL, "inbox returns non-NULL");
    if (result) {
        check(strstr(result, "Inbox") != NULL, "inbox shows title");
        free(result);
    }

    /* Inbox with NULL */
    result = vs_cross_session_inbox(NULL, 10);
    check(result != NULL, "inbox with NULL returns non-NULL");
    if (result) free(result);

    /* --- vs_cross_session_shutdown --- */
    printf("  shutdown:\n");
    vs_cross_session_shutdown(&store);
    check(store.peer_count == 0, "shutdown clears peer_count");

    vs_cross_session_shutdown(NULL);
    check(1, "shutdown with NULL is safe");

    /* --- Capacity limits --- */
    printf("  capacity limits:\n");
    check(VS_MAX_SESSION_PEERS == 8, "max peers is 8");
    check(VS_MAX_SESSION_MESSAGES == 16, "max messages is 16");
    check(VS_SESSION_PEER_ID_MAX == 32, "peer ID max is 32");
    check(VS_SESSION_MESSAGE_MAX == 512, "message max is 512");

    /* --- NULL safety --- */
    printf("  null safety:\n");
    vs_cross_session_init(NULL);
    check(1, "init with NULL is safe");

    /* --- Edge cases --- */
    printf("  edge cases:\n");
    vs_cross_session_init(&store);

    /* List with 0 max_lines */
    result = vs_cross_session_list(&store, 0);
    check(result != NULL, "list with 0 max_lines returns non-NULL");
    if (result) free(result);

    /* Inbox with 0 max_lines */
    result = vs_cross_session_inbox(&store, 0);
    check(result != NULL, "inbox with 0 max_lines returns non-NULL");
    if (result) free(result);

    /* Manual peer construction */
    printf("  manual peers:\n");
    store.peer_count = 2;
    store.peers[0].pid = 12345;
    store.peers[0].active = 1;
    store.peers[0].last_seen = 1000000;
    snprintf(store.peers[0].id, sizeof(store.peers[0].id), "test-peer-1");
    store.peers[1].pid = 67890;
    store.peers[1].active = 1;
    store.peers[1].last_seen = 2000000;
    snprintf(store.peers[1].id, sizeof(store.peers[1].id), "test-peer-2");

    result = vs_cross_session_list(&store, 10);
    check(result != NULL, "list with manual peers returns non-NULL");
    if (result) {
        check(strstr(result, "test-peer-1") != NULL, "list shows peer 1");
        check(strstr(result, "test-peer-2") != NULL, "list shows peer 2");
        free(result);
    }

    /* Manual inbox construction */
    store.inbox_count = 1;
    store.inbox[0].from_pid = 12345;
    store.inbox[0].timestamp = 1000000;
    snprintf(store.inbox[0].message, sizeof(store.inbox[0].message),
             "Test message from peer");

    result = vs_cross_session_inbox(&store, 10);
    check(result != NULL, "inbox with manual msg returns non-NULL");
    if (result) {
        check(strstr(result, "Test message from peer") != NULL,
              "inbox shows message");
        free(result);
    }

    vs_cross_session_shutdown(&store);

    printf("\nResult: %d/%d tests passed\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
