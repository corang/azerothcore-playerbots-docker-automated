#!/usr/bin/env bash
# regen-raid-pet-growl-patch.sh — re-cut patches/0021-playerbot-raid-pet-no-growl.patch from the fork
# working tree at azerothcore-wotlk/modules/mod-playerbots.
#
# 0021: TogglePetSpellAutoCastAction never turns ON (and actively turns OFF) a hunter pet's Growl
# autocast while the bot is in a raid map or a raid group — pets were pulling aggro off the tanks.
#
# NOT baseline-aware: 0021 touches ONLY src/Ai/Base/Actions/PetsAction.cpp, and no other overlay or
# mod-era-talents patch touches that file (checked 2026-10-08). A plain scoped diff therefore emits
# exactly 0021's hunks. The prefix flags make the patch apply from the fork root, matching how
# setup.sh's apply_patches runs `git -C "$AC_DIR" apply`.
#
# LITERAL PATHS ONLY on every git line. zsh does not word-split an unquoted $var, so a
# `for f in $FILES` loop would cut an empty patch (this trap has bitten the WG/publish recipes).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AC="$ROOT/azerothcore-wotlk"
PB="$AC/modules/mod-playerbots"
OUT="$ROOT/patches/0021-playerbot-raid-pet-no-growl.patch"

[[ -d "$PB/.git" ]] || { echo "ERROR: $PB is not a git clone (run setup.sh first)" >&2; exit 1; }

git -C "$PB" diff \
  --src-prefix=a/modules/mod-playerbots/ \
  --dst-prefix=b/modules/mod-playerbots/ \
  -- src/Ai/Base/Actions/PetsAction.cpp \
  > "$OUT"

# Gates. A bad patch here plus a fork reset would orphan the work.
[[ -s "$OUT" ]] || { echo "GATE FAIL: generated patch is empty" >&2; exit 1; }
if grep '^diff --git' "$OUT" | grep -v ' a/modules/mod-playerbots/src/Ai/Base/Actions/PetsAction.cpp '; then
  echo "GATE FAIL: patch touches files other than src/Ai/Base/Actions/PetsAction.cpp (listed above)" >&2; exit 1
fi
grep -q "Growl" "$OUT" || { echo "GATE FAIL: patch does not mention Growl" >&2; exit 1; }
# The worktree must contain exactly this patch: reverse-applying it must be clean.
git -C "$AC" apply --reverse --check "$OUT" \
  || { echo "GATE FAIL: patch does not reverse-apply against the fork worktree" >&2; exit 1; }

echo "OK: wrote $OUT ($(wc -l < "$OUT") lines)"
git -C "$AC" apply --stat "$OUT" | sed 's/^/    /'
