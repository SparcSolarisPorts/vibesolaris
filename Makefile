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
CFLAGS = -O2 -Wall -pthread
LDFLAGS =
CURL_LIBS = -lcurl
CRYPTO_LIBS = -lcrypto
X11_LIBS = -lX11 -lXtst
CU_X11_DEFS = -DHAVE_X11 -I/usr/openwin/include
THREAD_LIBS = -pthread

CORE = src/util.o src/http.o src/web.o src/graph.o src/config.o src/secure_config.o src/sha256.o src/oauth.o src/mcp.o src/provider.o src/agent.o src/subagent.o src/workflow.o src/coordinator.o src/tool_executor.o src/skill.o src/memory.o src/todo.o src/edit.o src/glob.o src/grep.o src/notebook.o src/monitor.o src/multi_edit.o src/lsp.o src/permission.o src/task.o src/slash.o src/tool_search.o src/hooks.o src/checkpoint.o src/auto_mode.o src/agent_team.o src/plugin.o src/code_review.o src/init_cmd.o src/keybindings.o src/settings.o src/project_index.o src/tui_theme.o src/tui_suggestions.o src/tui_editor.o src/reasoning.o src/rewind.o src/loop.o src/workflow_engine.o src/cross_session.o src/phase9.o

all: vibesolaris vibesolaris-gui

# The Wayland and Cocoa backends use only external commands, so they link
# into both binaries without adding library dependencies.  The non-X11
# computer_use build delegates to whichever backend the session offers.
vibesolaris: $(CORE) src/computer_use_stub.o src/computer_use_wayland.o src/computer_use_cocoa.o src/tui.o src/tui_stream.o src/tui_compositor.o
	$(CC) $(LDFLAGS) -o vibesolaris $(CORE) src/computer_use_stub.o src/computer_use_wayland.o src/computer_use_cocoa.o src/tui.o src/tui_stream.o src/tui_compositor.o $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS)

vibesolaris-gui: $(CORE) src/computer_use.o src/computer_use_wayland.o src/computer_use_cocoa.o src/gui.o
	$(CC) $(LDFLAGS) -o vibesolaris-gui $(CORE) src/computer_use.o src/computer_use_wayland.o src/computer_use_cocoa.o src/gui.o $(CURL_LIBS) $(CRYPTO_LIBS) $(X11_LIBS) $(THREAD_LIBS)

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

src/tui_compositor.o: src/tui_compositor.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui_compositor.c -o src/tui_compositor.o

src/gui.o: src/gui.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/gui.c -o src/gui.o

src/coordinator.o: src/coordinator.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/coordinator.c -o src/coordinator.o

src/tool_executor.o: src/tool_executor.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tool_executor.c -o src/tool_executor.o

src/skill.o: src/skill.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/skill.c -o src/skill.o

src/memory.o: src/memory.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/memory.c -o src/memory.o

src/todo.o: src/todo.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/todo.c -o src/todo.o

src/edit.o: src/edit.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/edit.c -o src/edit.o

src/glob.o: src/glob.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/glob.c -o src/glob.o

src/grep.o: src/grep.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/grep.c -o src/grep.o

src/notebook.o: src/notebook.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/notebook.c -o src/notebook.o

src/monitor.o: src/monitor.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/monitor.c -o src/monitor.o

src/multi_edit.o: src/multi_edit.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/multi_edit.c -o src/multi_edit.o

src/lsp.o: src/lsp.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/lsp.c -o src/lsp.o

src/permission.o: src/permission.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/permission.c -o src/permission.o

src/task.o: src/task.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/task.c -o src/task.o

src/slash.o: src/slash.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/slash.c -o src/slash.o

src/tool_search.o: src/tool_search.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tool_search.c -o src/tool_search.o

src/hooks.o: src/hooks.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/hooks.c -o src/hooks.o

src/computer_use.o: src/computer_use.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CU_X11_DEFS) -c src/computer_use.c -o src/computer_use.o

src/computer_use_stub.o: src/computer_use.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_WAYLAND -DHAVE_COCOA -c src/computer_use.c -o src/computer_use_stub.o

src/checkpoint.o: src/checkpoint.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/checkpoint.c -o src/checkpoint.o

src/auto_mode.o: src/auto_mode.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/auto_mode.c -o src/auto_mode.o

src/agent_team.o: src/agent_team.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/agent_team.c -o src/agent_team.o

src/plugin.o: src/plugin.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/plugin.c -o src/plugin.o

src/code_review.o: src/code_review.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/code_review.c -o src/code_review.o

src/init_cmd.o: src/init_cmd.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/init_cmd.c -o src/init_cmd.o

