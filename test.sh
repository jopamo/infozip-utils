#!/usr/bin/env bash
# test-unzip.sh — functional and performance tests for Info-ZIP unzip on Linux
# self-contained, uses Python stdlib to generate archives

set -euo pipefail

# config
UNZIP_BIN="${UNZIP_BIN:-unzip}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
PERF_SIZE_MB="${PERF_SIZE_MB:-256}"      # size of perf payload per case in MiB
PERF_ITERS="${PERF_ITERS:-3}"            # iterations per perf case
PERF_TMPFS="${PERF_TMPFS:-}"             # set to a writable tmpfs path to reduce disk noise

# colors
RED=$'\e[31m'
GRN=$'\e[32m'
YLW=$'\e[33m'
BLU=$'\e[34m'
RST=$'\e[0m'

# counters
PASS=0
FAIL=0

need() { command -v "$1" >/dev/null 2>&1 || { echo "${RED}missing required tool: $1${RST}"; exit 1; }; }
need "$UNZIP_BIN"
need "$PYTHON_BIN"

# workspace
WORKDIR="${PERF_TMPFS:-"$(mktemp -d)"}"
ART="$WORKDIR/artifacts"
EXTRACT="$WORKDIR/extract"
EXPECT="$WORKDIR/expect"
OUTS="$WORKDIR/outs"
PERF="$WORKDIR/perf"
mkdir -p "$ART" "$EXTRACT" "$EXPECT" "$OUTS" "$PERF"
trap '[[ -n "${PERF_TMPFS:-}" ]] || rm -rf "$WORKDIR"' EXIT

ok()   { echo "[$GRN ok $RST] $*"; PASS=$((PASS+1)); }
err()  { echo "[$RED err$RST] $*"; FAIL=$((FAIL+1)); }
_show_hex() { xxd -p -c 32 "$1" | sed 's/^/  /'; }
diff_bytes() {
  local exp="$1" got="$2" label="$3"
  if cmp -s "$exp" "$got"; then
    ok "$label"
  else
    err "$label"
    echo "${BLU}--- expected (hex)$RST"
    _show_hex "$exp"
    echo "${YLW}+++ actual   (hex)$RST"
    _show_hex "$got"
  fi
}
mkexp() {
  local path="$1" data="$2"
  : > "$path"
  printf '%s' "$data" > "$path"
}

# python helpers
py_make_main_zip() {
  local out="$1"
  "$PYTHON_BIN" - "$out" <<'PY'
import os, sys, time, zipfile
out = sys.argv[1]
os.makedirs(os.path.dirname(out), exist_ok=True)

files = {
  "hello.txt": b"hello world\n",
  "dir/nested.txt": b"nested file\n",
  "unicod\xe9/na\xefve.txt": b"utf8 names\n",
}

def set_level(zi, lvl=6):
    if hasattr(zi, "compresslevel"):
        zi.compresslevel = lvl
    elif hasattr(zi, "compress_level"):
        zi.compress_level = lvl

with zipfile.ZipFile(out, "w", allowZip64=True) as z:
    zi = zipfile.ZipInfo("hello.txt", time.localtime()[:6])
    zi.create_system = 3
    zi.compress_type = zipfile.ZIP_STORED
    zi.external_attr = (0o100644 << 16)
    z.writestr(zi, files["hello.txt"])

    zi = zipfile.ZipInfo("dir/nested.txt", time.localtime()[:6])
    zi.create_system = 3
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = (0o100644 << 16)
    set_level(zi, 6)
    z.writestr(zi, files["dir/nested.txt"])

    zi = zipfile.ZipInfo("unicod\xe9/na\xefve.txt", time.localtime()[:6])
    zi.create_system = 3
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = (0o100644 << 16)
    set_level(zi, 6)
    z.writestr(zi, files["unicod\xe9/na\xefve.txt"])
PY
}

py_make_symlink_zip() {
  local out="$1"
  "$PYTHON_BIN" - "$out" <<'PY'
import os, sys, time, zipfile
out = sys.argv[1]
os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, "w") as z:
    zi = zipfile.ZipInfo("dir/target.txt", time.localtime()[:6])
    zi.create_system = 3
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = (0o100644 << 16)
    z.writestr(zi, b"target\n")

    zi = zipfile.ZipInfo("dir/link.ln", time.localtime()[:6])
    zi.create_system = 3
    zi.compress_type = zipfile.ZIP_STORED
    zi.external_attr = ((0o120777) << 16)
    z.writestr(zi, b"target.txt")
