#!/usr/bin/env bash
# Build the core (no plugin wrappers) with ASan+UBSan and with TSan and run the
# non-windowed, non-perf suite under each. Used for the release matrix
# (docs/juce-removal-baseline/RMJ-100-release-matrix-report.md).
#
# Usage: scripts/run_sanitizers.sh [asan|tsan|all]   (default: all)
# Build trees: build-asan, build-tsan. Requires the patched qatsh/ checkout.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(uname -m)"
which="${1:-all}"

run() {
    local name="$1" flags="$2"
    local dir="$REPO/build-$name"
    echo "=== $name ($flags) in $dir"
    cmake -S "$REPO" -B "$dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DBUILD_PLUGIN=OFF \
        -DSNDFILE_INCLUDE_DIR="$REPO/vendor/sndfile-arm64/include" \
        -DSNDFILE_LIBRARY="$REPO/vendor/sndfile-arm64/lib/libsndfile.dylib" \
        -DCMAKE_C_FLAGS="$flags -fno-omit-frame-pointer" \
        -DCMAKE_CXX_FLAGS="$flags -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" >/dev/null
    cmake --build "$dir" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 \
        ctest --test-dir "$dir" -j4 -LE 'perf|windowed' --timeout 900 --output-on-failure
}

case "$which" in
    asan) run asan "-fsanitize=address,undefined -fno-sanitize-recover=undefined" ;;
    tsan) run tsan "-fsanitize=thread" ;;
    all)
        run asan "-fsanitize=address,undefined -fno-sanitize-recover=undefined"
        run tsan "-fsanitize=thread"
        ;;
    *) echo "usage: $0 [asan|tsan|all]" >&2; exit 2 ;;
esac
