#!/usr/bin/env bash
#
# mayhem/build.sh — builds, from one pass:
#   1. the Beam core libraries (BVM_ONLY: utility/core/pow/p2p/bvm) with ASan+UBSan+SanCov  -> mayhem/build/fuzz
#   2. the four fuzz harnesses + their standalone reproducers                                 -> /mayhem/<name>[-standalone]
#   3. the same libraries again with the project's normal flags, plus the upstream BVM/EVM unit
#      tests and a known-answer probe (mayhem/kat.cc) for mayhem/test.sh                       -> mayhem/build/test
# Everything is resolved from the image (apt boost/openssl, the two git submodules checked out into
# 3rdparty/); nothing is fetched, so the air-gapped re-run works.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

for sub in 3rdparty/secp256k1/src 3rdparty/re2/re2; do
    [ -d "$sub" ] || { echo "build.sh: submodule content missing ($sub); check out with submodules" >&2; exit 1; }
done

# Relaxed UBSan check: bvm/evm.cpp does memcpy(NULL, src, 0) for zero-length code/data copies; with
# halting sanitizers that would stop every EVM execution at its first CODECOPY. ASan and the rest of UBSan stay fatal.
UBSAN_RELAX="-fno-sanitize=nonnull-attribute,bool"   # memcpy(NULL,..,0) in evm.cpp; yas loads raw wire bytes into bool
# SanCov instrumentation for the library code itself (the harness TU gets it from $LIB_FUZZING_ENGINE).
SANCOV="-fsanitize=fuzzer-no-link"

FUZZ_BUILD="$SRC/mayhem/build/fuzz"
TEST_BUILD="$SRC/mayhem/build/test"

CMAKE_COMMON=(-G Ninja
    -DBEAM_BVM_ONLY=ON -DBEAM_TESTS_ENABLED=OFF
    -DBEAM_ATOMIC_SWAP_SUPPORT=OFF -DBEAM_LASER_SUPPORT=OFF -DBEAM_IPFS_SUPPORT=OFF -DBEAM_HW_WALLET=OFF
    -DBEAM_WALLET_CLIENT_LIBRARY=OFF)
# The compiler is picked up from the exported CC/CXX on the first configure only: passing
# -DCMAKE_C_COMPILER=clang again on a re-run mismatches the cached full path and wipes the cache.
LIB_TARGETS=(bvm core pow p2p utility)

# ---------------------------------------------------------------- 1. sanitized + instrumented libraries
FUZZ_CFLAGS="$SANITIZER_FLAGS $UBSAN_RELAX $SANCOV $DEBUG_FLAGS -O1"
cmake -S "$SRC" -B "$FUZZ_BUILD" "${CMAKE_COMMON[@]}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="$FUZZ_CFLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_CFLAGS" -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -DNDEBUG -Wno-error" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -DNDEBUG -Wno-error" \
    -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS" >/dev/null
cmake --build "$FUZZ_BUILD" -j"$MAYHEM_JOBS" --target "${LIB_TARGETS[@]}"

# ---------------------------------------------------------------- 3a. normal-flags libraries (oracle)
cmake -S "$SRC" -B "$TEST_BUILD" "${CMAKE_COMMON[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -Wno-error" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -Wno-error" >/dev/null
cmake --build "$TEST_BUILD" -j"$MAYHEM_JOBS" --target "${LIB_TARGETS[@]}" ethash_utils

# Compile definitions/includes that beam's own targets use (see `ninja -t commands`).
DEFS=(-DBC_STATIC '-DBEAM_LIB_VERSION="7.5.0"' -DBOOST_ATOMIC_DYN_LINK -DBOOST_ATOMIC_NO_LIB
      -DBOOST_FILESYSTEM_DYN_LINK -DBOOST_FILESYSTEM_NO_LIB -DLOG_VERBOSE_ENABLED=0 -DENABLE_MINING
      "-DPROJECT_SOURCE_DIR=\"$SRC\"" -DUV_INTERNAL)
