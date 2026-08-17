#!/bin/sh
# SPDX-License-Identifier: Unlicense
# VibeSolaris build/package driver for Solaris, illumos, Linux, BSD, and Darwin/XNU.
# POSIX /bin/sh only: deliberately avoids bashisms for old Solaris/BSD systems.
set -eu

SCRIPT_DIR=`dirname "$0"`
ROOT=`cd "$SCRIPT_DIR" && pwd`
OUT="$ROOT/dist"
VERSION=0.10.5
HOST_OS=`uname -s 2>/dev/null || echo unknown`

mkdir -p "$OUT"

pickcc() {
    preferred="${1:-}"
    for c in "$preferred" gcc clang cc suncc; do
        [ -n "$c" ] || continue
        if command -v "$c" >/dev/null 2>&1; then
            echo "$c"
            return 0
        fi
    done
    return 1
}

linux_dep_help() {
    echo ""
    echo "Linux build dependencies are missing. Install the development packages, then run ./build.sh again."
    if command -v dnf >/dev/null 2>&1; then
        echo "  Fedora/RHEL/Rocky/Alma: sudo dnf install gcc make pkgconf-pkg-config libcurl-devel libX11-devel openssl-devel"
    elif command -v yum >/dev/null 2>&1; then
        echo "  RHEL/CentOS (yum):      sudo yum install gcc make pkgconfig libcurl-devel libX11-devel openssl-devel"
    elif command -v apt-get >/dev/null 2>&1; then
        echo "  Debian/Ubuntu:          sudo apt-get install build-essential pkg-config libcurl4-openssl-dev libx11-dev libssl-dev"
    elif command -v zypper >/dev/null 2>&1; then
        echo "  openSUSE/SLES:          sudo zypper install gcc make pkg-config libcurl-devel libX11-devel libopenssl-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "  Arch Linux:             sudo pacman -S --needed base-devel pkgconf curl libx11 openssl"
    else
        echo "  Required: C compiler, make, libcurl headers/library, OpenSSL libcrypto headers/library, and X11 headers/library for the GUI."
    fi
    echo ""
    echo "For a TUI-only build, X11 is optional: VS_NO_GUI=1 ./build.sh"
}

bsd_dep_help() {
    echo ""
    echo "BSD build dependencies are missing."
    case "$HOST_OS" in
        FreeBSD|DragonFly)
            echo "  $HOST_OS: pkg install curl libX11 pkgconf"
            echo "  clang/make and OpenSSL-compatible libcrypto are normally available in the base system."
            ;;
        OpenBSD)
            echo "  OpenBSD: pkg_add curl"
            echo "  Install the xbase sets for X11 GUI headers/libraries; clang, make, and LibreSSL libcrypto are in the base system."
            ;;
        NetBSD)
            echo "  NetBSD/pkgsrc: pkgin install curl libX11 pkg-config"
            echo "  A base compiler/make and OpenSSL-compatible libcrypto are also required."
            ;;
        *)
            echo "  Required: C compiler, make, libcurl, libcrypto/OpenSSL-compatible headers, and X11 headers for the GUI."
            ;;
    esac
    echo ""
    echo "For a TUI-only build, X11 is optional: VS_NO_GUI=1 ./build.sh"
}


darwin_dep_help() {
    echo ""
    echo "Darwin/macOS build dependencies are missing."
    echo "  Current macOS: install Xcode Command Line Tools, libcurl/OpenSSL development files, and XQuartz for the GUI."
    if command -v brew >/dev/null 2>&1; then
        echo "  Homebrew example: brew install pkg-config curl openssl@3"
        echo "  X11 GUI: install XQuartz (or set VS_NO_GUI=1 for the TUI only)."
    elif command -v port >/dev/null 2>&1; then
        echo "  MacPorts example: sudo port install curl openssl3 pkgconfig xorg-libX11"
    else
        echo "  On historical PowerPC Darwin/macOS, MacPorts/Fink or manually built curl/OpenSSL/X11 may be used."
    fi
    echo ""
    echo "For a TUI-only build, X11 is optional: VS_NO_GUI=1 ./build.sh"
}

