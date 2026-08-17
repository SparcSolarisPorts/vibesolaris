#!/bin/sh
# SPDX-License-Identifier: Unlicense
# OPTIONAL: delegate /etc/vibesolaris to one local account. Normally no setup is needed: if /etc is not writable, VibeSolaris automatically uses ~/.vibesolaris for that user. Run this only when you deliberately want system-path storage.
set -eu

DIR=${VIBESOLARIS_GLOBAL_CONFIG_DIR:-/etc/vibesolaris}
TARGET_USER=${1:-${SUDO_USER:-}}

if [ "`id -u`" != 0 ]; then
    echo "This setup must run as root." >&2
    echo "Example: sudo $0 \"\$USER\"" >&2
    exit 1
fi
if [ -z "$TARGET_USER" ]; then
    echo "Usage: $0 USER" >&2
    exit 1
fi
if ! id "$TARGET_USER" >/dev/null 2>&1; then
    echo "Unknown local user: $TARGET_USER" >&2
    exit 1
fi

mkdir -p "$DIR"
chown "$TARGET_USER" "$DIR"
chmod 0700 "$DIR"
chmod g-s "$DIR" 2>/dev/null || true

# Preserve existing encrypted files, but ensure the delegated owner can update
# them.  Secrets remain readable only by the chosen account.
for f in "$DIR/config.enc" "$DIR/master.key"; do
    if [ -e "$f" ]; then
        chown "$TARGET_USER" "$f"
        chmod 0600 "$f"
    fi
done

echo "$DIR is ready for encrypted VibeSolaris autosave by $TARGET_USER"
echo "Other users without write access will automatically use ~/.vibesolaris instead."
