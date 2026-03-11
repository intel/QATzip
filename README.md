# Intel&reg; QuickAssist Technology (QAT) QATzip Library

QATzip is a user-space library built on top of the Intel&reg; QuickAssist Technology (QAT) user-space library. It provides extended compression and decompression capabilities by offloading these operations to Intel&reg; QAT Accelerators. QATzip generates data in the standard gzip\* format (RFC1952) with extended headers or [lz4\* blocks](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md) and using the [lz4\* frame format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md). The resulting data can be decompressed using any compliant gzip\* or lz4\* implementation. QATzip is optimized to fully leverage the performance benefits of Intel&reg; QuickAssist Technology.

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
| `QZ_ZSTD`              | zstd\*    | QAT 5.x            | Data is in ZSTD\* wrapped by a zstd\* frame.                                                  |
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

Distributions like Fedora 34+, RHEL 8.4+ & 9.0+, CentOS 9 Stream, SUSE SLES15 SP3+, Ubuntu 24.04+ and Debian13+ include the `qatzip` RPM package in their repositories. This package is built with the QAT_HW `qatlib` in-tree driver, specifically for 4xxx devices.

### QATzip Compression Level Mapping

#### QAT 1.x Compression Levels
- The compression levels in QATzip for QAT 1.x are mapped to standard zlib\* levels as follows:
  - **QATzip Levels 1 - 4**: Correspond to zlib\* Levels 1 - 4.
  - **QATzip Levels 5 - 8**: Mapped to QATzip Level 4.
  - **QATzip Level 9**: Uses software zlib\* for compression at Level 9.

#### QAT 2.0 Compression Levels
- The compression levels in QATzip for QAT 2.0 will be mapped to standard zlib\* or lz4\* levels. Details will be provided in future releases.

#### Detailed Compression Level Mapping

## Limitations

* The partitioned internal chunk size of 16 KB is disabled, this chunk is used for QAT hardware DMA.
* For stream object, user should reset the stream object by calling qzEndStream() before reuse it
  in the other session.
* For stream object, user should clear stream object by calling qzEndStream() before clear session
  object with qzTeardownSession(). Otherwise, memory leak happens.
* For stream object, stream length must be smaller than `strm_buff_sz`, or QATzip would generate multiple
  deflate block in order and has the last block with BFIN set.
* For stream object, we will optimize the performance of the pre-allocation process using a thread-local
  stream buffer list in a future release.
* For 7z format, decompression only supports \*.7z archives compressed by qzip.
* For 7z format, decompression only supports software.
* For 7z format, the header compression is not supported.
* For lz4\* (de)compression, QATzip only supports 32KB history buffer.
* For zstd format compression, qzstd only supports `hw_buffer_sz` which is less than 128KB.
* Stream APIs only support "DEFLATE_GZIP", "DEFLATE_GZIP_EXT", "DEFLATE_RAW" for compression
  and "DEFLATE_GZIP", "DEFLATE_GZIP_EXT" for decompression now.
* DEFLATE_RAW hardware decompression is supported and input data must be complete deflate block. If input data is not complete block and software backup is enabled decompression will fallback on software else failure will be returned.

## Installation Instructions

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

#### Install from RPM Packaging

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

## Known Issues

## Legal

Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2016-2026, Intel Corporation. All rights reserved.
