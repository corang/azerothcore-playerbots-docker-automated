#!/usr/bin/env bash
# regen-bwl-patch.sh — re-cut patches/0020-playerbot-bwl.patch from the fork working tree
# at azerothcore-wotlk/modules/mod-playerbots.
#
# NOT baseline-aware: 0020 touches ONLY src/Ai/Raid/BWL/, and no other overlay or mod-era-talents
# patch touches that tree (checked 2026-10-06). A plain scoped diff therefore emits exactly 0020's
# hunks. The prefix flags make the patch apply from the fork root, matching how setup.sh's
# apply_patches runs `git -C "$AC_DIR" apply`.
#
# LITERAL PATHS ONLY on every git line. zsh does not word-split an unquoted $var, so a
# `for f in $FILES` loop would cut an empty patch (this trap has bitten the WG/publish recipes).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AC="$ROOT/azerothcore-wotlk"
PB="$AC/modules/mod-playerbots"
OUT="$ROOT/patches/0020-playerbot-bwl.patch"

[[ -d "$PB/.git" ]] || { echo "ERROR: $PB is not a git clone (run setup.sh first)" >&2; exit 1; }
[[ -f "$PB/src/Ai/Raid/BWL/BwlWyrmguardRules.h" ]] \
  || { echo "ERROR: BwlWyrmguardRules.h missing — 0020 edits not present in the fork worktree" >&2; exit 1; }
[[ -f "$PB/src/Ai/Raid/BWL/BwlNefarianRules.h" ]] \
  || { echo "ERROR: BwlNefarianRules.h missing — 0020 Nefarian edits not present in the fork worktree" >&2; exit 1; }

# The new files ride along via intent-to-add so they appear in the diff. Unstage them afterwards so
# the fork index is left exactly as found.
git -C "$PB" add -N -- src/Ai/Raid/BWL/BwlWyrmguardRules.h src/Ai/Raid/BWL/BwlNefarianRules.h
git -C "$PB" diff \
  --src-prefix=a/modules/mod-playerbots/ \
  --dst-prefix=b/modules/mod-playerbots/ \
  -- src/Ai/Raid/BWL \
  > "$OUT"
git -C "$PB" reset -q -- src/Ai/Raid/BWL/BwlWyrmguardRules.h src/Ai/Raid/BWL/BwlNefarianRules.h

# Gates. A bad patch here plus a fork reset would orphan the work.
[[ -s "$OUT" ]] || { echo "GATE FAIL: generated patch is empty" >&2; exit 1; }
if grep '^diff --git' "$OUT" | grep -v ' a/modules/mod-playerbots/src/Ai/Raid/BWL/'; then
  echo "GATE FAIL: patch touches files outside src/Ai/Raid/BWL/ (listed above)" >&2; exit 1
fi
grep -q "BwlWyrmguardRules" "$OUT" || { echo "GATE FAIL: patch missing BwlWyrmguardRules.h" >&2; exit 1; }
grep -q "BwlNefarianRules" "$OUT" || { echo "GATE FAIL: patch missing BwlNefarianRules.h" >&2; exit 1; }
if grep -q "BWLNEF-DEBUG" "$OUT"; then
  echo "GATE FAIL: patch still contains temporary [BWLNEF-DEBUG] logging" >&2; exit 1
fi
if grep -q "BWLWG-DEBUG" "$OUT"; then
  echo "GATE FAIL: patch still contains temporary [BWLWG-DEBUG] logging" >&2; exit 1
fi
# The worktree must contain exactly this patch: reverse-applying it must be clean.
git -C "$AC" apply --reverse --check "$OUT" \
  || { echo "GATE FAIL: patch does not reverse-apply against the fork worktree" >&2; exit 1; }

echo "OK: wrote $OUT ($(wc -l < "$OUT") lines)"
git -C "$AC" apply --stat "$OUT" | sed 's/^/    /'
