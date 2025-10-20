#!/usr/bin/env bash
# test-zip+unzip.sh — functional + perf tests for Info-ZIP zip/unzip on Linux
# builds archives with zip; verifies with unzip AND Python's zipfile
# now also benchmarks zip (compression) throughput

set -euo pipefail

# config
ZIP_BIN="${ZIP_BIN:-zip}"
UNZIP_BIN="${UNZIP_BIN:-unzip}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
PERF_SIZE_MB="${PERF_SIZE_MB:-256}"      # size per perf case in MiB
PERF_ITERS="${PERF_ITERS:-3}"            # iterations per perf case
PERF_TMPFS="${PERF_TMPFS:-}"             # set to tmpfs to reduce disk noise

# colors
RED=$'\e[31m'; GRN=$'\e[32m'; YLW=$'\e[33m'; BLU=$'\e[34m'; RST=$'\e[0m'

PASS=0; FAIL=0
need(){ command -v "$1" >/dev/null 2>&1 || { echo "${RED}missing required tool: $1${RST}"; exit 1; }; }
need "$ZIP_BIN"; need "$UNZIP_BIN"; need "$PYTHON_BIN"

WORKDIR="${PERF_TMPFS:-"$(mktemp -d)"}"
ART="$WORKDIR/artifacts"; SRC="$WORKDIR/src"; EXTRACT="$WORKDIR/extract"
EXPECT="$WORKDIR/expect"; OUTS="$WORKDIR/outs"; PERF="$WORKDIR/perf"
mkdir -p "$ART" "$SRC" "$EXTRACT" "$EXPECT" "$OUTS" "$PERF"
trap '[[ -n "${PERF_TMPFS:-}" ]] || rm -rf "$WORKDIR"' EXIT

ok(){ echo "[$GRN ok $RST] $*"; PASS=$((PASS+1)); }
err(){ echo "[$RED err$RST] $*"; FAIL=$((FAIL+1)); }
_show_hex(){ xxd -p -c 32 "$1" | sed 's/^/  /'; }
diff_bytes(){
  local exp="$1" got="$2" label="$3"
  if cmp -s "$exp" "$got"; then ok "$label"
  else err "$label"; echo "${BLU}--- expected (hex)$RST"; _show_hex "$exp"
       echo "${YLW}+++ actual   (hex)$RST"; _show_hex "$got"; fi
}
mkexp(){ : > "$1"; printf '%s' "$2" > "$1"; }
reset_extract(){ rm -rf "$EXTRACT"; mkdir -p "$EXTRACT"; }

# -------- Python helpers (independent verification + perf payload gen) -----
py_verify_main_zip(){ # args: zip_path
  "$PYTHON_BIN" - "$1" <<'PY'
import sys, zipfile, binascii
zpath = sys.argv[1]
expected = {
  "hello.txt": b"hello world\n",
  "dir/nested.txt": b"nested file\n",
  "unicod\xe9/na\xefve.txt": b"utf8 names\n",
}
with zipfile.ZipFile(zpath, "r") as z:
    names = set(z.namelist())
    need  = set(expected.keys())
    missing = need - names
    extra   = names - need
    if missing:
        raise SystemExit(f"missing entries in {zpath}: {sorted(missing)}")
    for name, want in expected.items():
        with z.open(name, "r") as f:
            got = f.read()
        if got != want:
            raise SystemExit(f"bytes mismatch for {name} in {zpath}")
        zi = z.getinfo(name)
        calc = binascii.crc32(want) & 0xffffffff
        if zi.CRC != calc:
            raise SystemExit(f"CRC mismatch for {name}: header={zi.CRC:x} calc={calc:x}")
print("py-verify main.zip OK")
PY
}

py_verify_symlink_zip(){ # args: zip_path
  "$PYTHON_BIN" - "$1" <<'PY'
import sys, zipfile
zpath = sys.argv[1]
with zipfile.ZipFile(zpath, "r") as z:
    zi_link = z.getinfo("dir/link.ln")
    z.getinfo("dir/target.txt")
    if zi_link.create_system != 3:
        raise SystemExit(f"link.ln create_system={zi_link.create_system}, not Unix")
    mode = (zi_link.external_attr >> 16) & 0xFFFF
    if (mode & 0xF000) != 0xA000:  # 0o120000 symlink
        raise SystemExit(f"link.ln not marked as symlink: mode={oct(mode)}")
    if z.read("dir/link.ln") != b"target.txt":
        raise SystemExit("link.ln payload unexpected")
print("py-verify symlink.zip OK")
PY
}