INCS=(-I"$SRC" -I"$SRC/bvm" -I"$SRC/3rdparty" -I"$SRC/3rdparty/re2" -I"$SRC/3rdparty/libuv/include"
      -I"$SRC/3rdparty/ethash/include" -I"$SRC/3rdparty/secp256k1/include")
CXXSTD=(-std=gnu++17 -pthread -march=core2 -Wno-nonnull)

libs_for() { # $1 = build dir
    local b="$1"
    echo "$b/bvm/libbvm.a" "$b/core/libcore.a" "$b/pow/libpow.a" "$b/core/libcore.a" "$b/p2p/libp2p.a"
    echo "$b/utility/libutility.a" "$b/3rdparty/libuv/libuv_a.a" "$b/3rdparty/libpbkdf.a"
    echo "$b/3rdparty/secp256k1/src/libsecp256k1.a" "$b/3rdparty/crypto/blake/libblake2b.a"
    echo "$b/3rdparty/ethash/lib/ethash/libethash.a" "$b/3rdparty/ethash/lib/keccak/libkeccak.a"
    echo "$b/3rdparty/re2/libre2.a"
}
SYSLIBS=(-lboost_filesystem -lboost_atomic -lssl -lcrypto -lpthread -lrt -ldl)
FUZZ_LIBS=($(libs_for "$FUZZ_BUILD"))
TEST_LIBS=($(libs_for "$TEST_BUILD"))

# ---------------------------------------------------------------- 2. harnesses
HFLAGS=($SANITIZER_FLAGS $UBSAN_RELAX $SANCOV $DEBUG_FLAGS -O1 "${CXXSTD[@]}" "${DEFS[@]}" "${INCS[@]}")

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$SRC/mayhem/build/lsan_off.o"
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$SRC/mayhem/build/standalone_main.o"

for h in fuzz_wasm_compile fuzz_tx_validate fuzz_evm fuzz_proto_msg; do
    $CXX "${HFLAGS[@]}" $LIB_FUZZING_ENGINE "$SRC/mayhem/$h.cc" "$SRC/mayhem/build/lsan_off.o" \
        "${FUZZ_LIBS[@]}" "${SYSLIBS[@]}" -o "$SRC/$h"
    $CXX "${HFLAGS[@]}" "$SRC/mayhem/$h.cc" "$SRC/mayhem/build/standalone_main.o" "$SRC/mayhem/build/lsan_off.o" \
        "${FUZZ_LIBS[@]}" "${SYSLIBS[@]}" -o "$SRC/$h-standalone"
done

# ---------------------------------------------------------------- 3b. oracle binaries (normal flags)
TFLAGS=(-O2 "$COVERAGE_FLAGS" "${CXXSTD[@]}" -DNDEBUG "${DEFS[@]}" "${INCS[@]}" -I"$SRC/bvm/ethash_service")
mkdir -p "$TEST_BUILD/bin"
for t in evm_test doc_test; do
    $CXX "${TFLAGS[@]}" "$SRC/bvm/unittest/$t.cpp" "${TEST_LIBS[@]}" "${SYSLIBS[@]}" -o "$TEST_BUILD/bin/$t"
done
$CXX "${TFLAGS[@]}" "$SRC/bvm/unittest/shaders_test.cpp" "$TEST_BUILD/bvm/ethash_service/libethash_utils.a" \
    "${TEST_LIBS[@]}" "${SYSLIBS[@]}" -o "$TEST_BUILD/bin/shaders_test"
$CXX "${TFLAGS[@]}" "$SRC/mayhem/kat.cc" "${TEST_LIBS[@]}" "${SYSLIBS[@]}" -o "$TEST_BUILD/bin/kat"

# shaders_test loads <shader>/<file>.wasm relative to its working directory.
rm -rf "$TEST_BUILD/data"
mkdir -p "$TEST_BUILD/data"
cp -r "$SRC"/bvm/Shaders/* "$TEST_BUILD/data/"

echo "build.sh: done"
