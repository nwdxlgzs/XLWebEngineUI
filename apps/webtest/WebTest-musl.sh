#!/bin/sh
# Preserve the caller's working directory: that is the default UI root.
webtest_bin_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
exec "$webtest_bin_dir/../lib/ld-musl-x86_64.so.1" \
  --library-path "$webtest_bin_dir/../lib" "$webtest_bin_dir/WebTest.bin" "$@"
