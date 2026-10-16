# SPDX-License-Identifier: Unlicense
# VibeSolaris portable Makefile
#
# Kept compatible with classic Solaris make as well as GNU/BSD make.
# No GNU-only ?= or pattern rules are used.  Explicit -o paths are intentional:
# old Solaris make inference rules can otherwise put src/foo.o in the project root.
#
# build.sh normally supplies these variables on the command line.

CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -O2 -Wall
LDFLAGS =
CURL_LIBS = -lcurl
CRYPTO_LIBS = -lcrypto
X11_LIBS = -lX11
THREAD_LIBS = -lpthread

CORE = src/util.o src/http.o src/web.o src/graph.o src/config.o src/secure_config.o src/sha256.o src/oauth.o src/mcp.o src/provider.o src/agent.o src/subagent.o src/workflow.o

all: vibesolaris vibesolaris-gui

vibesolaris: $(CORE) src/tui.o src/tui_stream.o
	$(CC) $(LDFLAGS) -o vibesolaris $(CORE) src/tui.o src/tui_stream.o $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS)

vibesolaris-gui: $(CORE) src/gui.o
	$(CC) $(LDFLAGS) -o vibesolaris-gui $(CORE) src/gui.o $(CURL_LIBS) $(CRYPTO_LIBS) $(X11_LIBS) $(THREAD_LIBS)

src/util.o: src/util.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/util.c -o src/util.o

src/http.o: src/http.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/http.c -o src/http.o

src/web.o: src/web.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/web.c -o src/web.o

src/graph.o: src/graph.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/graph.c -o src/graph.o

src/config.o: src/config.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/config.c -o src/config.o

src/secure_config.o: src/secure_config.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/secure_config.c -o src/secure_config.o

src/sha256.o: src/sha256.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/sha256.c -o src/sha256.o

src/oauth.o: src/oauth.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/oauth.c -o src/oauth.o


src/mcp.o: src/mcp.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/mcp.c -o src/mcp.o

src/provider.o: src/provider.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/provider.c -o src/provider.o

src/agent.o: src/agent.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/agent.c -o src/agent.o

src/subagent.o: src/subagent.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/subagent.c -o src/subagent.o

src/workflow.o: src/workflow.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/workflow.c -o src/workflow.o

src/tui.o: src/tui.c src/tui_stream.h include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui.c -o src/tui.o

src/tui_stream.o: src/tui_stream.c src/tui_stream.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui_stream.c -o src/tui_stream.o

src/gui.o: src/gui.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/gui.c -o src/gui.o

clean:
	rm -f *.o src/*.o vibesolaris vibesolaris-gui

test-agent:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_lifecycle_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-agent-lifecycle-test
	/tmp/vibesolaris-agent-lifecycle-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_strict_execution_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-agent-strict-test
	/tmp/vibesolaris-agent-strict-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_compaction_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-agent-compaction-test
	/tmp/vibesolaris-agent-compaction-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_native_tools_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-agent-native-tools-test
	/tmp/vibesolaris-agent-native-tools-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/graph_test.c src/graph.c -o /tmp/vibesolaris-graph-test
	/tmp/vibesolaris-graph-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/web_test.c src/web.c -o /tmp/vibesolaris-web-test
	/tmp/vibesolaris-web-test

test-subagent:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/subagent_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-subagent-test
	/tmp/vibesolaris-subagent-test

test-workflow:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/workflow_test.c src/workflow.c -o /tmp/vibesolaris-workflow-test
	/tmp/vibesolaris-workflow-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_workflow_test.c src/agent.c src/subagent.c src/workflow.c -o /tmp/vibesolaris-agent-workflow-test
	/tmp/vibesolaris-agent-workflow-test

test-claude-oauth:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/oauth_claude_test.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-oauth-claude-test
	/tmp/vibesolaris-oauth-claude-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/claude_provider_test.c src/provider.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-claude-provider-test
	/tmp/vibesolaris-claude-provider-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/claude_config_test.c src/config.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-claude-config-test
	/tmp/vibesolaris-claude-config-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/custom_anthropic_agent_test.c src/provider.c src/agent.c src/subagent.c src/workflow.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-custom-anthropic-agent-test
	/tmp/vibesolaris-custom-anthropic-agent-test

test-provider-stream:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/provider_stream_sse_test.c src/provider.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-provider-stream-sse-test
	/tmp/vibesolaris-provider-stream-sse-test

test-tui-stream:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_stream_test.c src/tui_stream.c -o /tmp/vibesolaris-tui-stream-test
	/tmp/vibesolaris-tui-stream-test

test: test-agent test-subagent test-workflow test-claude-oauth test-provider-stream test-tui-stream
