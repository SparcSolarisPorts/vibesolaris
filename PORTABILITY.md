# Solaris / illumos / Linux / BSD / Darwin portability notes

## Target matrix

The source intentionally uses a conservative Unix/POSIX subset and plain C front ends.

- Solaris 8 SPARC / x86: source target; external contemporary TLS/libcurl is expected.
- Solaris 9/10 SPARC / x86: source target.
- Solaris 11.4 SPARC / x86: primary packaging target.
- illumos distributions SPARC/x86 where the required X11/libcurl toolchain exists.
- Linux x86/x86-64, ARM64, SPARC, and SPARC64 where libcurl/libcrypto/X11 development libraries are available.
- FreeBSD, OpenBSD, NetBSD, and DragonFly on supported native architectures.
- Darwin/XNU/macOS on Intel, ARM64/Apple Silicon, and historical PowerPC/PowerPC64 with matching third-party dependencies.

## Why Xlib and stdio

The GUI uses Xlib directly instead of GTK or Qt. The TUI is plain terminal stdio instead of ncurses. This avoids pulling a modern desktop or terminal-widget stack onto old Solaris installations.

## Compiler

GCC is preferred. Oracle Developer Studio/Sun Studio `cc` is also a design target. `Makefile` allows `CC`, `CFLAGS`, and `LDFLAGS` overrides.

## TLS reality on Solaris 8

The application talks to modern HTTPS AI endpoints through libcurl. The limiting factor on a stock Solaris 8 installation is normally not the VibeSolaris C source; it is obtaining a libcurl + TLS library capable of negotiating the TLS versions/ciphers accepted by modern services.

## Building both ISAs on one Solaris 11.4 machine

SVR4 package creation is architecture metadata plus payload; it does not cross-compile binaries. To create both runnable SPARC and i386 packages from one host, install a compiler/sysroot for the opposite ISA and set the variables documented in `build.sh`/README. Without that, `build.sh` creates the native package and tells you exactly which cross-compiler variable is missing rather than falsely labeling a native binary as the other architecture.

## Solaris make compatibility (0.2.1)

The Makefile intentionally uses only traditional/POSIX make assignments (`=`), not GNU Make extensions such as `?=`. This allows `/usr/bin/make` on Solaris 8 through Solaris 11.4 to parse the project. `build.sh` passes compiler and flag variables on the make command line, so they still override the Makefile defaults.


## Linux

Linux is a first-class build target as of 0.3.1. `build.sh` uses `uname -s` to separate Linux packaging from Solaris packaging, and `uname -m` to retain native Linux architecture names such as `x86_64`, `aarch64`, `sparc`, `sparcv9`, and `sparc64`. The Linux build uses `pkg-config` for libcurl/X11 flags when available and falls back to `-lcurl -lX11`.

The GUI remains raw Xlib rather than GTK or Qt. This keeps the dependency surface small and works on X11 as well as XWayland. The TUI does not depend on ncurses.

Linux packages needed for compilation are the development variants of libcurl and X11; having the `curl` executable alone is not sufficient because `src/http.c` includes `<curl/curl.h>`.

## OAuth / PKCE portability (0.9.4)

The native OAuth client uses ordinary POSIX sockets and `select()`, so the same flow is shared by Linux, Solaris, and illumos. The callback binds only to IPv4 loopback. Interactive login needs a system browser launcher; the TUI prints the authorisation URL when automatic browser launch is unavailable.

PKCE and OAuth `state` generation require `/dev/urandom` or `/dev/random`. The implementation deliberately fails instead of falling back to a weak pseudo-random generator. The OAuth profile uses POSIX directory/file permissions (`0700`/`0600`); it is not encrypted at rest.


## BSD

`build.sh` recognizes FreeBSD, OpenBSD, NetBSD, and DragonFly. It prefers the platform compiler (`clang` or `gcc`) and uses `pkg-config`/`pkgconf` where available. The GUI remains plain Xlib and therefore works with native X11 desktops and X11 compatibility layers when libX11 development files are installed. BSD releases are emitted as portable tar bundles rather than pretending to create a native package-manager artifact on a different BSD.


## Darwin / macOS / XNU

Darwin is grouped with the BSD-derived Unix portability targets in this project, although XNU is not itself one of the four BSD systems listed above. `build.sh` recognises Darwin and normalises these machine families:

- `arm64` / `aarch64` -> `arm64`
- `x86_64` / `amd64` -> `x86_64`
- `i386` through `i686` -> `i686`
- `ppc` / `powerpc` -> `ppc`
- `ppc64` / `powerpc64` -> `ppc64`

The terminal client has no X11 dependency. The GUI remains deliberately pure Xlib, so macOS needs XQuartz or another X11 implementation rather than a Cocoa/AppKit port. The build driver checks `/opt/X11`, historical `/usr/X11R6`, MacPorts `/opt/local`, and pkg-config/pkgconf paths. Current Homebrew OpenSSL/curl prefixes are also detected when available.

PowerPC support is a **source/build-system target**, intended for historical Darwin/macOS machines with compatible curl, OpenSSL/libcrypto, X11, and a compiler. It is not claimed as a natively tested target in this release.


## Live activity and token accounting (0.9.5)

The core trace callback is plain C and has no terminal-library dependency. The TUI installs it only while an agent turn is running, so observable model/tool/MCP events are printed immediately instead of being buffered until the turn finishes.

Provider-reported input/output/total token fields are normalised in the shared core, so both the TUI and GUI use the same current-conversation counters. These counters are session data rather than encrypted configuration and reset with a new/cleared conversation.

## X11 clipboard and drag-and-drop (0.8.0)

The GUI requests pasted text through the X11 selection protocol (`CLIPBOARD` with `UTF8_STRING`, falling back to `STRING`) rather than interpreting Ctrl/Meta+V as typed text. It handles normal and incremental (`INCR`) transfers and supports middle-click PRIMARY paste.

File drag-and-drop uses XDND version 5 and requests `text/uri-list`. Only local readable regular files are attached; remote-host URIs and directories are rejected. No GTK/Qt drag-and-drop dependency is introduced. Rendering uses an X11 pixmap back buffer so cursor blinking and selection motion are copied to the visible window as completed frames instead of being painted in stages.