probe_deps() {
    cc="$1"
    cppflags="$2"
    cflags="$3"
    ldflags="$4"
    curl_libs="$5"
    crypto_libs="$6"
    x11_libs="$7"
    want_gui="$8"
    src="$OUT/.vs-conftest.c"
    bin="$OUT/.vs-conftest"

    cat > "$src" <<'EOC'
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
int main(void) {
    CURL *c = curl_easy_init();
    unsigned char b[1];
    const EVP_CIPHER *x = EVP_aes_256_cbc();
    if (c) curl_easy_cleanup(c);
    (void)x;
    return RAND_bytes(b,1) == 1 ? 0 : 0;
}
EOC
    if ! $cc $cppflags $cflags "$src" $ldflags $curl_libs $crypto_libs -o "$bin" >/dev/null 2>&1; then
        echo "ERROR: cannot compile/link the libcurl + libcrypto dependency test with $cc." >&2
        echo "       Missing curl/curl.h, OpenSSL-compatible crypto headers, libraries, or matching flags." >&2
        rm -f "$src" "$bin"
        return 2
    fi

    if [ "$want_gui" = yes ]; then
        cat > "$src" <<'EOC'
#include <X11/Xlib.h>
int main(void) { Display *d = XOpenDisplay((char*)0); if (d) XCloseDisplay(d); return 0; }
EOC
        if ! $cc $cppflags $cflags "$src" $ldflags $x11_libs -o "$bin" >/dev/null 2>&1; then
            echo "ERROR: cannot compile/link an X11 test with $cc." >&2
            echo "       Missing X11/Xlib.h, libX11, or matching compiler/library flags." >&2
            rm -f "$src" "$bin"
            return 3
        fi
    fi

    rm -f "$src" "$bin"
    return 0
}

make_solaris_pkg() {
    arch="$1"
    root="$2"
    pkgdir="$OUT/pkg-$arch"
    spool="$OUT/spool-$arch"

    rm -rf "$pkgdir" "$spool"
    mkdir -p "$pkgdir" "$spool"

    cat > "$pkgdir/pkginfo" <<EOP
PKG=VIBEvibesolaris
NAME=VibeSolaris AI coding client
ARCH=$arch
VERSION=$VERSION
CATEGORY=application
BASEDIR=/
VENDOR=VibeSolaris
EMAIL=local@localhost
EOP

    {
        echo 'i pkginfo'
        (cd "$root" && find . -type d ! -name . -print) | sed 's#^\./##' | awk '{mode="0755"; if ($0 == "etc/vibesolaris") mode="0700"; print "d none /"$0" "mode" root bin"}'
        (cd "$root" && find . -type f -print) | sed 's#^\./##' | awk '{mode="0444"; if ($0 ~ /^usr\/local\/(bin|sbin)\//) mode="0555"; print "f none /"$0" "mode" root bin"}'
    } > "$pkgdir/prototype"

    if command -v pkgmk >/dev/null 2>&1 && command -v pkgtrans >/dev/null 2>&1; then
        pkgfile="$OUT/vibesolaris-$VERSION-solaris-$arch.pkg"
        rm -f "$pkgfile"
        (cd "$pkgdir" && pkgmk -o -r "$root" -d "$spool")
        pkgtrans -s "$spool" "$pkgfile" VIBEvibesolaris
        echo "created $pkgfile"
    else
        tarfile="$OUT/vibesolaris-$VERSION-solaris-$arch.tar"
        rm -f "$tarfile"
        tar -cf "$tarfile" -C "$root" .
        echo "pkgmk/pkgtrans unavailable; created $tarfile"
    fi
}

