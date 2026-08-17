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

CORE = src/util.o src/http.o src/config.o src/secure_config.o src/sha256.o src/oauth.o src/mcp.o src/provider.o src/agent.o

all: vibesolaris vibesolaris-gui

vibesolaris: $(CORE) src/tui.o
	$(CC) $(LDFLAGS) -o vibesolaris $(CORE) src/tui.o $(CURL_LIBS) $(CRYPTO_LIBS)

vibesolaris-gui: $(CORE) src/gui.o
	$(CC) $(LDFLAGS) -o vibesolaris-gui $(CORE) src/gui.o $(CURL_LIBS) $(CRYPTO_LIBS) $(X11_LIBS) $(THREAD_LIBS)

src/util.o: src/util.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/util.c -o src/util.o

src/http.o: src/http.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/http.c -o src/http.o

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

src/tui.o: src/tui.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tui.c -o src/tui.o

src/gui.o: src/gui.c include/vibesolaris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/gui.c -o src/gui.o

clean:
	rm -f *.o src/*.o vibesolaris vibesolaris-gui
