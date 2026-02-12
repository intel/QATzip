# Intel&reg; QuickAssist Technology (QAT) QATzip Library

QATzip is a user-space library built on top of the Intel&reg; QuickAssist Technology (QAT) user-space library. It provides extended compression and decompression capabilities by offloading these operations to Intel&reg; QAT Accelerators. QATzip generates data in the standard gzip\* format (RFC1952) with extended headers or [lz4\* blocks](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md) and using the [lz4\* frame format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md). The resulting data can be decompressed using any compliant gzip\* or lz4\* implementation. QATzip is optimized to fully leverage the performance benefits of Intel&reg; QuickAssist Technology.

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

| Data Format            | Algorithm | QAT Device         | Description                                                                                   |
|-------------------------|-----------|-------------------|-----------------------------------------------------------------------------------------------|
| `QZ_DEFLATE_4B`        | deflate\* | QAT 1.x & QAT 2.0  | Data is in DEFLATE\* with a 4-byte header.                                                    |
| `QZ_DEFLATE_GZIP`      | deflate\* | QAT 1.x & QAT 2.0  | Data is in DEFLATE\* wrapped by a Gzip\* header and footer.                                   |
| `QZ_DEFLATE_GZIP_EXT`  | deflate\* | QAT 1.x & QAT 2.0  | Data is in DEFLATE\* wrapped by an Intel&reg; QAT Gzip\* extension header and footer.         |
| `QZ_DEFLATE_RAW`       | deflate\* | QAT 1.x & QAT 2.0  | Data is in raw DEFLATE\* without any additional header. (Compression only; decompression falls back to software.) |
| `QZ_LZ4`               | lz4\*     | QAT 2.0            | Data is in LZ4\* wrapped by an lz4\* frame.                                                   |
| `QZ_LZ4S`              | lz4s\*    | QAT 2.0            | Data is in LZ4S\* blocks.                                                                     |

## Features

- Accelerated compression and decompression using Intel&reg; QuickAssist Technology, including utilities for file compression and decompression.
- Dynamic memory allocation for zero-copy operations via `qzMalloc()` and `qzFree()`, enabling pinned, contiguous buffers for DMA operations.
- Instance over-subscription, allowing multiple threads in the same process to share fewer hardware instances seamlessly.
- Memory allocation backed by huge pages and kernel memory for pinned, contiguous memory access, with fallback to huge pages during kernel memory contention.
- Configurable accelerator device sharing across processes.
- Optional software failover for compression and decompression, ensuring functionality even when system resources are insufficient.
- Streaming interfaces for compression and decompression to improve compression ratios and throughput for piecemeal data submissions.
- Asynchronous interfaces for compression and decompression to achieve lower latency and higher throughput (not supported by the `qzip` utility). This is particularly beneficial for fewer instances or smaller data packets (below 64KB).
- Latency Sensitive Mode: Designed for high-stress scenarios, this mode offloads part of the workload to the CPU, leveraging Intel's multi-core processors to enhance throughput and reduce latency.
  > Note: It is not recommended to enable this mode in low-stress scenarios or for smaller data workloads (below 8KB), as it may result in reduced throughput.
- Adaptive polling mechanisms to reduce CPU usage under stress.
- Support for compressing files and directories into the 7z format using the `qzip` utility.
- Compatibility with QATzip Gzip\* format, which includes a 10-byte header and an 8-byte footer:

  ```
  | ID1 (1B) | ID2(0x8B) (1B) | Compression Method (8 = DEFLATE*) (1B) | Flags (1B) | Modification Time (4B) | Extra Flags (1B) | OS (1B) | Deflate Block | CRC32 (4B) | ISIZE (4B) |
  ```

- Support for QATzip Gzip\* extended format, which extends the standard 10-byte Gzip\* header by an additional 14 bytes:

  ```
  | Length of ext. header (2B) | SI1('Q') (1B) | SI2('Z') (1B) | Length of subheader (2B) | Intel(R) defined field 'Chunksize' (4B) | Intel(R) defined field 'Blocksize' (4B) |
  ```

- Support for Intel&reg; QATzip 4-byte headers, indicating the length of the compressed block:

  ```
  | Intel(R) defined Header (4B) | deflate\* block |
  ```

- Support for QATzip lz4\* format, structured as follows:

  ```
  | MagicNb (4B) | FLG (1B) | BD (1B) | CS (8B) | HC (1B) | lz4\* Block | EndMark (4B) |
  ```

## QATzip Compression Level Mapping

The following table shows how standard software `zlib*` compression levels map to `QATzip` levels for QAT 1.x and QAT 2.0:

| Software `zlib*` Levels | QAT 1.x Equivalent | QAT 2.0 Equivalent |
|-------------------------|--------------------|---------------------|
| 1 - 4                  | QATzip Level 1     | QATzip Level 1      |
| 5                      | QATzip Level 5     | QATzip Level 1      |
| 6 - 8                  | QATzip Level 5     | QATzip Level 6      |
| 9                      | QATzip Level 9     | QATzip Level 9      |
| 10 - 12                | Unsupported        | QATzip Level 9      |