make_tar_bundle() {
    oslabel="$1"
    arch="$2"
    root="$3"
    tarfile="$OUT/vibesolaris-$VERSION-$oslabel-$arch.tar.gz"

    rm -f "$tarfile"
    if tar -czf "$tarfile" -C "$root" . 2>/dev/null; then
        :
    else
        plain="$OUT/vibesolaris-$VERSION-$oslabel-$arch.tar"
        rm -f "$plain"
        tar -cf "$plain" -C "$root" .
        if command -v gzip >/dev/null 2>&1; then
            gzip -f "$plain"
            tarfile="$plain.gz"
        else
            tarfile="$plain"
        fi
    fi
    echo "created $tarfile"
}

install_tree() {
    root="$1"
    want_gui="$2"
    rm -rf "$root"
    mkdir -p "$root/usr/local/bin" "$root/usr/local/sbin" "$root/usr/local/share/vibesolaris" "$root/etc/vibesolaris"

    cp "$ROOT/vibesolaris" "$root/usr/local/bin/"
    if [ "$want_gui" = yes ]; then
        cp "$ROOT/vibesolaris-gui" "$root/usr/local/bin/"
    fi
    cp "$ROOT/setup-system-config.sh" "$root/usr/local/sbin/vibesolaris-config-setup"
    chmod 0555 "$root/usr/local/sbin/vibesolaris-config-setup"
    cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/CACHING.md" "$ROOT/PROVIDERS.md" "$ROOT/PORTABILITY.md" "$ROOT/OAUTH.md" "$ROOT/SECURITY.md" "$ROOT/MCP.md" "$ROOT/examples/AGENT.MD" "$root/usr/local/share/vibesolaris/"
    cp "$ROOT/etc/vibesolaris/README" "$root/etc/vibesolaris/README"
    chmod 0700 "$root/etc/vibesolaris"
    chmod g-s "$root/etc/vibesolaris" 2>/dev/null || true

    if [ "$HOST_OS" = Linux ] && [ "$want_gui" = yes ]; then
        mkdir -p "$root/usr/local/share/applications"
        cat > "$root/usr/local/share/applications/vibesolaris.desktop" <<'EOD'
[Desktop Entry]
Type=Application
Name=VibeSolaris
Comment=Lightweight AI coding chat client
Exec=/usr/local/bin/vibesolaris-gui
Terminal=false
Categories=Development;Utility;
EOD
    fi
}

build_one() {
    arch="$1"
    cc="$2"
    user_cflags="$3"
    user_ldflags="$4"
    cppflags="$5"
    curl_libs="$6"
    crypto_libs="$7"
    x11_libs="$8"
    want_gui="$9"
    root="$OUT/root-$arch"

    warnflags=""
    ccbase=`basename "$cc"`
    case "$ccbase" in
        gcc|gcc-*|clang|clang-*) warnflags="-Wall" ;;
        *) warnflags="" ;;
    esac

    echo "== building $HOST_OS/$arch with $cc =="
    echo "   curl libs:   $curl_libs"
    echo "   crypto libs: $crypto_libs"
    if [ "$want_gui" = yes ]; then echo "   X11 libs:    $x11_libs"; fi

    if ! probe_deps "$cc" "$cppflags" "-O2 $user_cflags" "$user_ldflags" "$curl_libs" "$crypto_libs" "$x11_libs" "$want_gui"; then
        case "$HOST_OS" in
            Linux) linux_dep_help ;;
            FreeBSD|OpenBSD|NetBSD|DragonFly) bsd_dep_help ;;
            Darwin) darwin_dep_help ;;
        esac
        exit 1
    fi

    (cd "$ROOT" && make clean)
    if [ "$want_gui" = yes ]; then
        (cd "$ROOT" && make \
            CC="$cc" \
            CPPFLAGS="$cppflags" \
            CFLAGS="-O2 $warnflags $user_cflags" \
            LDFLAGS="$user_ldflags" \
            CURL_LIBS="$curl_libs" \
            CRYPTO_LIBS="$crypto_libs" \
            X11_LIBS="$x11_libs" all)
    else
        (cd "$ROOT" && make \
            CC="$cc" \
            CPPFLAGS="$cppflags" \
            CFLAGS="-O2 $warnflags $user_cflags" \
            LDFLAGS="$user_ldflags" \
            CURL_LIBS="$curl_libs" \
            CRYPTO_LIBS="$crypto_libs" vibesolaris)
    fi

    install_tree "$root" "$want_gui"

    case "$HOST_OS" in
        SunOS) make_solaris_pkg "$arch" "$root" ;;
        Linux) make_tar_bundle linux "$arch" "$root" ;;
        FreeBSD) make_tar_bundle freebsd "$arch" "$root" ;;
        OpenBSD) make_tar_bundle openbsd "$arch" "$root" ;;
        NetBSD) make_tar_bundle netbsd "$arch" "$root" ;;
        DragonFly) make_tar_bundle dragonfly "$arch" "$root" ;;
        Darwin) make_tar_bundle darwin "$arch" "$root" ;;
        *) make_tar_bundle `echo "$HOST_OS" | tr '[:upper:]' '[:lower:]'` "$arch" "$root" ;;
    esac
}

