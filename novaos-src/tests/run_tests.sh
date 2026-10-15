#!/bin/bash
# Runs every test suite.  From the repository root:  tests/run_tests.sh
# Needs: gcc (native), python3 + PIL + numpy (asset generation).  Uses AddressSanitizer + UBSan.
set -e
cd "$(dirname "$0")/.."
mkdir -p shots build
echo "== generate assets, host asset table, package corpus"
python3 gen_assets.py > /dev/null
python3 tools/gen_host_assets.py
python3 tests/gen_corpus.py
python3 tests/make_pydisk.py          # Python formats + fills a disk image; the C test suite mounts it
python3 tools/make_sdk.py > /dev/null  # regenerate the developer kit sources (sdk/)
python3 tests/test_sdk.py             # follows the SDK Quick Start in a staged copy; leaves build/sdkdisk.img for the C suite
CF="-DNOVA_HOST -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Ibuild -I."
rc=0
for t in test test_disk test_fast test_pkg; do
    echo "== build + run $t"
    gcc $CF $t.c build/assets_host.c -o build/host_$t
    if [ "$t" = test_fast ]; then build/host_$t 600 | tail -3 || rc=1; else build/host_$t | tail -${TAIL:-12} || rc=1; fi
done
echo "== same suites WITHOUT sanitizers: -O0, and -O2 with uninitialized stack variables poisoned (catches missing-NUL / uninit bugs ASan hides)"
for mode in "-O0" "-O2 -ftrivial-auto-var-init=pattern"; do
    for t in test_disk test_pkg; do
        gcc -DNOVA_HOST $mode -w -Ibuild -I. $t.c build/assets_host.c -o build/plain_$t
        build/plain_$t > build/plain_$t.log 2>&1 && echo "  [$mode] $t: pass ($(grep -c . build/plain_$t.log) lines)" || { echo "  [$mode] $t: FAILED"; grep -E "FAIL" build/plain_$t.log | head; rc=1; }
    done
done
echo "== Python re-reads the image the C kernel code modified (build/cdisk.img)"
python3 tests/verify_cdisk.py || rc=1
echo "== cross-check: package built by the C test is accepted by the Python reference tool; samples too"
python3 tools/mkpkg.py check build/cbuilt.powor build/a4.bin build/a5.bin build/a6.bin || rc=1
echo "== 32-bit freestanding kernel build (warnings as errors)"
gcc -m32 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-tree-loop-distribute-patterns -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Werror -Wno-unused-parameter -Ibuild -I. -c kernel.c -o build/kernel.o || rc=1
exit $rc
