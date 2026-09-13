#!/bin/sh
#
# make-ai-context.sh
#
# Create a single plain-text snapshot of the important VibeSolaris files
# for giving the project to an AI model.
#
# Designed to stay compatible with POSIX /bin/sh and old Solaris systems.
#
# Usage:
#   ./make-ai-context.sh
#   ./make-ai-context.sh my-vibesolaris-context.txt
#

set -eu

# Assume this script lives in the repository root.
SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd)
ROOT=$SCRIPT_DIR

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [output-file]" >&2
    exit 2
fi

if [ "$#" -eq 1 ]; then
    case "$1" in
        /*) OUT=$1 ;;
        *)  OUT=$ROOT/$1 ;;
    esac
else
    OUT=$ROOT/VIBESOLARIS_AI_CONTEXT.txt
fi

TMPBASE=${TMPDIR:-/tmp}/vibesolaris-ai-context.$$
ALL_FILES=$TMPBASE.all
IMPORTANT_FILES=$TMPBASE.important

cleanup()
{
    rm -f "$ALL_FILES" "$IMPORTANT_FILES"
}
trap cleanup EXIT HUP INT TERM

cd "$ROOT"

# Prefer Git's view of the repository. This avoids build products and ignored
# files automatically. If Git is unavailable, fall back to find(1).
if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git ls-files > "$ALL_FILES"
else
    find . -type f -print | sed 's|^\./||' > "$ALL_FILES"
fi

# If this exporter has not been committed yet, include it anyway.
SCRIPT_NAME=$(basename "$0")
if [ -f "$ROOT/$SCRIPT_NAME" ]; then
    printf '%s\n' "$SCRIPT_NAME" >> "$ALL_FILES"
fi

# Decide which repository files are useful to an AI.
#
# We intentionally include:
#   - all program source and headers
#   - tests and examples
#   - VibeSolaris configuration examples
#   - documentation
#   - build/install files
#
# We intentionally exclude:
#   - compiled/generated files
#   - images/archives
#   - private keys, encrypted config, and common credential files
is_important()
{
    p=$1
    base=`basename "$p"`

    case "$p" in
        VIBESOLARIS_AI_CONTEXT.txt|AI_CONTEXT.txt|*"/VIBESOLARIS_AI_CONTEXT.txt"|*"/AI_CONTEXT.txt")
            return 1
            ;;
        .git/*|build/*|dist/*|out/*|coverage/*)
            return 1
            ;;
        *.o|*.a|*.so|*.so.*|*.dylib|*.dll|*.exe|*.bin|*.class|*.jar)
            return 1
            ;;
        *.png|*.jpg|*.jpeg|*.gif|*.webp|*.bmp|*.ico|*.pdf)
            return 1
            ;;
        *.zip|*.tar|*.tgz|*.gz|*.bz2|*.xz|*.7z)
            return 1
            ;;
        *.log|*.core|core)
            return 1
            ;;
        .env|*/.env|.env.local|*/.env.local|.env.production|*/.env.production)
            return 1
            ;;
        config.enc|*/config.enc|master.key|*/master.key|credentials.json|*/credentials.json)
            return 1
            ;;
        *.pem|*.p12|*.pfx|*.keystore)
            return 1
            ;;
    esac

    case "$p" in
        # Main implementation.
        src/*|include/*)
            return 0
            ;;

        # Tests, examples, packaged configuration, and future documentation.
        tests/*|examples/*|etc/vibesolaris/*|docs/*)
            return 0
            ;;

        # CI/build metadata can be useful when diagnosing portability/build issues.
        .github/workflows/*)
            return 0
            ;;

        # Important root-level project files.
        Makefile|makefile|GNUmakefile|LICENSE|LICENSE.*|COPYING|COPYING.*)
            return 0
            ;;
        README|README.*|*.md|*.txt|*.sh|*.mk|*.in|*.conf|*.ini|*.toml|*.yaml|*.yml|*.json)
            # Only include these automatically when they are at the repository
            # root. Files under the important directories were handled above.
            case "$p" in
                */*) return 1 ;;
                *)   return 0 ;;
            esac
            ;;
        .gitignore|.gitattributes|.editorconfig)
            return 0
            ;;
        "$SCRIPT_NAME")
            return 0
            ;;
    esac

    return 1
}

: > "$IMPORTANT_FILES"

while IFS= read -r path
do
    [ -n "$path" ] || continue
    if is_important "$path" && [ -f "$ROOT/$path" ]; then
        printf '%s\n' "$path" >> "$IMPORTANT_FILES"
    fi
done < "$ALL_FILES"

# Stable ordering makes successive snapshots easy to diff.
sort -u "$IMPORTANT_FILES" -o "$IMPORTANT_FILES"

FILE_COUNT=`wc -l < "$IMPORTANT_FILES" | tr -d ' '`

{
    echo "VIBESOLARIS AI CONTEXT"
    echo "======================"
    echo
    echo "This file is a plain-text snapshot of the important VibeSolaris"
    echo "source, headers, build files, tests, examples, configuration, and"
    echo "documentation. Each file begins with a clear FILE: marker."
    echo
    echo "When analysing this snapshot:"
    echo "  * Treat FILE: markers as file boundaries."
    echo "  * Prefer current source code over documentation if they disagree."
    echo "  * Keep Solaris/Unix portability requirements in mind."
    echo "  * Do not assume GNU-only tools or Linux-only APIs are available."
    echo
    echo "Generated: `date '+%Y-%m-%d %H:%M:%S %Z'`"
    echo "Files included: $FILE_COUNT"

    if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        branch=`git symbolic-ref --quiet --short HEAD 2>/dev/null || echo detached`
        commit=`git rev-parse HEAD 2>/dev/null || echo unknown`
        echo "Git branch: $branch"
        echo "Git commit: $commit"
    fi

    echo
    echo "FILE INDEX"
    echo "=========="
    cat "$IMPORTANT_FILES"
    echo
    echo "BEGIN FILE CONTENTS"
    echo "==================="

    while IFS= read -r path
    do
        [ -f "$ROOT/$path" ] || continue

        echo
        echo "======================================================================"
        echo "FILE: $path"
        echo "======================================================================"
        cat "$ROOT/$path"

        # Always put the next marker on a fresh line, even if the source file
        # itself does not end with a newline.
        echo
    done < "$IMPORTANT_FILES"

    echo
    echo "===================="
    echo "END VIBESOLARIS CONTEXT"
    echo "===================="
} > "$OUT"

echo "Created: $OUT"
echo "Included $FILE_COUNT files."
echo "Size: `wc -c < "$OUT" | tr -d ' '` bytes."
