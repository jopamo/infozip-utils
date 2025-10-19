# UnZip

This is a fork of **Info-ZIP’s UnZip 6.0**, focused exclusively on modern **Unix and Linux** systems.
It removes legacy platform code (VMS, DOS, OS/2, Windows, etc.), adds clean **Meson build support**, and integrates **bzip2** decompression by default. It only builds/installs the unzip executable.

---

## Overview

UnZip is a command-line utility for extracting files from archives in the standard `.zip` format.
This fork preserves compatibility with traditional UnZip behavior while refactoring the codebase for current Linux toolchains and glibc/musl environments.

### Key Features

* POSIX-compliant, Linux-only build
* Simplified sources (`src/`) — no platform-specific clutter
* Uses modern headers (`_XOPEN_SOURCE=700`, `_DEFAULT_SOURCE`)
* Bzip2 support (`-DUSE_BZIP2`, links against `libbz2`)
* Clean and reproducible builds with Meson/Ninja
* Installs a minimal man page (`unzip.1`)

---

## Building

### Requirements

* **Meson ≥ 1.2**
* **Ninja**
* **pkg-config**
* **libbz2** development headers

### Steps

```bash
meson setup build
ninja -C build
sudo ninja -C build install
```

This will build and install `/usr/bin/unzip` and `man1/unzip.1`.

---

## Differences from Info-ZIP 6.0

| Aspect              | This Fork             | Original Info-ZIP           |
| ------------------- | --------------------- | --------------------------- |
| Supported OS        | Linux / Unix only     | ~25 platforms               |
| Build system        | Meson + Ninja         | hand-written Makefiles      |
| Compression methods | Deflate, Store, BZip2 | same (plus optional legacy) |
| Large file support  | Always enabled        | optional                    |
| Zip64 support       | retained              | retained                    |
| GUI, DLLs, SFX      | removed               | included for legacy OSes    |
| ZipInfo / ZipGrep   | disabled              | included                    |

---

## Installation Layout

```
/usr/bin/unzip
/usr/share/man/man1/unzip.1
```

---

## License

This project retains the **Info-ZIP license**, a permissive BSD-style license.
See [`LICENSE`](LICENSE) for details.

---

## Acknowledgments

Based on the original **Info-ZIP UnZip 6.0** (April 2009), to all original contributors.

---

## Repository Structure

```
.
├── meson.build
├── man/
│   └── unzip.1
└── src/
    ├── *.c / *.h — core extraction logic
    └── unix.c — POSIX integration layer