# Dependency flags: pkg-config is preferred when available, with plain library
# flags as portable fallbacks for Solaris and the BSD base systems.
CURL_CFLAGS="${VS_CURL_CFLAGS:-}"
CURL_LIBS="${VS_CURL_LIBS:-}"
CRYPTO_CFLAGS="${VS_CRYPTO_CFLAGS:-}"
CRYPTO_LIBS="${VS_CRYPTO_LIBS:-}"
X11_CFLAGS="${VS_X11_CFLAGS:-}"
X11_LIBS="${VS_X11_LIBS:-}"

PC=""
if command -v pkg-config >/dev/null 2>&1; then PC=pkg-config
elif command -v pkgconf >/dev/null 2>&1; then PC=pkgconf
fi

if [ -n "$PC" ]; then
    if [ -z "$CURL_CFLAGS" ] && $PC --exists libcurl 2>/dev/null; then CURL_CFLAGS=`$PC --cflags libcurl`; fi
    if [ -z "$CURL_LIBS" ] && $PC --exists libcurl 2>/dev/null; then CURL_LIBS=`$PC --libs libcurl`; fi
    if [ -z "$CRYPTO_CFLAGS" ] && $PC --exists libcrypto 2>/dev/null; then CRYPTO_CFLAGS=`$PC --cflags libcrypto`;
    elif [ -z "$CRYPTO_CFLAGS" ] && $PC --exists openssl 2>/dev/null; then CRYPTO_CFLAGS=`$PC --cflags openssl`; fi
    if [ -z "$CRYPTO_LIBS" ] && $PC --exists libcrypto 2>/dev/null; then CRYPTO_LIBS=`$PC --libs libcrypto`;
    elif [ -z "$CRYPTO_LIBS" ] && $PC --exists openssl 2>/dev/null; then CRYPTO_LIBS=`$PC --libs openssl`; fi
    if [ -z "$X11_CFLAGS" ] && $PC --exists x11 2>/dev/null; then X11_CFLAGS=`$PC --cflags x11`; fi
    if [ -z "$X11_LIBS" ] && $PC --exists x11 2>/dev/null; then X11_LIBS=`$PC --libs x11`; fi
fi

# Common BSD package prefixes when pkg-config/pkgconf metadata is unavailable.
case "$HOST_OS" in
    FreeBSD|DragonFly)
        [ -n "$CURL_CFLAGS" ] || CURL_CFLAGS='-I/usr/local/include'
        [ -n "$CURL_LIBS" ] || CURL_LIBS='-L/usr/local/lib -lcurl'
        [ -n "$X11_CFLAGS" ] || X11_CFLAGS='-I/usr/local/include'
        [ -n "$X11_LIBS" ] || X11_LIBS='-L/usr/local/lib -lX11'
        ;;
    OpenBSD)
        [ -n "$CURL_CFLAGS" ] || CURL_CFLAGS='-I/usr/local/include'
        [ -n "$CURL_LIBS" ] || CURL_LIBS='-L/usr/local/lib -lcurl'
        [ -n "$X11_CFLAGS" ] || X11_CFLAGS='-I/usr/X11R6/include'
        [ -n "$X11_LIBS" ] || X11_LIBS='-L/usr/X11R6/lib -lX11'
        ;;
    NetBSD)
        [ -n "$CURL_CFLAGS" ] || CURL_CFLAGS='-I/usr/pkg/include'
        [ -n "$CURL_LIBS" ] || CURL_LIBS='-L/usr/pkg/lib -lcurl'
        [ -n "$X11_CFLAGS" ] || X11_CFLAGS='-I/usr/pkg/include'
        [ -n "$X11_LIBS" ] || X11_LIBS='-L/usr/pkg/lib -lX11'
        ;;