src/keybindings.o: src/keybindings.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/keybindings.c -o src/keybindings.o

src/settings.o: src/settings.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/settings.c -o src/settings.o

src/project_index.o: src/project_index.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/project_index.c -o src/project_index.o

src/tui_theme.o: src/tui_theme.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui_theme.c -o src/tui_theme.o

src/tui_suggestions.o: src/tui_suggestions.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui_suggestions.c -o src/tui_suggestions.o

src/tui_editor.o: src/tui_editor.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui_editor.c -o src/tui_editor.o

src/reasoning.o: src/reasoning.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/reasoning.c -o src/reasoning.o

src/rewind.o: src/rewind.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/rewind.c -o src/rewind.o

src/loop.o: src/loop.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/loop.c -o src/loop.o

src/workflow_engine.o: src/workflow_engine.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/workflow_engine.c -o src/workflow_engine.o

src/cross_session.o: src/cross_session.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/cross_session.c -o src/cross_session.o

src/phase9.o: src/phase9.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/phase9.c -o src/phase9.o

# Optional: Wayland Computer Use (Linux only)
src/computer_use_wayland.o: src/computer_use_wayland.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_WAYLAND -c src/computer_use_wayland.c -o src/computer_use_wayland.o

# Optional: Cocoa Computer Use (macOS only)
src/computer_use_cocoa.o: src/computer_use_cocoa.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_COCOA -c src/computer_use_cocoa.c -o src/computer_use_cocoa.o

