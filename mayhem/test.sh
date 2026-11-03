#!/usr/bin/env bash
#
# mayhem/test.sh — RUN beam's own BVM/EVM unit tests plus known-answer probes (all built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
: "${SRC:=/mayhem}"
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN="$SRC/mayhem/build/test/bin"
DATA="$SRC/mayhem/build/test/data"
export UBSAN_OPTIONS="" ASAN_OPTIONS=""

passed=0
failed=0
ok()   { passed=$((passed + 1)); echo "PASS  $1"; }
fail() { failed=$((failed + 1)); echo "FAIL  $1" >&2; }

# beam's own unit tests: they assert internally and exit non-zero on failure. Run from the shader data dir.
for t in evm_test doc_test shaders_test; do
  if [ ! -x "$BIN/$t" ]; then fail "$t (binary missing)"; continue; fi
  if (cd "$DATA" && timeout 900 "$BIN/$t" > "/tmp/$t.out" 2>&1); then ok "$t"; else fail "$t"; tail -20 "/tmp/$t.out" >&2; fi
done

# Known-answer probes: values below are asserted here, in bash, from what the probe prints.
if [ ! -x "$BIN/kat" ]; then
  fail "kat (binary missing)"
else
  kat_out="$("$BIN/kat" "$SRC/bvm/Shaders/vault/contract.wasm" "$SRC/mayhem/fuzz_tx_validate/testsuite/tx_2out.bin" 2>&1)"
  echo "$kat_out"
  expect() { # <name> <exact line>
    if printf '%s\n' "$kat_out" | grep -Fxq -- "$2"; then ok "$1"; else fail "$1 (expected: $2)"; fi
  }
  expect keccak256_abc   "keccak256_abc=4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"
  expect evm_address     "evm_address=b90d65a624909bc36eee6bffdecf3c5acd7774c0"
  expect wasm_compile    "wasm_in=764 wasm_out=363 wasm_out_keccak=4d00d2618b3c4abc74ec84918fff4523afeb1d6dec64079b455ad98ee8155620"
  expect tx_parse        "tx_parsed=1 inputs=1 outputs=2 kernels=1"
  expect tx_fee          "tx_fee=250"
  expect tx_valid        "tx_valid=1"
  expect tx_tamper_reject "tx_tampered_valid=0"
fi

emit_ctrf "beam-bvm" "$passed" "$failed" 0
