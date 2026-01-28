# Intel&reg; QuickAssist Technology (QAT) QATzip Library
QATzip is a user-space library built on top of the Intel&reg; QuickAssist Technology (QAT) user-space library. It provides extended compression and decompression capabilities by offloading these operations to Intel&reg; QAT Accelerators. QATzip generates data in the standard gzip\* format (RFC1952) with extended headers or [lz4\* blocks](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md) and using the [lz4\* frame format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md). The resulting data can be decompressed using any compliant gzip\* or lz4\* implementation. QATzip is optimized to fully leverage the performance benefits of Intel&reg; QuickAssist Technology.

## Table of Contents

- [Supported Formats](#supported-formats)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [QATzip Compression Level Mapping](#qatzip-compression-level-mapping)
- [Install Instructions](#install-instructions)
  - [Install with the in-tree QAT package](#install-with-the-in-tree-qat-package)
  - [Install with the out-of-tree QAT package](#install-with-the-out-of-tree-qat-package)
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
- **Latency Sensitive Mode**: Designed for high-stress scenarios, this mode offloads part of the workload to the CPU, leveraging Intel's multi-core processors to enhance throughput and reduce latency.
  > **Note**: It is not recommended to enable this mode in low-stress scenarios or for smaller data workloads (below 8KB), as it may result in reduced throughput.
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

## Hardware Requirements

This QATzip library supports compression and decompression offload on the platforms with the following QAT devices
acceleration devices:

* [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon.html)
* [Intel® QuickAssist Adapter 8970](https://www.intel.com/content/www/us/en/products/sku/125200/intel-quickassist-adapter-8970/downloads.html)
* [Intel® QuickAssist Adapter 8960](https://www.intel.com/content/www/us/en/products/sku/125199/intel-quickassist-adapter-8960/downloads.html)
* [Intel® QuickAssist Adapter 8950](https://www.intel.com/content/www/us/en/products/sku/80371/intel-communications-chipset-8950/specifications.html)
* [Intel® Atom&trade; Processor C3000](https://www.intel.com/content/www/us/en/products/docs/processors/atom/c-series/c3000-family-brief.html)

## Software Requirements

This release has been validated with the following:

- **Intel® QuickAssist Technology Driver for Linux\* HW v2.0 and v1.7**(Out of Tree): Latest from [Intel® QuickAssist Technology](https://www.intel.com/content/www/us/en/developer/topic-technology/open/quick-assist-technology/overview.html)
- **Intel® QATlib for Linux with intree driver**: [QATlib 24.09 or later](https://github.com/intel/qatlib/releases/tag/24.09.0)
- **Zlib\* Library**: Version `1.2.7`
- **LZ4\* Library**: Version `1.8.3`
- **Zstandard (zstd\*) Library**: Version `1.5.0`

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

| **QATzip Level** | **QAT Level** | **QAT 2.0 (deflate\*, LZ4\*, LZ4s\*)** | **QAT 1.7/1.8 (deflate\*)** |
|-------------------|---------------|-----------------------------------------|-----------------------------|
| 1                 | CPA_DC_L1     | 2 (HW_L1)                              | DEPTH_1                    |
| 2                 | CPA_DC_L2     | 2 (HW_L1)                              | DEPTH_4                    |
| 3                 | CPA_DC_L3     | 2 (HW_L1)                              | DEPTH_8                    |
| 4                 | CPA_DC_L4     | 2 (HW_L1)                              | DEPTH_16                   |
| 5                 | CPA_DC_L5     | 2 (HW_L1)                              | DEPTH_16                   |
| 6                 | CPA_DC_L6     | 8 (HW_L6)                              | DEPTH_16                   |
| 7                 | CPA_DC_L7     | 8 (HW_L6)                              | DEPTH_16                   |
| 8                 | CPA_DC_L8     | 8 (HW_L6)                              | DEPTH_16                   |
| 9                 | CPA_DC_L9     | 16 (HW_L9)                             | DEPTH_16                   |
| 10                | CPA_DC_L10    | 16 (HW_L9)                             | Unsupported                |
| 11                | CPA_DC_L11    | 16 (HW_L9)                             | Unsupported                |
| 12                | CPA_DC_L12    | 16 (HW_L9)                             | Unsupported                |

## Install Instructions
### Install with the in-tree QAT package

More information about installing QATzip using the in-tree driver from RPM or from source code is available [here](https://intel.github.io/quickassist/qatlib/index.html).

#### Install from RPM Packaging

Distributions like Fedora 34+, RHEL 8.4+ & 9.0+, CentOS 9 Stream, SUSE SLES15 SP3+, Ubuntu 24.04+, and Debian 13+ include the `qatzip` RPM package in their repositories. This package is built with the QAT_HW `qatlib` in-tree driver, specifically for 4xxx devices.

Install the `qatzip` package using the corresponding distribution's install command:

For RHEL-based distributions:
```bash
sudo dnf install -y qatzip qatzip-devel
```

For Debian-based distributions:
```bash
sudo apt -y install qatzip libqatzip3 libqatzip-dev
```

#### Install from Source Code

Refer to the [installation guide](https://intel.github.io/quickassist/qatlib/install.html#installing-from-sources) for more information.

```bash
cd QATzip/
export QZ_ROOT=$(pwd)
./autogen.sh
./configure
make clean
make
sudo make install
```

### Install with the out-of-tree QAT package

1. Refer to the [Intel&reg; QuickAssist Technology Installation Guide](https://intel.github.io/quickassist/GSG/2.X/installation.html) for detailed instructions on installing the out-of-tree QAT package.

> **Note**<br>
> - If you run QAT as a **non-root** user, additional manual steps are required. Please refer to the [non-root user guide](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user).<br>
> - If SVM is not enabled, memory passed to QAT hardware must be DMA’able. Intel provides a USDM component for allocating and freeing DMA-able memory. Refer to the [USDM settings guide](https://intel.github.io/quickassist/PG/infrastructure_memory_management.html#huge-pages) for more details.

2. Install the required package dependencies:

  For RHEL based distributions:
  ```bash
  sudo dnf install -y autoconf automake libtool zlib-devel lz4-devel numactl-devel
  ```

  For Debian based distributions:
  ```bash
  sudo apt -y install autoconf automake libtool zlib1g-dev liblz4-dev libnuma-dev
  ```

3. Configure the QATzip library:

  ```bash
  cd QATzip/
  export QZ_ROOT=$(pwd)
  export ICP_ROOT=/QAT/PACKAGE/PATH
  ./autogen.sh
  ./configure
  ```

  > **Note**<br>
  > Run `./configure -h` to view additional configuration options.

4. Build and install the QATzip library:

  ```bash
  make clean
  make
  sudo make install
  ```
### Install from Docker Image
Dockerfile with the qatlib intree driver dependancy which can be built into docker images can be used for installing qatzip. Please refer [here](dockerfiles/README.md) for more details.

### Configuration

> **Note**<br>
> This section is only required when you are using out-of-tree QAT package. if you are
> using qatlib with in-tree QAT package, please refer to [link](https://intel.github.io/quickassist/qatlib/configuration.html) for details on configuring qatlib.

The default QAT Driver configuration file does not contain a `[SHIM]` section, which is required by Intel&reg; QATzip.
To resolve this, set the environment variable `QAT_SECTION_NAME` to `SHIM`.

Refer to the [QAT Hardware v2.0 and v1.7 Programmer's Guide](https://www.intel.com/content/www/us/en/developer/topic-technology/open/quick-assist-technology/overview.html) for detailed information on software architecture, usage guidelines, and runtime customization options. The number of process supported per device varies according to Device type, for C6xx it is 32 process.
Alternatively, Intel&reg; QATzip provides example configuration files that include the `[SHIM]` section with the best-known tuning configurations. Follow these steps to update the configuration file:

1. Locate the example configuration files in the directory:
   ```
   $QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf
   ```

   - `YOUR_PLATFORM`: The QAT hardware platform (e.g., `c6xx` for Intel&reg; C62X Series Chipset, `dh895xcc` for Intel&reg; Communications Chipset 8925 to 8955 Series).
   - `CONFIG_TYPE`: The configuration type for different usage scenarios:
     - `multiple_process_opt`: Optimized for multiple processes.
     - `multiple_thread_opt`: Optimized for multiple threads.

2. Copy the appropriate configuration file(s) to the `/etc` directory and restart the driver:
   ```bash
   sudo cp $QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf /etc
   service qat_service restart
   ```

### Enable qzstd
If you want to enable lz4s + postprocessing pipeline, you have to compile qzstd which is a sample app to support ZSTD format compression/decompression. Before enabling qzstd, make sure that you have installed zstd static lib.

**Compile qzstd**

```bash
    cd $QZ_ROOT
    ./autogen.sh
    ./configure --enable-lz4s-postprocessing
    make clean
    make qzstd
```
**test qzstd**

```bash
    qzstd $your_input_file
```

## Test QATzip

Run the following command to check if the QATzip is setup correctly for
compressing or decompressing files:

```bash
    qzip -k $your_input_file  -O gzipext -A deflate
```

### File compression in 7z:
```bash
    qzip -O 7z FILE1 FILE2 FILE3... -o result.7z
```
### Dir compression in 7z:
```bash
    qzip -O 7z DIR1 DIR2 DIR3... -o result.7z
```
### Decompression file in 7z:
```bash
    qzip -d result.7z
```
### Dir Decompression with -R:
If the DIR contains files that are compressed by qzip and using gzip/gzipext
format, then it should be add `-R` option to decompress them:
```bash
    qzip -d -R DIR
```

## Performance Test With QATzip

Please run the QATzip (de)compression performance test with the following command.
Please update the drive configuration and process/thread argument in run_perf_test.sh
before running the performance test.
Note that when the number of threads is changed, the argument "max_huge_pages_per_process"
in run_perf_test.sh should be changed accordingly, to at least 6 times the number of threads.

```bash
    cd $QZ_ROOT/test/performance_tests
    ./run_perf_test.sh
```

## QATzip API Manual

Please refer to file `QATzip-man.pdf` under the `docs` folder and Refer this [link](https://intel.github.io/quickassist) for additional QAT Documents

## Limitations

* The partitioned internal chunk size of 16 KB, used for QAT hardware DMA, is currently disabled.
* For stream objects:
  - Reset the stream object by calling `qzEndStream()` before reusing it in another session.
  - Clear the stream object by calling `qzEndStream()` before clearing the session object with `qzTeardownSession()` to avoid memory leaks.
  - Ensure the stream length is smaller than `strm_buff_sz`. If it exceeds this size, QATzip will generate multiple deflate blocks in sequence, with the last block marked as the final block (BFIN set).
  - Performance optimization for the pre-allocation process using a thread-local stream buffer list is planned for a future release.
* For the 7z format:
  - Decompression supports only `*.7z` archives compressed by `qzip`.
  - Decompression is limited to software-only processing.
  - Header compression is not supported.
* For LZ4\* (de)compression, QATzip supports only a 32 KB history buffer.
* For Zstandard (zstd) format compression, `qzstd` supports only `hw_buffer_sz` values less than 128 KB.
* Stream APIs currently support the following:
  - Compression: `DEFLATE_GZIP`, `DEFLATE_GZIP_EXT`, and `DEFLATE_RAW`.
  - Decompression: `DEFLATE_GZIP` and `DEFLATE_GZIP_EXT`.
* For `DEFLATE_RAW` hardware decompression:
  - To offload to hardware, the input data must be a single, complete deflate block with an uncompressed size smaller than the hardware buffer size. Otherwise, it will fall back to software processing. The API's input destination length must be set appropriately by the user.
  - If the input data contains multiple blocks and software fallback is enabled, the operation will default to software processing.
* For `DEFLATE_ZLIB` hardware compression and decompression:
  - To offload to hardware, the input data must be a single, complete deflate block with a compressed size smaller than or equal to the hardware buffer size. Otherwise, it will fall back to software processing. The API's input destination length must be set appropriately by the user.
  - If the input data contains multiple blocks, only the first complete block will be decompressed by hardware. The user must call the decompression API for subsequent blocks.
* When using the asynchronous API:
  - Instance over-subscription is not supported. The number of threads should not exceed the number of instances, as each thread maintains its own session and instance. Additional threads will offload workloads to software.
* For latency-sensitive mode:
  - Avoid enabling this mode in low-stress scenarios or for smaller data workloads (below 8 KB), as it may result in decreased throughput for QATzip.


## Licensing

The licensing for this project is divided as follows:

- **Intel&reg; QuickAssist Technology (QAT) QATzip**: BSD License. Refer to the `LICENSE` file in the top-level directory for details.
- **Example Intel&reg; QuickAssist Technology Driver Configuration Files**: Dual BSD/GPLv2 License. Refer to the file headers and the `LICENSE.GPL` file in the `config_file` directory for details.

## Legal
Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

\*Other names and brands may be claimed as the property of others.

Copyright © 2016-2026, Intel Corporation. All rights reserved.