clean:
	rm -f *.o src/*.o vibesolaris vibesolaris-gui

AGENT_SRCS = src/agent.c src/subagent.c src/workflow.c src/todo.c src/edit.c src/glob.c src/grep.c src/notebook.c src/monitor.c src/multi_edit.c src/lsp.c src/permission.c src/task.c src/slash.c src/tool_search.c src/hooks.c src/skill.c src/coordinator.c src/tool_executor.c src/computer_use.c src/checkpoint.c src/auto_mode.c src/agent_team.c src/plugin.c src/code_review.c src/init_cmd.c src/keybindings.c src/computer_use_wayland.c src/computer_use_cocoa.c src/tui_theme.c src/tui_suggestions.c src/tui_editor.c src/reasoning.c
AGENT_MCP_STUB = tests/mcp_stub.c
test-agent:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_lifecycle_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-lifecycle-test
	/tmp/vibesolaris-agent-lifecycle-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_strict_execution_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-strict-test
	/tmp/vibesolaris-agent-strict-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_compaction_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-compaction-test
	/tmp/vibesolaris-agent-compaction-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_native_tools_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-native-tools-test
	/tmp/vibesolaris-agent-native-tools-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_extended_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-extended-test
	/tmp/vibesolaris-agent-extended-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/graph_test.c src/graph.c -o /tmp/vibesolaris-graph-test
	/tmp/vibesolaris-graph-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/web_test.c src/web.c -o /tmp/vibesolaris-web-test
	/tmp/vibesolaris-web-test

test-agent-extended-tools:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_extended_tools_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-extended-tools-test
	/tmp/vibesolaris-agent-extended-tools-test

test-agent-plan-deferral:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_plan_deferral_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-plan-deferral-test
	/tmp/vibesolaris-agent-plan-deferral-test

test-subagent:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/subagent_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-subagent-test
	/tmp/vibesolaris-subagent-test

test-workflow:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/workflow_test.c src/workflow.c -o /tmp/vibesolaris-workflow-test
	/tmp/vibesolaris-workflow-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_workflow_test.c $(AGENT_SRCS) $(AGENT_MCP_STUB) $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-agent-workflow-test
	/tmp/vibesolaris-agent-workflow-test

test-claude-oauth:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/oauth_claude_test.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-oauth-claude-test
	/tmp/vibesolaris-oauth-claude-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/claude_provider_test.c src/provider.c src/reasoning.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-claude-provider-test
	/tmp/vibesolaris-claude-provider-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/claude_config_test.c src/config.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-claude-config-test
	/tmp/vibesolaris-claude-config-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/custom_anthropic_agent_test.c src/provider.c $(AGENT_SRCS) $(AGENT_MCP_STUB) src/oauth.c src/sha256.c $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-custom-anthropic-agent-test
	/tmp/vibesolaris-custom-anthropic-agent-test

test-provider-cache:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/provider_cache_test.c src/provider.c $(AGENT_SRCS) $(AGENT_MCP_STUB) src/oauth.c src/sha256.c $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-provider-cache-test
	/tmp/vibesolaris-provider-cache-test

test-provider-stream:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/provider_stream_sse_test.c src/provider.c src/reasoning.c src/oauth.c src/sha256.c -o /tmp/vibesolaris-provider-stream-sse-test
	/tmp/vibesolaris-provider-stream-sse-test

test-tui-stream:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_stream_test.c src/tui_stream.c -o /tmp/vibesolaris-tui-stream-test
	/tmp/vibesolaris-tui-stream-test

test-todo:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/todo_test.c src/todo.c -o /tmp/vibesolaris-todo-test
	/tmp/vibesolaris-todo-test

test-edit:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/edit_test.c src/edit.c -o /tmp/vibesolaris-edit-test
	/tmp/vibesolaris-edit-test

test-memory:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/memory_test.c src/memory.c -o /tmp/vibesolaris-memory-test
	/tmp/vibesolaris-memory-test

test-skill:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/skill_test.c src/skill.c -o /tmp/vibesolaris-skill-test
	/tmp/vibesolaris-skill-test

test-coordinator:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/coordinator_test.c src/coordinator.c $(THREAD_LIBS) -o /tmp/vibesolaris-coordinator-test
	/tmp/vibesolaris-coordinator-test

test-tool-executor:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tool_executor_test.c src/tool_executor.c $(THREAD_LIBS) -o /tmp/vibesolaris-tool-executor-test
	/tmp/vibesolaris-tool-executor-test

test-glob:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/glob_test.c src/glob.c -o /tmp/vibesolaris-glob-test
	/tmp/vibesolaris-glob-test

test-grep:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/grep_test.c src/grep.c -o /tmp/vibesolaris-grep-test
	/tmp/vibesolaris-grep-test

test-notebook:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/notebook_test.c src/notebook.c -o /tmp/vibesolaris-notebook-test
	/tmp/vibesolaris-notebook-test

test-monitor:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/monitor_test.c src/monitor.c -o /tmp/vibesolaris-monitor-test
	/tmp/vibesolaris-monitor-test

test-multi-edit:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/multi_edit_test.c src/multi_edit.c -o /tmp/vibesolaris-multi-edit-test
	/tmp/vibesolaris-multi-edit-test

test-lsp:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/lsp_test.c src/lsp.c -o /tmp/vibesolaris-lsp-test
	/tmp/vibesolaris-lsp-test

test-permission:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/permission_test.c src/permission.c -o /tmp/vibesolaris-permission-test
	/tmp/vibesolaris-permission-test

test-task:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/task_test.c src/task.c -o /tmp/vibesolaris-task-test
	/tmp/vibesolaris-task-test

test-checkpoint:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/checkpoint_test.c src/checkpoint.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-checkpoint-test
	/tmp/vibesolaris-checkpoint-test

test-dream:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/dream_test.c src/memory.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-dream-test
	/tmp/vibesolaris-dream-test

test-auto-mode:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/auto_mode_test.c src/auto_mode.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-auto-mode-test
	/tmp/vibesolaris-auto-mode-test

test-agent-team:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/agent_team_test.c src/agent_team.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-agent-team-test
	/tmp/vibesolaris-agent-team-test

test-plugin:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/plugin_test.c src/plugin.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-plugin-test
	/tmp/vibesolaris-plugin-test

test-code-review:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/code_review_test.c src/code_review.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-code-review-test
	/tmp/vibesolaris-code-review-test

test-init-cmd:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/init_cmd_test.c src/init_cmd.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-init-cmd-test
	/tmp/vibesolaris-init-cmd-test

test-keybindings:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/keybindings_test.c src/keybindings.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-keybindings-test
	@d=$$(mktemp -d /tmp/vs-keybindings.XXXXXX) || exit 1; trap 'rm -rf "$$d"' 0 1 2 3 15; mkdir "$$d/.vibesolaris" || exit 1; HOME="$$d" /tmp/vibesolaris-keybindings-test

test-computer-use:
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_WAYLAND -DHAVE_COCOA tests/computer_use_backend_test.c src/computer_use.c src/computer_use_wayland.c src/computer_use_cocoa.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-computer-use-test
	/tmp/vibesolaris-computer-use-test

# Non-interactive Computer Use checks (detection, NULL-safety, headless
# degradation).  Safe for CI and SSH sessions: no input injection, no
# screen capture.
test-computer-use-macos:
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_WAYLAND -DHAVE_COCOA tests/computer_use_macos_test.c src/computer_use.c src/computer_use_wayland.c src/computer_use_cocoa.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-computer-use-macos-test
	/tmp/vibesolaris-computer-use-macos-test

# GUI smoke test: the full GUI binary must link and must refuse a headless
# session cleanly instead of crashing.  Requires X11 development libraries
# to build, so it is not part of "make test".
test-gui: vibesolaris-gui
	@d=$$(mktemp -d /tmp/vs-gui.XXXXXX) || exit 1; trap 'rm -rf "$$d"' 0 1 2 3 15; \
	out=$$(unset DISPLAY WAYLAND_DISPLAY; HOME="$$d" ./vibesolaris-gui 2>&1); rc=$$?; \
	echo "$$out"; \
	if [ $$rc -eq 1 ] && echo "$$out" | grep -q "cannot open X display"; then \
		echo "ok: GUI refuses a headless session cleanly"; \
	else \
		echo "FAIL: GUI did not refuse a headless session cleanly (rc=$$rc)"; exit 1; \
	fi

test-goal: $(CORE) src/computer_use_stub.o src/computer_use_wayland.o src/computer_use_cocoa.o
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/goal_test.c $(CORE) src/computer_use_stub.o src/computer_use_wayland.o src/computer_use_cocoa.o $(CURL_LIBS) $(CRYPTO_LIBS) $(THREAD_LIBS) -o /tmp/vibesolaris-goal-test
	@d=$$(mktemp -d /tmp/vs-goal.XXXXXX) || exit 1; trap 'rm -rf "$$d"' 0 1 2 3 15; HOME="$$d" /tmp/vibesolaris-goal-test

test-settings:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/settings_test.c src/settings.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-settings-test
	/tmp/vibesolaris-settings-test

test-hooks-extended:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hooks_extended_test.c src/hooks.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-hooks-extended-test
	/tmp/vibesolaris-hooks-extended-test

test-project-index:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/project_index_test.c src/project_index.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-project-index-test
	/tmp/vibesolaris-project-index-test

test-tui-theme:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_theme_test.c src/tui_theme.c -o /tmp/vibesolaris-tui-theme-test
	/tmp/vibesolaris-tui-theme-test

test-tui-suggestions:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_suggestions_test.c src/tui_suggestions.c -o /tmp/vibesolaris-tui-suggestions-test
	/tmp/vibesolaris-tui-suggestions-test

test-tui-slash:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_slash_test.c src/tui_suggestions.c -o /tmp/vibesolaris-tui-slash-test
	/tmp/vibesolaris-tui-slash-test

test-tui-editor:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/tui_editor_test.c src/tui_editor.c -o /tmp/vibesolaris-tui-editor-test
	/tmp/vibesolaris-tui-editor-test

test-reasoning:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/reasoning_test.c src/reasoning.c -o /tmp/vibesolaris-reasoning-test
	/tmp/vibesolaris-reasoning-test

test-rewind:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/rewind_test.c src/rewind.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-rewind-test
	/tmp/vibesolaris-rewind-test

test-security-review:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/security_review_test.c src/code_review.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-security-review-test
	/tmp/vibesolaris-security-review-test

test-loop:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/loop_test.c src/loop.c $(AGENT_MCP_STUB) $(THREAD_LIBS) -o /tmp/vibesolaris-loop-test
	/tmp/vibesolaris-loop-test

test-workflow-engine:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/workflow_engine_test.c src/workflow_engine.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-workflow-engine-test
	/tmp/vibesolaris-workflow-engine-test

test-cross-session:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/cross_session_test.c src/cross_session.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-cross-session-test
	/tmp/vibesolaris-cross-session-test

test-phase9:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/phase9_test.c src/phase9.c src/hooks.c $(AGENT_MCP_STUB) -o /tmp/vibesolaris-phase9-test
	/tmp/vibesolaris-phase9-test

# No X11, libcurl or libcrypto link dependencies.
test-basic: test-keybindings test-provider-stream test-tui-stream test-todo test-edit test-memory test-skill test-coordinator test-tool-executor test-glob test-grep test-notebook test-monitor test-multi-edit test-lsp test-permission test-task test-checkpoint test-dream test-auto-mode test-agent-team test-plugin test-code-review test-init-cmd test-settings test-hooks-extended test-project-index test-tui-theme test-tui-suggestions test-tui-slash test-tui-editor test-reasoning test-rewind test-security-review test-loop test-workflow-engine test-cross-session test-phase9 test-computer-use-macos

# No X11, but these targets link libcurl and libcrypto.
test-headless-deps: test-goal test-agent test-agent-extended-tools test-agent-plan-deferral test-subagent test-workflow test-claude-oauth test-provider-cache

test-headless: test-basic test-headless-deps

# Backend test may interact with the desktop; opt in explicitly.
test-optional: test-computer-use

test: test-headless