esac

# Darwin/XNU: XQuartz normally installs under /opt/X11.  Current Homebrew
# OpenSSL is keg-only, so use its prefix when pkg-config did not find it.
if [ "$HOST_OS" = Darwin ]; then
    if command -v brew >/dev/null 2>&1; then
        if [ -z "$CRYPTO_CFLAGS" ] || [ -z "$CRYPTO_LIBS" ]; then
            OSSL_PREFIX=`brew --prefix openssl@3 2>/dev/null || true`
            if [ -n "$OSSL_PREFIX" ]; then
                [ -n "$CRYPTO_CFLAGS" ] || CRYPTO_CFLAGS="-I$OSSL_PREFIX/include"
                [ -n "$CRYPTO_LIBS" ] || CRYPTO_LIBS="-L$OSSL_PREFIX/lib -lcrypto"
            fi
        fi
        if [ -z "$CURL_CFLAGS" ] || [ -z "$CURL_LIBS" ]; then
            CURL_PREFIX=`brew --prefix curl 2>/dev/null || true`
            if [ -n "$CURL_PREFIX" ]; then
                [ -n "$CURL_CFLAGS" ] || CURL_CFLAGS="-I$CURL_PREFIX/include"
                [ -n "$CURL_LIBS" ] || CURL_LIBS="-L$CURL_PREFIX/lib -lcurl"
            fi
        fi
    fi
    if [ -z "$CRYPTO_CFLAGS" ] && [ -d /opt/local/include ]; then CRYPTO_CFLAGS='-I/opt/local/include'; fi
    if [ -z "$CRYPTO_LIBS" ] && [ -d /opt/local/lib ]; then CRYPTO_LIBS='-L/opt/local/lib -lcrypto'; fi
    if [ -z "$CURL_CFLAGS" ] && [ -d /opt/local/include ]; then CURL_CFLAGS='-I/opt/local/include'; fi
    if [ -z "$CURL_LIBS" ] && [ -d /opt/local/lib ]; then CURL_LIBS='-L/opt/local/lib -lcurl'; fi
    if [ -z "$X11_CFLAGS" ]; then
        if [ -d /opt/X11/include ]; then X11_CFLAGS='-I/opt/X11/include';
        elif [ -d /usr/X11R6/include ]; then X11_CFLAGS='-I/usr/X11R6/include';
        elif [ -d /opt/local/include ]; then X11_CFLAGS='-I/opt/local/include'; fi
    fi
    if [ -z "$X11_LIBS" ]; then
        if [ -d /opt/X11/lib ]; then X11_LIBS='-L/opt/X11/lib -lX11';
        elif [ -d /usr/X11R6/lib ]; then X11_LIBS='-L/usr/X11R6/lib -lX11';
        elif [ -d /opt/local/lib ]; then X11_LIBS='-L/opt/local/lib -lX11'; fi
    fi
fi

[ -n "$CURL_LIBS" ] || CURL_LIBS='-lcurl'
[ -n "$CRYPTO_LIBS" ] || CRYPTO_LIBS='-lcrypto'
[ -n "$X11_LIBS" ] || X11_LIBS='-lX11'
CPPFLAGS="-Iinclude $CURL_CFLAGS $CRYPTO_CFLAGS $X11_CFLAGS ${VS_CPPFLAGS:-}"

