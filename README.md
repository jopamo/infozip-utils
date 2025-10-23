<p align="center">
  <img src="zip-utils.svg" alt="InfoZIP Utils logo" width="220">
</p>

# ZIP Utils

Modernized, Linux optimized builds of Info-ZIP’s classic `zip` and `unzip` utilities. This fork keeps the battle-tested core, removes decades of legacy platform baggage, adds a shared `common/` layer, and ships reproducible Meson builds for contemporary toolchains.

---

## Highlights

* Builds both `zip` and `unzip` with a unified Meson + Ninja workflow
* Linux/Unix-only code paths; legacy VMS/DOS/OS2/Windows baggage removed
* Always-on large-file (Zip64) and Unicode filename support
* Integrated libbz2 compression/expansion logic for both tools
* Optional components (ZipInfo, ZipGrep, GUI stubs, SFX) stripped out
* Maintains Info-ZIP’s traditional CLI flags and behavior

---

## Build & Install

### Requirements

* Meson ≥ 1.2
* Ninja
* pkg-config
* libbz2 development headers

### Steps

```bash
meson setup build
ninja -C build
sudo ninja -C build install
```

The install target places both binaries and man pages under the standard prefixes (e.g. `/usr/bin/{zip,unzip}` and `share/man/man1/{zip,unzip}.1`).

---

## Testing

Run the functional and performance checks via:

```bash
./test.sh
```

The harness builds sample archives with `zip`, verifies them with `unzip` and Python’s `zipfile` module, validates Unicode/symlink handling, and optionally runs throughput benchmarks (`PERF_SIZE_MB`, `PERF_ITERS`, `PERF_TMPFS` environment knobs).

Export `PERF_REPORT=perf/report.tsv` (or any path) to capture the average MiB/s numbers from each performance case in a tab-separated file for later comparisons.

The script auto-detects locally built binaries in `build/` and falls back to the system `zip`/`unzip` if they are not present; override via `ZIP_BIN=` or `UNZIP_BIN=` when needed.

---

## Differences from Info-ZIP 6.0

| Aspect                  | InfoZIP Utils (this fork)        | Original Info-ZIP 6.0        |
| ----------------------- | -------------------------------- | ---------------------------- |
| Supported OS            | Linux / Unix only                | ~25 platforms                |
| Build system            | Meson + Ninja                    | Hand-written Makefiles       |
| Components              | `zip`, `unzip`                   | `zip`, `unzip`, `zipinfo`,…  |
| Compression methods     | Deflate, Store, BZip2            | Same (plus legacy options)   |
| Large file + Zip64      | Always enabled                   | Optional                     |
| Unicode filenames       | Enabled by default               | Optional                     |
| Encryption support      | Retained (`-DCRYPT`)             | Retained                     |
| GUI / DLL / SFX targets | Not built                        | Provided for legacy systems  |
| Platform glue           | `unix.c` only                    | Per-OS directories           |

---

## Repository Layout

```
.
├── common/        # shared CRC32/TTY helpers and platform glue
├── man/           # updated zip(1) + unzip(1) man pages
├── unzip/         # modernized unzip sources
├── zip/           # modernized zip sources
├── meson.build    # top-level Meson build description
└── test.sh        # functional + perf smoke test harness
```

Legacy reference material from the original project (e.g. `History.600`, `zip.txt`, `unzip.txt`) is preserved for context.

---

## License

InfoZIP Utils continues to use the permissive Info-ZIP license. See [`LICENSE`](LICENSE) for the current text and `COPYING.OLD` for the historical notice.

---

## Acknowledgments

Thanks to the original Info-ZIP contributors and the broader community keeping classic ZIP tooling alive on modern systems.
