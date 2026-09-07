#!/bin/bash
set -e

echo "=== Verifying backup exists before touching anything ==="
if [ ! -d "../backup-2026-09-07" ]; then
    echo "ERROR: ../backup-2026-09-07 not found. Refusing to clean up"
    echo "without a confirmed backup in place. Run make_backup.sh first."
    exit 1
fi
echo "Backup confirmed present at ../backup-2026-09-07"
echo ""

echo "=== Stage 1: removing confirmed-safe junk files ==="
for f in .agent.md.swp agent_bak hardware_bak AGENTS.bak; do
    if [ -e "$f" ]; then
        rm -v "$f"
    else
        echo "(already absent: $f)"
    fi
done
echo ""

echo "=== Checking .gitignore for build-* coverage before touching any build directory ==="
if grep -qE '^build' .gitignore 2>/dev/null; then
    echo "Confirmed: build* is covered by .gitignore. Safe to proceed."
else
    echo "WARNING: could not confirm build* is gitignored."
    echo "Showing .gitignore contents for manual review:"
    cat .gitignore 2>/dev/null || echo "(.gitignore not found)"
    echo ""
    read -p "Proceed with removing stale build directories anyway? (yes/no): " confirm
    if [ "$confirm" != "yes" ]; then
        echo "Stopping before Stage 3. Stage 1 cleanup above already completed."
        exit 0
    fi
fi
echo ""

echo "=== Stage 3: removing stale build directories, keeping build-debug and build-production ==="
KEEP=("build-debug" "build-production")
COUNT=0
for d in build*/; do
    d="${d%/}"
    skip=false
    for k in "${KEEP[@]}"; do
        if [ "$d" == "$k" ]; then
            skip=true
        fi
    done
    if [ "$skip" == "true" ]; then
        echo "(keeping: $d)"
    else
        rm -rf "$d"
        echo "(removed: $d)"
        COUNT=$((COUNT+1))
    fi
done
echo ""
echo "Removed $COUNT stale build directories. Kept: ${KEEP[*]}"
echo ""

echo "=== Cleanup complete ==="
echo "Not touched by this script, still needs your own judgment:"
echo "  - 28.08.2026/ (dated snapshot folder)"
echo "  - *-prompt.md files (past Codex task instructions)"
echo "  - v2-sensing.md, v3-simulation-rig.md, play-*-listening-test.md,"
echo "    buzzer-tone-comparison.md (likely superseded design docs)"
echo ""
echo "A verified backup exists at ../backup-2026-09-07 if anything"
echo "removed above turns out to be needed after all."
