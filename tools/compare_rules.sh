#!/bin/sh
# SPDX-License-Identifier: MIT
# Do all these builds compute the same game? Each file holds one build's rules fingerprint
# (ofp_server --version, openfirepower --version, node tools/rules_wasm.js):
#   tools/compare_rules.sh rules-*.txt
set -eu
[ $# -gt 0 ] || { echo "usage: compare_rules.sh FILE..." >&2; exit 2; }
first=""; bad=0
for f in "$@"; do
    r=$(grep -o '[0-9a-f]\{8\}' "$f" | tail -1)
    printf '%-28s %s\n' "$(basename "$f" .txt)" "${r:-?}"
    [ -z "$first" ] && first=$r
    [ "$r" = "$first" ] || bad=1
done
if [ $bad -ne 0 ]; then echo "DIFFERENT: these builds would get out of step with each other" >&2; exit 1; fi
echo "all the same: $first"
