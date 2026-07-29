#!/bin/sh
set -eu

app_bundle=$1
plugin_binary=$2
result_file=$3

rm -f "$result_file"
open -W -n "$app_bundle" --args "$plugin_binary" "$result_file"
test -f "$result_file"
grep -q '"status":"passed"' "$result_file"