Refer to [QAT Compression levels](https://intel.github.io/quickassist/PG/services_compression_api.html#compression-levels) that summarizes how QATzip Level translates to hardware-accelerated levels for each QAT generation.

## Hardware Requirements

This QATzip library supports compression and decompression offload on the platforms with the following QAT devices
acceleration devices:

- [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon.html)
- [Intel® QuickAssist Adapter 8970](https://www.intel.com/content/www/us/en/products/sku/125200/intel-quickassist-adapter-8970/downloads.html)
- [Intel® QuickAssist Adapter 8960](https://www.intel.com/content/www/us/en/products/sku/125199/intel-quickassist-adapter-8960/downloads.html)
- [Intel® QuickAssist Adapter 8950](https://www.intel.com/content/www/us/en/products/sku/80371/intel-communications-chipset-8950/specifications.html)
- [Intel® Atom&trade; Processor C3000](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-atom-processor-c3000-family-technical-overview.html)

## Software Requirements

- Intel® QuickAssist Technology Driver for Linux\* HW v2.0 or v1.7 (Out of Tree), latest from [Intel® QuickAssist Technology](https://developer.intel.com/quickassist)
- Intel® QATlib for Linux (in-tree) - [v25.08](https://github.com/intel/qatlib/releases) or later
- Zlib: v1.2.7 or later
- LZ4: v1.8.3 or later
- Zstandard (zstd): v1.5.0 or later

Distributions like Fedora 34+, RHEL 8.4+ & 9.0+, CentOS 9 Stream, SUSE SLES15 SP3+, Ubuntu 24.04+ and Debian13+ include the `qatzip` RPM package in their repositories. This package is built with the QAT_HW `qatlib` in-tree driver, specifically for 4xxx devices.

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
   sudo dnf install -y autoconf autoconf-archive automake libtool zlib-devel lz4-devel numactl-devel

   # Debian-based
   sudo apt -y install autoconf autoconf-archive automake libtool zlib1g-dev liblz4-dev libnuma-dev
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
Note: This is built with QATlib in-tree driver

### Configuration

> **Note**: This section applies only to out-of-tree QAT packages. For in-tree QATlib, see the [QATlib configuration guide](https://intel.github.io/quickassist/qatlib/configuration.html).

The QATzip library requires a `[SHIM]` section in its configuration file. Set the environment variable `QAT_SECTION_NAME=SHIM` or use the provided configuration templates.

**Update configuration**:

1. Locate example configuration files:
   ```
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

Please refer to file `QATzip-man.pdf` under the `docs` folder and Refer this [link](https://intel.github.io/quickassist) for additional QAT Documents

## Limitations

- The partitioned internal chunk size of 16 KB, used for QAT hardware DMA, is currently disabled.
- For stream objects:
  - Reset the stream object by calling `qzEndStream()` before reusing it in another session.
  - Clear the stream object by calling `qzEndStream()` before clearing the session object with `qzTeardownSession()` to avoid memory leaks.
  - Ensure the stream length is smaller than `strm_buff_sz`. If it exceeds this size, QATzip will generate multiple deflate blocks in sequence, with the last block marked as the final block (BFIN set).
  - Performance optimization for the pre-allocation process using a thread-local stream buffer list is planned for a future release.
- For the 7z format:
  - Decompression supports only `*.7z` archives compressed by `qzip`.
  - Decompression is limited to software-only processing.
  - Header compression is not supported.
- For LZ4\* (de)compression, QATzip supports only a 32 KB history buffer.
- For Zstandard (zstd) format compression, `qzstd` supports only `hw_buffer_sz` values less than 128 KB.
- Stream APIs currently support the following:
  - Compression: `DEFLATE_GZIP`, `DEFLATE_GZIP_EXT`, and `DEFLATE_RAW`.
  - Decompression: `DEFLATE_GZIP` and `DEFLATE_GZIP_EXT`.
- For `DEFLATE_RAW` hardware decompression:
  - To offload to hardware, the input data must be a single, complete deflate block with an uncompressed size smaller than the hardware buffer size. Otherwise, it will fall back to software processing. The API's input destination length must be set appropriately by the user.
  - If the input data contains multiple blocks and software fallback is enabled, the operation will default to software processing.
- For `DEFLATE_ZLIB` hardware compression and decompression:
  - To offload to hardware, the input data must be a single, complete deflate block with a compressed size smaller than or equal to the hardware buffer size. Otherwise, it will fall back to software processing. The API's input destination length must be set appropriately by the user.
  - If the input data contains multiple blocks, only the first complete block will be decompressed by hardware. The user must call the decompression API for subsequent blocks.
- When using the asynchronous API:
  - Instance over-subscription is not supported. The number of threads should not exceed the number of instances, as each thread maintains its own session and instance. Additional threads will offload workloads to software.
- For latency-sensitive mode:
  - Avoid enabling this mode in low-stress scenarios or for smaller data workloads (below 8 KB), as it may result in decreased throughput for QATzip.

## Licensing

- **Intel&reg; QuickAssist Technology (QAT) QATzip**: BSD-3-Clause License. Refer to the `LICENSE` file in the top-level directory for details.
- **Example Intel&reg; QuickAssist Technology Driver Configuration Files**: Dual BSD/GPLv2 License. Refer to the file headers and the `LICENSE.GPL` file in the `config_file` directory for details.

## Legal

Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2016-2026, Intel Corporation. All rights reserved.
