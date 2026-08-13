# Intel&reg; QuickAssist Technology (QAT) QATzip Library

QATzip is a user-space library built on top of the Intel&reg; QuickAssist Technology (QAT) user-space library. It provides extended compression and decompression capabilities by offloading these operations to Intel&reg; QAT Accelerators. QATzip generates data in the standard gzip\* format (RFC1952) with extended headers, in [lz4\* blocks](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md) and the [lz4\* frame format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md), or in the standard zstd\* frame format. The resulting data can be decompressed using any compliant gzip\*, lz4\*, or zstd\* implementation. QATzip is optimized to fully leverage the performance benefits of Intel&reg; QuickAssist Technology.

## Table of Contents

- [Supported Formats](#supported-formats)
- [Features](#features)
- [QATzip Compression Level Mapping](#qatzip-compression-level-mapping)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Install Instructions](#install-instructions)
  - [Install with the in-tree QAT package](#install-with-the-in-tree-qat-package)
  - [Install with the out-of-tree QAT package](#install-with-the-out-of-tree-qat-package)
  - [Install from Docker Image](#install-from-docker-image)
  - [Configuration](#configuration)
  - [Enable qzstd](#enable-qzstd)
- [Test QATzip](#test-qatzip)
- [Performance Test With QATzip](#performance-test-with-qatzip)
- [QATzip API Manual](#qatzip-api-manual)
- [Limitations](#limitations)
- [Licensing](#licensing)
- [Legal](#legal)

## Supported Formats

| Data Format           | Algorithm | QAT Device                    | Description                                                            |
|-----------------------|-----------|-------------------------------|------------------------------------------------------------------------|
| `QZ_DEFLATE_4B`       | deflate\* | QAT 1.x (Gen2/Gen3) and later | Raw DEFLATE\* with a 4-byte length header.                             |
| `QZ_DEFLATE_GZIP`     | deflate\* | QAT 1.x (Gen2/Gen3) and later | DEFLATE\* wrapped by a gzip\* header and footer.                       |
| `QZ_DEFLATE_GZIP_EXT` | deflate\* | QAT 1.x (Gen2/Gen3) and later | DEFLATE\* wrapped by a QAT gzip\* extension header and footer.         |
| `QZ_DEFLATE_RAW`      | deflate\* | QAT 1.x (Gen2/Gen3) and later | Raw DEFLATE\*. Compression only; decompression falls back to software. |
| `QZ_LZ4`              | lz4\*     | QAT 2.x (Gen4) and later      | LZ4\* data wrapped in an lz4\* frame.                                  |
| `QZ_LZ4s`             | lz4s\*    | QAT 2.x (Gen4) and later      | LZ4s\* blocks.                                                         |
| `QZ_LZ4_BLOCK`        | lz4\*     | QAT 5.x (Gen6) only           | Raw LZ4\* blocks, with no frame header or footer.                      |
| `QZ_ZSTD`             | zstd\*    | QAT 5.x (Gen6) only           | ZSTD\* data wrapped in a zstd\* frame.                                 |

For the raw block layout used by `QZ_LZ4_BLOCK`, see the [lz4\* block format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md).

## Features

### Frame Formats

Where a specification defines a standard frame, QATzip emits it exactly, so the compressed stream can be decoded by any compliant gzip\*, lz4\*, or zstd\* implementation. The frame layout produced for each entry in [Supported Formats](#supported-formats) is shown below.

- **Gzip\*** (`QZ_DEFLATE_GZIP`) — a 10-byte header and an 8-byte footer, per RFC1952:

  ```text
  | ID1(0x1F) (1B) | ID2(0x8B) (1B) | Compression Method (8 = DEFLATE*) (1B) | Flags (1B) | Modification Time (4B) | Extra Flags (1B) | OS (1B) | Deflate Block | CRC32 (4B) | ISIZE (4B) |
  ```

- **Gzip\* extended** (`QZ_DEFLATE_GZIP_EXT`) — the standard gzip\* header followed by a 14-byte Intel&reg; extra field recording the chunk and block sizes:

  ```text
  |  XLEN (2B) | SI1('Q') (1B) | SI2('Z') (1B) | Length of subheader (2B) | Intel(R) defined field 'Chunksize' (4B) | Intel(R) defined field 'Blocksize' (4B) |
  ```

- **4-byte header** (`QZ_DEFLATE_4B`) — a deflate block prefixed by its compressed length:

  ```text
  | Intel(R) defined Header (4B) | deflate block |
  ```

- **Raw deflate\*** (`QZ_DEFLATE_RAW`) — a bare deflate block with no header or footer. Compression is offloaded; decompression falls back to software.

- **LZ4\* frame** (`QZ_LZ4`) — a standard lz4\* frame. QAT 2.x (Gen4) and later:

  ```text
  | MagicNb (4B) | FLG (1B) | BD (1B) | CS (8B) | HC (1B) | lz4 Block | EndMark (4B) |
  ```

- **LZ4s\*** (`QZ_LZ4s`) — lz4s\* sequences, which require a user-supplied post-processing callback to produce the final output. QAT 2.x (Gen4) and later.

- **LZ4\* block** (`QZ_LZ4_BLOCK`) — raw lz4\* blocks with no frame header or footer. QAT 5.x (Gen6) only.

- **ZSTD\*** (`QZ_ZSTD`) — a standard zstd\* frame, generated and consumed directly by QAT hardware with no post-processing. QAT 5.x (Gen6) only.

### Programming Interfaces

- **Streaming** (`qzCompressStream()` / `qzDecompressStream()`) — compresses or decompresses data submitted piecemeal, improving ratio and throughput over one-shot calls. Deflate\* only; LZ4\*, LZ4s\*, and ZSTD\* have no streaming path.
- **Asynchronous** (`qzCompress2()` / `qzDecompress2()`) — callback-driven offload for lower latency and higher throughput, most useful with few instances or payloads below 64 KB. Works with every algorithm the synchronous API supports, but is hardware-only: async requests have no software fallback. Passing a NULL callback runs the request synchronously. Not exposed by `qzip`.
- **Dictionary (de)compression** (`qzCompressWithDictionary()` / `qzDecompressWithDictionary()`) — primes a session with a dictionary via `QzDictionaryParams_T`, optionally returning its CRC64. Works with `DEFLATE_RAW`, `DEFLATE_4B`, `DEFLATE_ZLIB`, `ZSTD`, and `LZ4s`, but not `LZ4_FH` or `LZ4_BLOCK`. QAT 5.x (Gen6) only.

### Runtime and Resource Management

- **Zero-copy buffers** — `qzMalloc()` and `qzFree()` allocate pinned, contiguous memory for DMA, backed by huge pages and kernel memory with fallback to huge pages under kernel memory contention.
- **Instance sharing** — multiple threads in a process transparently over-subscribe fewer hardware instances, and device sharing across processes is configurable.
- **Software failover** — optional fallback to software, keeping compression and decompression available when hardware resources are insufficient.
- **Latency Sensitive Mode** — offloads part of the workload to the CPU to raise throughput and cut latency under high stress.
  > Note: Enabling this mode in low-stress scenarios or for workloads below 8 KB may reduce throughput.
- **Adaptive polling** — reduces CPU usage under stress.

### Command-Line Utilities

- `qzip` — compresses and decompresses files and directories, including 7z output.
- `qzstd` — sample application built on the lz4s + post-processing pipeline that produces and consumes the ZSTD\* format. See [Enable qzstd](#enable-qzstd).

## QATzip Compression Level Mapping

The following table shows how standard software `zlib*` compression levels map to `QATzip` levels for QAT.

| Software `zlib*` Levels | QAT 1.x Equivalent | QAT 2.x & later Equivalent |
|-------------------------|--------------------|----------------------------|
| 1 - 4                   | QATzip Level 1     | QATzip Level 1             |
| 5                       | QATzip Level 5     | QATzip Level 1             |
| 6 - 8                   | QATzip Level 5     | QATzip Level 6             |
| 9                       | QATzip Level 9     | QATzip Level 9             |
| 10 - 12                 | Unsupported        | QATzip Level 9             |

Refer to [QAT Compression levels](https://intel.github.io/quickassist/PG/services_compression_api.html#compression-levels), which summarizes how a QATzip level translates to hardware-accelerated levels for each QAT generation.

## Hardware Requirements

This QATzip library supports compression and decompression offload on platforms with the following QAT
acceleration devices:

- [Intel&reg; QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon.html) — QAT Gen4 (`4xxx`, `401xx`, `402xx`) and QAT Gen5 (`420xx`)
- Intel&reg; QuickAssist 6xxx Series — QAT Gen6
- [Intel&reg; QuickAssist Adapter 8970](https://www.intel.com/content/www/us/en/products/sku/125200/intel-quickassist-adapter-8970/downloads.html) — QAT 1.x (Gen3)
- [Intel&reg; QuickAssist Adapter 8960](https://www.intel.com/content/www/us/en/products/sku/125199/intel-quickassist-adapter-8960/downloads.html) — QAT 1.x (Gen3)
- [Intel&reg; QuickAssist Adapter 8950](https://www.intel.com/content/www/us/en/products/sku/80371/intel-communications-chipset-8950/specifications.html) — QAT 1.x (Gen2)
- [Intel&reg; Atom&trade; Processor C3000](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-atom-processor-c3000-family-technical-overview.html) — QAT 1.x (Gen3)

## Software Requirements

- Intel&reg; QuickAssist Technology Driver for Linux\* HW v2.x or v1.x (out-of-tree), latest from [Intel&reg; QuickAssist Technology](https://developer.intel.com/quickassist)
- Intel&reg; QATlib for Linux (in-tree, for QAT Gen4 and later) - [v25.08](https://github.com/intel/qatlib/releases) or later
- Zlib: v1.2.7 or later
- LZ4: v1.8.3 or later
- Zstandard (zstd): v1.5.7 or later

Distributions such as Fedora 34+, RHEL 8.4+ & 9.0+, CentOS 9 Stream, SUSE SLES15 SP3+, Ubuntu 24.04+, and Debian 13+ ship the `qatzip` package in their repositories. That package is built against the QAT_HW `qatlib` in-tree driver, which supports `4xxx`, `401xx`, `402xx`, `420xx`, and `6xxx` devices.

## Install Instructions

### Install with the in-tree QAT package

See the [QATlib installation guide](https://intel.github.io/quickassist/qatlib/index.html) for detailed instructions.

**From RPM** (Fedora 34+, RHEL 8.4+, CentOS 9+, Ubuntu 24.04+, Debian 13+):

```bash
# RHEL-based
sudo dnf install -y qatzip qatzip-devel

# Debian-based
sudo apt -y install qatzip libqatzip3 libqatzip-dev
```

**From Source Code**:

```bash
cd QATzip/
export QZ_ROOT=$(pwd)
./autogen.sh
./configure
make clean && make && sudo make install
```

### Install with the out-of-tree QAT package

Refer to the [QAT Installation Guide](https://intel.github.io/quickassist/GSG/2.X/installation.html) for detailed setup instructions.

> **Note**: For non-root users, see the [non-root user guide](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user). When SVM is disabled, QAT hardware requires DMA-accessible memory. Use QAT USDM component to allocate and free DMA-able memory (see the [USDM settings guide](https://intel.github.io/quickassist/PG/infrastructure_memory_management.html#huge-pages)).

1. **Install dependencies**:

   ```bash
   # RHEL-based
   sudo dnf install -y autoconf autoconf-archive automake libtool zlib-devel lz4-devel numactl-devel libzstd-devel

   # Debian-based
   sudo apt -y install autoconf autoconf-archive automake libtool zlib1g-dev liblz4-dev libnuma-dev libzstd-dev
   ```

2. **Configure**:

   ```bash
   cd QATzip/
   export QZ_ROOT=$(pwd)
   export ICP_ROOT=/QAT/PACKAGE/PATH
   ./autogen.sh
   ./configure  # Run ./configure -h for options
   ```

3. **Build and install**:

   ```bash
   make clean && make && sudo make install
   ```

### Install from Docker Image

- **Pre-built image**: [intel/intel-qatzip](https://hub.docker.com/r/intel/intel-qatzip)
- **Build image**: Use the [Dockerfile](dockerfiles/README.md)

> **Note**: The image is built with the QATlib in-tree driver.

### Configuration

> **Note**: This section applies only to out-of-tree QAT packages. For in-tree QATlib, see the [QATlib configuration guide](https://intel.github.io/quickassist/qatlib/configuration.html).

The QATzip library requires a `[SHIM]` section in its configuration file. Set the environment variable `QAT_SECTION_NAME=SHIM` or use the provided configuration templates.

**Update configuration**:

1. Locate example configuration files:

   ```text
   $QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf
   ```

   - **QAT Devices**: `4xxx`, `c6xx`, `dh895xcc`, `c3xxx`
   - **Types**: `multiple_process_opt` or `multiple_thread_opt`

2. Copy and apply:

   ```bash
   sudo cp $QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf /etc
   sudo service qat_service restart
   ```

For more details, see the [QAT Programmer's Guide](https://www.intel.com/content/www/us/en/developer/topic-technology/open/quick-assist-technology/overview.html).

### Enable qzstd

To enable the lz4s + postprocessing pipeline, compile `qzstd`, a sample application supporting ZSTD format compression/decompression.

**Prerequisites**: Install the zstd static library before proceeding.

**Build**:

```bash
cd $QZ_ROOT
./autogen.sh
./configure --enable-lz4s-postprocessing
make clean && make qzstd
```

**Test**:

```bash
qzstd $your_input_file
```

## Test QATzip

Verify QATzip functionality using qzip tests below:

```bash
qzip -k $your_input_file -O gzipext -A deflate
```

**Compress with ZSTD\* or LZ4\* block format** (QAT 5.x (Gen6) only):

```bash
# Native ZSTD frame
qzip -k $your_input_file -A zstd -O zstd

# Raw LZ4 block
qzip -k $your_input_file -A lz4bk -O lz4bk
```

**Compress with a dictionary** (QAT 5.x (Gen6) only; dictionary file is capped at 32 KB):

```bash
qzip -k $your_input_file -A zstd -O zstd -D $your_dictionary_file
```

**Compress files to 7z**:

```bash
qzip -O 7z FILE1 FILE2 FILE3... -o result.7z
```

**Compress directories to 7z**:

```bash
qzip -O 7z DIR1 DIR2 DIR3... -o result.7z
```

**Decompress 7z archive**:

```bash
qzip -d result.7z
```

**Decompress directory with gzip/gzipext files**:

Use the `-R` option to recursively decompress gzip/gzipext files within a directory:

```bash
qzip -d -R DIR
```

## Performance Test With QATzip

Run the performance test script (uses the `qatzip-test` app):

```bash
cd $QZ_ROOT/test/performance_tests
./run_perf_test.sh
```

**Before testing**, update the following in `run_perf_test.sh`:

- Driver configuration for your QAT device (for OOT driver)
- Thread/process arguments
- `max_huge_pages_per_process`: Set to at least 6x the number of threads

## QATzip API Manual

Refer to `QATzip-man.pdf` in the `docs` folder. The `qzip` and `qatzip-test` man pages are available in the `man` folder. For additional QAT documentation, see the [QAT documentation site](https://intel.github.io/quickassist).

## Limitations

- **Internal chunking** — the 16 KB partitioned internal chunk size used for QAT hardware DMA is currently disabled.
- **LZ4\* history buffer** — (de)compression supports a 32 KB history buffer only.
- **Gen6-only formats** — `ZSTD` and `LZ4_BLOCK` are reported as unsupported on platforms without a QAT 5.x (Gen6, 6xxx series) device enabled.
- **Static Huffman** — `QZ_STATIC_HDR` is unsupported from Gen6 onwards. QATzip falls back to software for both directions, and fails if `sw_backup=0`.
- **`DEFLATE_RAW` decompression** — offloads only when the input is a single, complete deflate block whose uncompressed size is smaller than the hardware buffer size. Anything else, including multi-block input, falls back to software. Set the destination length accordingly.
- **`DEFLATE_ZLIB` compression and decompression** — offload only when the input is a single, complete deflate block whose compressed size is at most the hardware buffer size; otherwise the operation falls back to software. With multi-block input, hardware decompresses only the first block, so call the API again for the rest. Set the destination length accordingly.
- **`LZ4_BLOCK` buffer size** — `hw_buff_sz` must be a multiple of 64 KB (`QZ_LZ4_BLK_MAX_SIZE`). The format carries no end-of-block marker, so block boundaries derive solely from the sub-block count implied by `hw_buff_sz`; other values make the decompressor miscalculate them.
- **`LZ4_BLOCK` output buffer** — a block decodes all-or-nothing, so the decompression output buffer must hold a full 64 KB block. An undersized destination returns a fatal `QZ_FAIL`, which can make highly compressible data fail to decompress.
- **`qzstd` buffer size** — supports `hw_buffer_sz` values below 128 KB only.
- **Stream objects**:
  - Call `qzEndStream()` before reusing a stream in another session, and again before `qzTeardownSession()` to avoid memory leaks.
  - Keep the stream length below `strm_buff_sz`. Larger input produces a sequence of deflate blocks, with BFIN set on the last.
  - Supported formats are `DEFLATE_GZIP`, `DEFLATE_GZIP_EXT`, and `DEFLATE_RAW` for compression, and `DEFLATE_GZIP` and `DEFLATE_GZIP_EXT` for decompression.
  - Pre-allocation optimization using a thread-local stream buffer list is planned for a future release.
- **Dictionary size** — the dictionary buffer must not exceed 32 KB, and `dictId` applies to `ZSTD` only.
- **Dictionary software fallback** — `DEFLATE_GZIP` and `DEFLATE_GZIP_EXT` have no dictionary support on this path, because zlib's `deflateSetDictionary()` rejects gzip-wrapped streams. `LZ4s` dictionary requests that fall back to software also fail.
- **7z format** — only `*.7z` archives produced by `qzip` can be decompressed, decompression is software-only, and header compression is not supported.
- **Asynchronous API** — instance over-subscription is not supported. Each thread holds its own session and instance, so threads beyond the instance count offload to software.
- **Decompression thread count** — with `sw_backup=0`, `qatzip-test` on the decompression service hangs when the thread count exceeds the available decompression (dc) instances. Keep threads at or below the instance count, or enable software fallback.

## Licensing

- **Intel&reg; QuickAssist Technology (QAT) QATzip**: BSD-3-Clause License. Refer to the `LICENSE` file in the top-level directory for details.
- **Example Intel&reg; QuickAssist Technology Driver Configuration Files**: Dual BSD/GPLv2 License. Refer to the file headers and the `LICENSE.GPL` file in the `config_file` directory for details.

## Legal

Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2016-2026, Intel Corporation. All rights reserved.