WANT_GUI=yes
if [ "${VS_NO_GUI:-0}" = 1 ]; then WANT_GUI=no; fi

if ! command -v make >/dev/null 2>&1; then
    echo "No make utility found." >&2
    case "$HOST_OS" in
        Linux) linux_dep_help ;;
        FreeBSD|OpenBSD|NetBSD|DragonFly) bsd_dep_help ;;
        Darwin) darwin_dep_help ;;
    esac
    exit 1
fi

NATIVE_CC=`pickcc "${VS_CC:-}" || true`
if [ -z "$NATIVE_CC" ]; then
    echo "No C compiler found (tried VS_CC, gcc, clang, cc, suncc)" >&2
    case "$HOST_OS" in
        Linux) linux_dep_help ;;
        FreeBSD|OpenBSD|NetBSD|DragonFly) bsd_dep_help ;;
        Darwin) darwin_dep_help ;;
    esac
    exit 1
fi

native=`uname -m 2>/dev/null || uname -p 2>/dev/null || echo unknown`
case "$HOST_OS:$native" in
    SunOS:sparc*) native_arch=sparc ;;
    SunOS:i386|SunOS:i86pc|SunOS:x86_64|SunOS:amd64) native_arch=i386 ;;
    Linux:x86_64|Linux:amd64|FreeBSD:amd64|OpenBSD:amd64|NetBSD:amd64|DragonFly:x86_64|Darwin:x86_64|Darwin:amd64) native_arch=x86_64 ;;
    Linux:aarch64|Linux:arm64|FreeBSD:aarch64|OpenBSD:arm64|NetBSD:aarch64|Darwin:arm64|Darwin:aarch64) native_arch=arm64 ;;
    Linux:i386|Linux:i486|Linux:i586|Linux:i686|FreeBSD:i386|OpenBSD:i386|NetBSD:i386|Darwin:i386|Darwin:i486|Darwin:i586|Darwin:i686) native_arch=i686 ;;
    Linux:sparc64|Linux:sparcv9|OpenBSD:sparc64|NetBSD:sparc64) native_arch=sparc64 ;;
    Linux:sparc|Linux:sparc32) native_arch=sparc ;;
    Darwin:ppc|Darwin:powerpc|Darwin:PowerPC|Darwin:Power\ Macintosh) native_arch=ppc ;;
    Darwin:ppc64|Darwin:powerpc64) native_arch=ppc64 ;;
    *) native_arch="$native" ;;
esac

build_one "$native_arch" "$NATIVE_CC" "${VS_NATIVE_CFLAGS:-}" "${VS_NATIVE_LDFLAGS:-}" "$CPPFLAGS" "$CURL_LIBS" "$CRYPTO_LIBS" "$X11_LIBS" "$WANT_GUI"

# Cross-ISA packaging remains a Solaris-specific feature of this script.
if [ "$HOST_OS" = SunOS ]; then
    if [ "$native_arch" = sparc ]; then
        if [ -n "${VS_X64_CC:-}" ]; then
            build_one i386 "$VS_X64_CC" "${VS_X64_CFLAGS:-}" "${VS_X64_LDFLAGS:-}" "$CPPFLAGS" "$CURL_LIBS" "$CRYPTO_LIBS" "$X11_LIBS" "$WANT_GUI"
        else
            echo "NOTE: x64 Solaris package skipped; set VS_X64_CC (and target flags/sysroot if needed)."
        fi
    else
        if [ -n "${VS_SPARC_CC:-}" ]; then
            build_one sparc "$VS_SPARC_CC" "${VS_SPARC_CFLAGS:-}" "${VS_SPARC_LDFLAGS:-}" "$CPPFLAGS" "$CURL_LIBS" "$CRYPTO_LIBS" "$X11_LIBS" "$WANT_GUI"
        else
            echo "NOTE: SPARC Solaris package skipped; set VS_SPARC_CC (and target flags/sysroot if needed)."
        fi
    fi
fi

echo "Artifacts are in $OUT"