py_make_perf_payload(){
  local out="$1" size_mb="$2" mode="$3"
  "$PYTHON_BIN" - "$out" "$size_mb" "$mode" <<'PY'
import os, sys, secrets
out, size_mb, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
size = size_mb * 1024 * 1024
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    chunk = 1024 * 1024
    if mode == "random":
        for _ in range(size // chunk): f.write(secrets.token_bytes(chunk))
        f.write(secrets.token_bytes(size % chunk))
    elif mode == "zero":
        f.write(b"\x00"*size)
    else:
        raise SystemExit("unknown mode")
PY
}

# -------- expected bytes for shell-level checks ----------
mkexp "$EXPECT/hello.txt"   $'hello world\n'
mkexp "$EXPECT/nested.txt"  $'nested file\n'
mkexp "$EXPECT/unicode.txt" $'utf8 names\n'

# -------- build test archives using zip ----------
build_main_zip(){
  rm -rf "$SRC/main"; mkdir -p "$SRC/main/dir" "$SRC/main/unicodé"
  printf 'hello world\n' > "$SRC/main/hello.txt"
  printf 'nested file\n' > "$SRC/main/dir/nested.txt"
  printf 'utf8 names\n'  > "$SRC/main/unicodé/naïve.txt"
  local z="$ART/main.zip"; rm -f "$z"
  (cd "$SRC/main" && "$ZIP_BIN" -X -0 -q "$z" "hello.txt")
  (cd "$SRC/main" && "$ZIP_BIN" -X -q "$z" "dir/nested.txt" "unicodé/naïve.txt")
}

build_symlink_zip(){
  rm -rf "$SRC/symlink"; mkdir -p "$SRC/symlink/dir"
  printf 'target\n' > "$SRC/symlink/dir/target.txt"
  (cd "$SRC/symlink/dir" && ln -sf "target.txt" "link.ln")
  (cd "$SRC/symlink" && "$ZIP_BIN" -X -q -r -y "$ART/symlink.zip" "dir")
}

# -------- timing helpers ----------
now_ns(){ date +%s%N 2>/dev/null || { date +%s; printf "000000000"; }; }
elapsed_s(){ awk -v s="$1" -v e="$2" 'BEGIN{ printf "%.6f\n", (e - s) / 1000000000.0 }'; }
mbps(){ awk -v m="$1" -v t="$2" 'BEGIN{ if (t<=0) t=0.000001; printf "%.2f\n", m / t }'; }

# -------- build archives --------
build_main_zip
build_symlink_zip
ZIP_MAIN="$ART/main.zip"
ZIP_SYM="$ART/symlink.zip"

# -------- functional tests (zip/unzip + python verification) --------
echo "${BLU}running zip+unzip tests in $WORKDIR${RST}"

py_verify_main_zip    "$ZIP_MAIN" && ok "python verified main.zip"
py_verify_symlink_zip "$ZIP_SYM"  && ok "python verified symlink.zip"

T1(){ local out; out="$("$UNZIP_BIN" -l "$ZIP_MAIN")"
      [[ "$out" =~ "hello.txt" && "$out" =~ "dir/nested.txt" ]] && ok "list shows expected files" || err "list missing expected files"; }
T2(){ "$UNZIP_BIN" -t "$ZIP_MAIN" >/dev/null 2>&1 && ok "unzip -t passes" || err "unzip -t failed"; }
T3(){
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -d "$EXTRACT"
  diff_bytes "$EXPECT/hello.txt"   "$EXTRACT/hello.txt"         "extracted hello.txt matches"
  diff_bytes "$EXPECT/nested.txt"  "$EXTRACT/dir/nested.txt"    "extracted nested.txt matches"
  diff_bytes "$EXPECT/unicode.txt" "$EXTRACT/unicodé/naïve.txt" "extracted unicode name matches"
}
T4(){ "$UNZIP_BIN" -p "$ZIP_MAIN" hello.txt > "$OUTS/pipe-hello.txt"
      diff_bytes "$EXPECT/hello.txt" "$OUTS/pipe-hello.txt" "pipe output matches hello.txt"; }
T5(){
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -x "dir/*" -d "$EXTRACT"
  [[ -f "$EXTRACT/hello.txt" && ! -e "$EXTRACT/dir/nested.txt" ]] && ok "exclude pattern works" || err "exclude pattern failed"
}
T6(){
  reset_extract
  "$UNZIP_BIN" -q -j "$ZIP_MAIN" -d "$EXTRACT"
  [[ -f "$EXTRACT/hello.txt" && -f "$EXTRACT/nested.txt" && -f "$EXTRACT/naïve.txt" ]] && ok "junk paths flatten" || err "junk paths failed"
}
T7(){
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_MAIN" -d "$EXTRACT"
  printf 'modified\n' > "$EXTRACT/hello.txt"
  "$UNZIP_BIN" -q -n "$ZIP_MAIN" -d "$EXTRACT"
  mkexp "$EXPECT/mod.txt" $'modified\n'
  diff_bytes "$EXPECT/mod.txt" "$EXTRACT/hello.txt" "-n preserves existing file"
  "$UNZIP_BIN" -q -o "$ZIP_MAIN" -d "$EXTRACT"
  diff_bytes "$EXPECT/hello.txt" "$EXTRACT/hello.txt" "-o overwrites existing file"
}
T8(){
  reset_extract
  "$UNZIP_BIN" -q "$ZIP_SYM" -d "$EXTRACT"
  if [[ -L "$EXTRACT/dir/link.ln" ]]; then
    local tgt; tgt="$(readlink "$EXTRACT/dir/link.ln")"
    [[ "$tgt" == "target.txt" ]] && ok "restored symlink points to target.txt" || err "symlink target mismatch"
  else
    err "symlink not restored as symlink"; ls -l "$EXTRACT/dir" || true
  fi
}
T9(){ "$UNZIP_BIN" -t "$ZIP_MAIN" >/dev/null 2>&1 && ok "data descriptor zip tests ok" || err "data descriptor handling failed"; }

T1; T2; T3; T4; T5; T6; T7; T8; T9

# -------- performance: zip (compression) AND unzip (extraction) -------------
bench_zip(){ # args: payload_zip_dir payload_file out_zip opts label size_mb
  local dir="$1" in="$2" out="$3" opts="$4" label="$5" size_mb="$6"
  local i=1 total=0
  echo "${BLU}perf zip $label size ${size_mb}MiB${RST}"
  while (( i <= PERF_ITERS )); do
    rm -f "$out"
    local start end secs rate
    start="$(now_ns)"
    ( cd "$dir" && "$ZIP_BIN" -X -q $opts "$out" "$(basename "$in")" )
    end="$(now_ns)"
    secs="$(elapsed_s "$start" "$end")"; rate="$(mbps "$size_mb" "$secs")"
    printf "  run %d: %.3fs  %s MiB/s\n" "$i" "$secs" "$rate"
    total="$(awk -v acc="$total" -v r="$rate" 'BEGIN{ printf "%.6f", acc + r }')"
    (( i++ ))
  done
  local avg; avg="$(awk -v t="$total" -v n="$PERF_ITERS" 'BEGIN{ printf "%.2f", t / n }')"
  echo "  avg: ${avg} MiB/s"
  # sanity: test the resulting zip quickly
  "$UNZIP_BIN" -t "$out" >/dev/null 2>&1 || err "zip integrity failed for $label"
}

bench_unzip(){ # args: zip label size_mb
  local zip="$1" label="$2" size_mb="$3"
  local i=1 total=0
  echo "${BLU}perf unzip $label size ${size_mb}MiB${RST}"
  while (( i <= PERF_ITERS )); do
    rm -rf "$PERF/out"; mkdir -p "$PERF/out"
    local start end secs rate
    start="$(now_ns)"; "$UNZIP_BIN" -q -o "$zip" -d "$PERF/out"; end="$(now_ns)"
    secs="$(elapsed_s "$start" "$end")"; rate="$(mbps "$size_mb" "$secs")"
    printf "  run %d: %.3fs  %s MiB/s\n" "$i" "$secs" "$rate"
    total="$(awk -v acc="$total" -v r="$rate" 'BEGIN{ printf "%.6f", acc + r }')"
    (( i++ ))
  done
  local avg; avg="$(awk -v t="$total" -v n="$PERF_ITERS" 'BEGIN{ printf "%.2f", t / n }')"
  echo "  avg: ${avg} MiB/s"
}

T10_perf(){
  local size="$PERF_SIZE_MB"
  local raw_rand="$PERF/payload-random.bin" raw_zero="$PERF/payload-zero.bin"
  py_make_perf_payload "$raw_rand" "$size" random
  py_make_perf_payload "$raw_zero" "$size" zero

  # ZIP benchmarks (create)
  local z_rand_st="$PERF/random-stored.zip" z_rand_def="$PERF/random-deflate.zip"
  local z_zero_st="$PERF/zero-stored.zip"   z_zero_def="$PERF/zero-deflate.zip"

  # zip throughput
  bench_zip "$PERF" "$raw_rand" "$z_rand_st"  "-0"           "random STORED (zip)"  "$size"
  bench_zip "$PERF" "$raw_rand" "$z_rand_def" ""             "random DEFLATE (zip)" "$size"
  bench_zip "$PERF" "$raw_zero" "$z_zero_st"  "-0"           "zero STORED (zip)"    "$size"
  bench_zip "$PERF" "$raw_zero" "$z_zero_def" ""             "zero DEFLATE (zip)"   "$size"

  # UNZIP benchmarks (extract)
  bench_unzip "$z_rand_st"  "random STORED (unzip)"  "$size"
  bench_unzip "$z_rand_def" "random DEFLATE (unzip)" "$size"
  bench_unzip "$z_zero_st"  "zero STORED (unzip)"    "$size"
  bench_unzip "$z_zero_def" "zero DEFLATE (unzip)"   "$size"

  ok "performance benchmarking completed"
}
T10_perf

echo
if (( FAIL == 0 )); then
  echo "${GRN}all tests passed ($PASS)${RST}"
  exit 0
else
  echo "${RED}$FAIL tests failed, $PASS passed${RST}"
  exit 1
fi