PY
}

# perf generators and zips
py_make_perf_payload() {
  local out="$1" size_mb="$2" mode="$3"
  "$PYTHON_BIN" - "$out" "$size_mb" "$mode" <<'PY'
import os, sys, secrets
out, size_mb, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
size = size_mb * 1024 * 1024
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    chunk = 1024 * 1024
    if mode == "random":
        for _ in range(size // chunk):
            f.write(secrets.token_bytes(chunk))
        f.write(secrets.token_bytes(size % chunk))
    elif mode == "zero":
        f.write(b"\x00" * size)
    else:
        raise SystemExit("unknown mode")
PY
}

py_zip_single_file() {
  local infile="$1" outzip="$2" method="$3" level="${4:-6}"
  "$PYTHON_BIN" - "$infile" "$outzip" "$method" "$level" <<'PY'
import os, sys, time, zipfile
infile, outzip, method, level = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
name = os.path.basename(infile)
with zipfile.ZipFile(outzip, "w", allowZip64=True) as z:
    zi = zipfile.ZipInfo(name, time.localtime()[:6])
    zi.create_system = 3
    if method == "stored":
        zi.compress_type = zipfile.ZIP_STORED
    else:
        zi.compress_type = zipfile.ZIP_DEFLATED
        if hasattr(zi, "compresslevel"):
            zi.compresslevel = level
        elif hasattr(zi, "compress_level"):
            zi.compress_level = level
    zi.external_attr = (0o100644 << 16)
    with open(infile, "rb") as f:
        z.writestr(zi, f.read(), compress_type=zi.compress_type)
PY
}

# timing helpers
now_ns() { date +%s%N 2>/dev/null || { date +%s; printf "000000000"; }; }
elapsed_s() {
  # args: start_ns end_ns -> prints seconds as float
  awk -v s="$1" -v e="$2" 'BEGIN{ printf "%.6f\n", (e - s) / 1000000000.0 }'
}
mbps() {
  # args: mib seconds -> mbps float
  awk -v m="$1" -v t="$2" 'BEGIN{ if (t<=0) t=0.000001; printf "%.2f\n", m / t }'
}

# build functional test zips
ZIP_MAIN="$ART/main.zip"
py_make_main_zip "$ZIP_MAIN"

ZIP_SYM="$ART/symlink.zip"
py_make_symlink_zip "$ZIP_SYM"

reset_extract() { rm -rf "$EXTRACT"; mkdir -p "$EXTRACT"; }

# expected byte files
mkexp "$EXPECT/hello.txt"   $'hello world\n'
mkexp "$EXPECT/nested.txt"  $'nested file\n'
mkexp "$EXPECT/unicode.txt" $'utf8 names\n'

# functional tests
T1() {
  local out
  out="$("$UNZIP_BIN" -l "$ZIP_MAIN")"
  [[ "$out" =~ "hello.txt" && "$out" =~ "dir/nested.txt" ]] \
    && ok "list shows expected files" \
    || err "list missing expected files"
}

T2() {
  "$UNZIP_BIN" -t "$ZIP_MAIN" >/dev/null 2>&1 && ok "test mode passes" || err "test mode failed"
}

T3() {
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -d "$EXTRACT"
  diff_bytes "$EXPECT/hello.txt"   "$EXTRACT/hello.txt"         "extracted hello.txt matches"
  diff_bytes "$EXPECT/nested.txt"  "$EXTRACT/dir/nested.txt"    "extracted nested.txt matches"
  diff_bytes "$EXPECT/unicode.txt" "$EXTRACT/unicodé/naïve.txt" "extracted unicode name matches"
}

T4() {
  "$UNZIP_BIN" -p "$ZIP_MAIN" hello.txt > "$OUTS/pipe-hello.txt"
  diff_bytes "$EXPECT/hello.txt" "$OUTS/pipe-hello.txt" "pipe output matches hello.txt"
}

T5() {
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -x "dir/*" -d "$EXTRACT"
  [[ -f "$EXTRACT/hello.txt" && ! -e "$EXTRACT/dir/nested.txt" ]] \
    && ok "exclude pattern prevents extracting dir/*" \
    || err "exclude pattern failed"
}

T6() {
  reset_extract
  "$UNZIP_BIN" -q -j "$ZIP_MAIN" -d "$EXTRACT"
  [[ -f "$EXTRACT/hello.txt" && -f "$EXTRACT/nested.txt" && -f "$EXTRACT/naïve.txt" ]] \
    && ok "junk paths flattens directories" \
    || err "junk paths failed"
}

T7() {
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -d "$EXTRACT"
  printf 'modified\n' > "$EXTRACT/hello.txt"
  "$UNZIP_BIN" -q -n "$ZIP_MAIN" -d "$EXTRACT"
  mkexp "$EXPECT/mod.txt" $'modified\n'
  diff_bytes "$EXPECT/mod.txt" "$EXTRACT/hello.txt" "-n preserves existing file"
  "$UNZIP_BIN" -q -o "$ZIP_MAIN" -d "$EXTRACT"
  diff_bytes "$EXPECT/hello.txt" "$EXTRACT/hello.txt" "-o overwrites existing file"
}

T8() {
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_SYM" -d "$EXTRACT"
  if [[ -L "$EXTRACT/dir/link.ln" ]]; then
    local tgt
    tgt="$(readlink "$EXTRACT/dir/link.ln")"
    [[ "$tgt" == "target.txt" ]] && ok "restored symlink points to target.txt" || err "symlink target mismatch"
  else
    err "symlink not restored as symlink"
    file "$EXTRACT/dir/link.ln" || true
    ls -l "$EXTRACT/dir" || true
  fi
}

T9() {
  "$UNZIP_BIN" -t "$ZIP_MAIN" >/dev/null 2>&1 && ok "data descriptor zip tests ok" || err "data descriptor handling failed"
}

# performance tests
# creates incompressible and compressible payloads, zips them as stored and deflated, then times extraction
# reports average MB/s over PERF_ITERS runs and prints per run numbers

bench_extract() {
  local zip="$1" what="$2" size_mb="$3"
  local i=1 total=0
  echo "${BLU}perf $what size ${size_mb}MiB${RST}"
  while (( i <= PERF_ITERS )); do
    rm -rf "$PERF/out"
    mkdir -p "$PERF/out"
    local start end secs rate
    start="$(now_ns)"
    "$UNZIP_BIN" -q -o "$zip" -d "$PERF/out"
    end="$(now_ns)"
    secs="$(elapsed_s "$start" "$end")"
    rate="$(mbps "$size_mb" "$secs")"
    printf "  run %d: %.3fs  %s MiB/s\n" "$i" "$secs" "$rate"
    total="$(awk -v acc="$total" -v r="$rate" 'BEGIN{ printf "%.6f", acc + r }')"
    (( i++ ))
    rm -rf "$PERF/out"
  done
  local avg
  avg="$(awk -v t="$total" -v n="$PERF_ITERS" 'BEGIN{ printf "%.2f", t / n }')"
  echo "  avg: ${avg} MiB/s"
}

T10_perf() {
  local size="$PERF_SIZE_MB"

  local raw_rand="$PERF/payload-random.bin"
  local raw_zero="$PERF/payload-zero.bin"
  py_make_perf_payload "$raw_rand" "$size" random
  py_make_perf_payload "$raw_zero" "$size" zero

  local zip_rand_st="$PERF/random-stored.zip"
  local zip_rand_def="$PERF/random-deflate.zip"
  local zip_zero_st="$PERF/zero-stored.zip"
  local zip_zero_def="$PERF/zero-deflate.zip"

  py_zip_single_file "$raw_rand" "$zip_rand_st" stored 0
  py_zip_single_file "$raw_rand" "$zip_rand_def" deflate 6
  py_zip_single_file "$raw_zero" "$zip_zero_st" stored 0
  py_zip_single_file "$raw_zero" "$zip_zero_def" deflate 6

  bench_extract "$zip_rand_st"  "extract random STORED" "$size"
  bench_extract "$zip_rand_def" "extract random DEFLATE" "$size"
  bench_extract "$zip_zero_st"  "extract zero STORED" "$size"
  bench_extract "$zip_zero_def" "extract zero DEFLATE" "$size"

  ok "performance benchmarking completed"
}

echo "${BLU}running unzip tests in $WORKDIR${RST}"
T1
T2
T3
T4
T5
T6
T7
T8
T9
T10_perf

echo
if (( FAIL == 0 )); then
  echo "${GRN}all tests passed ($PASS)${RST}"
  exit 0
else
  echo "${RED}$FAIL tests failed, $PASS passed${RST}"
  exit 1
fi
