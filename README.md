# Intel&reg; QuickAssist Technology (QAT) QATzip Library

## Table of Contents

- [Introduction](#introduction)
- [Licensing](#licensing)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Additional Information](#additional-information)
- [Limitations](#limitations)
- [Installation Instructions](#installation-instructions)
  - [Build Intel&reg; QuickAssist Technology Driver](#build-intel-quickassist-technology-driver)
  - [Install QATzip As Root User](#install-qatzip-as-root-user)
  - [Install QATzip As Non-root User](#install-qatzip-as-non-root-user)
  - [Enable qzstd](#enable-qzstd)
  - [Test QATzip](#test-qatzip)
  - [Performance Test With QATzip](#performance-test-with-qatzip)
- [QATzip API manual](#qatzip-api-manual)
- [Intended Audience](#intended-audience)
- [Open Issues](#open-issues)
- [Legal](#legal)

## Introduction

QATzip is a user space library which builds on top of the Intel&reg; QuickAssist
Technology user space library, to provide extended accelerated compression and
decompression services by offloading the actual compression and decompression
request(s) to the Intel&reg; Chipset Series. QATzip produces data using the standard
gzip\* format (RFC1952) with extended headers or [lz4\* blocks][7] with [lz4\* frame format][8].
The data can be decompressed with a compliant gzip\* or lz4\* implementation. QATzip is
designed to take full advantage of the performance provided by Intel&reg; QuickAssist Technology.

The currently supported formats include:

|Data Format|Algorithm|QAT device|Description|
| :---------------:     |  :---------------: |:---------------: | :------------------------------------------------------------: |
| `QZ_DEFLATE_4B`       | deflate\*            | QAT 1.x and QAT 2.0|Data is in DEFLATE\* with a 4 byte header|
| `QZ_DEFLATE_GZIP`     | deflate\*            | QAT 1.x and QAT 2.0|Data is in DEFLATE\* wrapped by Gzip\* header and footer|
| `QZ_DEFLATE_GZIP_EXT` | deflate\*            | QAT 1.x and QAT 2.0|Data is in DEFLATE\* wrapped by Intel&reg; QAT Gzip\* extension header and footer|
| `QZ_DEFLATE_RAW`      | deflate\*            | QAT 1.x and QAT 2.0|Data is in raw DEFLATE\* without any additional header. (Only support compression, decompression will fallback to software) |
| `QZ_LZ4`              | lz4\*                | QAT 2.0|Data is in LZ4\*  wrapped by lz4\* frame |
| `QZ_LZ4S`             | lz4s\*               | QAT 2.0|Data is in LZ4S\* blocks |

## Licensing

The Licensing of the files within this project is split as follows:

Intel&reg; Quickassist Technology (QAT) QATzip - BSD License. Please see the `LICENSE`
file contained in the top level folder. Further details can be found in the file headers
of the relevant files.

Example Intel&reg; Quickassist Technology Driver Configuration Files contained within the
folder hierarchy `config_file` - Dual BSD/GPLv2 License.
Please see the file headers of the configuration files, and the full GPLv2 license
contained in the file `LICENSE.GPL` within the `config_file` folder.

## Features

* Acceleration of compression and decompression utilizing Intel&reg; QuickAssist Technology,
  including a utility to compress and decompress files.
* Dynamic memory allocation for zero copy, by exposing qzMalloc() and qzFree() allowing
  working buffers to be pinned, contiguous buffers that can be used for DMA operations to
  and from the hardware.
* Instance over-subscription, allowing a number of threads in the same process to
  seamlessly share a smaller number of hardware instances.
* Memory allocation backed by huge page and kernel memory to provide access to pinned,
  contiguous memory. Allocating from huge-page when kernel memory contention.
* Configurable accelerator device sharing among processes.
* Optional software failover for both compression and decompression services. QATzip may
  switch to software if there is insufficient system resources including acceleration
  instances or memory. This feature allows for a common software stack between server
  platforms that have acceleration devices and non-accelerated platforms.
* Provide streaming interface of compression and decompression to achieve better compression
  ratio and throughput for data sets that are submitted piecemeal.
* 'qzip' utility supports compression from regular file, pipeline and block device.
* For QATzip GZIP\* format, try hardware decompression 1st before switch to software decompression.
* Enable adaptive polling mechanism to save CPU usage in stress mode.
* 'qzip' utility supports compression files and directories into 7z format.
* Support QATzip Gzip\* format, it includes 10 bytes header and 8 bytes footer:

  `| ID1 (1B) | ID2(0x8B) (1B) | Compression Method (8 = DEFLATE*) (1B) | Flags (1B) | Modification Time (4B) | Extra Flags (1B) | OS (1B) | Deflate Block| CRC32(4B)| ISIZE(4B)|`
* Support QATzip Gzip\* extended format. This consists of the standard 10 byte Gzip* header and follows RFC 1952 to extend the header by an additional 14 bytes. The extended headers structure is below:

  `| Length of ext. header (2B) | SI1('Q') (1B) | SI2('Z') (1B) | Length of subheader (2B) | Intel(R) defined field 'Chunksize' (4B) | Intel(R) defined field 'Blocksize' (4B) | `
* Support Intel&reg; QATzip 4 byte header, the header indicates the length of the compressed block followed by the header.

  `| Intel(R) defined Header (4B)|deflate\* block|`
* Support QATzip lz4* format. This format is structured as follows:

  `| MagicNb(4B) |FLG(1B)|BD(1B)| CS(8B)|HC(1B)| |lz4\* Block | EndMark(4B)|`
## Hardware Requirements

This QATzip library supports compression and decompression offload to the following
acceleration devices:

* [Intel&reg; C62X Series Chipset][1]
* [Intel&reg; Communications Chipset 8925 to 8955 Series][2]
* [Intel&reg; Communications Chipset 8960 to 8970 Series][3]
* [Intel&reg; C3XXX Series Chipset][4]
* [Intel&reg; 4XXX Series][5]

## Software Requirements

This release was validated on the following:

* QATzip has been tested with the latest Intel&reg; QuickAssist Acceleration Driver.
  Please download the QAT driver from the link [Intel&reg; QuickAssist Technology][6]
* QATzip has been tested by Intel&reg; on CentOS\* 7.8.2003 with kernel 3.10.0-1127.19.1.el7.x86\_64
* Zlib\* library of version 1.2.7 or higher
* Suggest GCC\* of version 4.8.5 or higher
* lz4\* library
* zstd\* static library


## Additional Information

* For QAT 1.x, the compression level in QATzip could be mapped to standard zlib\* as below:
  * QATzip level 1 - 4, similar to zlib\* level 1 - 4.
  * QATzip level 5 - 8, we map them to QATzip level 4.
  * QATzip level 9, we will use software zlib\* to compress as level 9.
* For QAT 2.0, the compression level in QATzip could be mapped to standard zlib\* or lz4\* as below:
  * Will be updated in future releases.

* QATzip Compression Level Mapping:

  | QATzip Level |QAT Level| QAT 2.0(deflate\*, LZ4\*, LZ4s\*)  |QAT1.7/1.8(Deflate\*) |
  |  ---- | --- | ----  |  ----  |
  | 1  | CPA_DC_L1 |2(HW_L1) | DEPTH_1  |
  | 2  | CPA_DC_L2 |2(HW_L1) | DEPTH_4   |
  | 3  | CPA_DC_L3 |2(HW_L1) | DEPTH_8   |
  | 4  | CPA_DC_L4 |2(HW_L1) | DEPTH_16   |
  | 5  | CPA_DC_L5 |2(HW_L1) | DEPTH_16   |
  | 6  | CPA_DC_L6 |8(HW_L6) | DEPTH_16   |
  | 7  | CPA_DC_L7 |8(HW_L6) | DEPTH_16   |
  | 8  | CPA_DC_L8 |8(HW_L6) | DEPTH_16   |
  | 9  | CPA_DC_L9 |16(HW_L9) | DEPTH_16   |
  | 10  | CPA_DC_L10 |16(HW_L9)  | Unsupported |
  | 11  | CPA_DC_L11 |16(HW_L9) | Unsupported  |
  | 12  | CPA_DC_L12 |16(HW_L9) | Unsupported  |

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

## Installation Instructions

### Build Intel&reg; QuickAssist Technology Driver

Please follow the instructions contained in:

**For Intel&reg; C62X Series Chipset:**

[Intel&reg; QuickAssist Technology Software for Linux\* - Getting Started Guide - HW version 1.7][11]

**For Intel&reg; Communications Chipset 89XX Series:**

[Intel&reg; Communications Chipset 89xx Series Software for Linux\* - Getting Started Guide][12]

**For Intel&reg; 4XXX Series:**

[Intel&reg; QuickAssist Technology (Intel&reg; QAT) Software for Linux\* Getting Started Guide – Hardware Version 2.0][13]


### Install QATzip As Root User

**Set below environment variable**

`ICP_ROOT`: the root directory of your QAT driver source tree

`QZ_ROOT`: the root directory of your QATzip source tree

**Enable huge page**

```bash
    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1024 max_huge_pages_per_process=48
```

**Compile and install QATzip**

```bash
    cd $QZ_ROOT
    ./autogen.sh
    ./configure --with-ICP_ROOT=$ICP_ROOT
    make clean
    make
    make install
```

For more configure options, please run "./configure -h" for help

**Update configuration files**

___Have to update those file, Otherwise QATzip will be Unavailable.___

QAT's the programmer’s guide which provides information on the architecture of the software
and usage guidelines. it allows customization of runtime operation.

* [Intel&reg; QAT 1.7 linux Programmer's Guide][9]
* [Intel&reg; QAT 2.0 linux Programmer's Guide][10]

The Intel&reg; QATzip comes with some tuning example conf files to use. you can replace the
old conf file(under /etc/) by them. The detailed info about Configurable options, please
refer Programmer's Guide manual.

The process section name(in configuration file) is the key change for QATzip.
there are two way to change:
* QAT Driver default conf file does not contain a [SHIM] section which the Intel&reg; QATzip
  requires by default. you can follow below step to replace them.
* The default section name in the QATzip can be modified if required by setting the environment
variable "QAT_SECTION_NAME".

To update the configuration file, copy the configure file(s) from directory of
`$QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf`
to directory of `/etc`

`YOUR_PLATFORM`: the QAT hardware platform, c6xx for Intel&reg; C62X Series
Chipset, dh895xcc for Intel&reg; Communications Chipset 8925 to 8955 Series

`CONFIG_TYPE`: tuned configure file(s) for different usage,
`multiple_process_opt` for multiple process optimization,
`multiple_thread_opt` for multiple thread optimization.

**Restart QAT driver**

```bash
    service qat_service restart
```

With current configuration, each PCI-e device in C6XX platform could support
32 processes in maximum.

### Install QAT As Non-root User
  Please refer to [Intel&reg; QuickAssist Technology (Intel&reg; QAT) Software for Linux\* Getting Started Guide][13] section "3.8 Running Applications as Non-Root User".

### Enable qzstd
If you want to enable lz4s + postprocessing pipeline, you have to compile qzstd. which
is a sample app to support ZSTD format compression/decompression. before enabling qzstd,
make sure that you have installed zstd static lib.

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
    qzstd -k $your_input_file
```

### Test QATzip

Run the following command to check if the QATzip is setup correctly for
compressing or decompressing files:

```bash
    qzip -k $your_input_file  -O gzipext -A deflate
```

#### File compression in 7z:
```bash
    qzip -O 7z FILE1 FILE2 FILE3... -o result.7z
```
#### Dir compression in 7z:
```bash
    qzip -O 7z DIR1 DIR2 DIR3... -o result.7z
```
#### Decompression file in 7z:
```bash
    qzip -d result.7z
```
#### Dir Decompression with -R:
If the DIR contains files that are compressed by qzip and using gzip/gzipext
format, then it should be add `-R` option to decompress them:
```bash
    qzip -d -R DIR
```

### Performance Test With QATzip

Please run the QATzip (de)compression performance test with the following command.
Please update the drive configuration and process/thread argument in run_perf_test.sh
before running the performance test.
Note that when number for threads changed, the argument "max_huge_pages_per_process"
in run_perf_test.sh should be changed accordingly, at least 6 times of threads number.

```bash
    cd $QZ_ROOT/test/performance_tests
    ./run_perf_test.sh
```

## QATzip API Manual

Please refer to file `QATzip-man.pdf` under the `docs` folder

## Open Issues
Known issues relating to the QATzip are described in this section.

### QATAPP-26069
| Title      |     Buffers allocated with qzMalloc() can't be freed after calling qzMemDestory    |
|----------|:-------------
| Reference   | QATAPP-26069 |
| Description | If the users call qzFree after qzMemDestory, they may encounter free memory error "free(): invalid pointe" |
| Implication | User use qzMalloc API to allocate continuous memory |
| Resolution | Ensure qzMemDestory is invoked after qzFree, now we use attribute destructor to invoke qzMemDestory|
| Affected OS | Linux |

## Intended Audience

The target audience is software developers, test and validation engineers,
system integrators, end users and consumers for QATzip integrated Intel&reg;
Quick Assist Technology

## Legal

Intel&reg; disclaims all express and implied warranties, including without
limitation, the implied warranties of merchantability, fitness for a
particular purpose, and non-infringement, as well as any warranty arising
from course of performance, course of dealing, or usage in trade.

This document contains information on products, services and/or processes in
development.  All information provided here is subject to change without
notice. Contact your Intel&reg; representative to obtain the latest forecast
, schedule, specifications and roadmaps.

The products and services described may contain defects or errors known as
errata which may cause deviations from published specifications. Current
characterized errata are available on request.

Copies of documents which have an order number and are referenced in this
document may be obtained by calling 1-800-548-4725 or by visiting
www.intel.com/design/literature.htm.

Intel, the Intel logo are trademarks of Intel Corporation in the U.S.
and/or other countries.

\*Other names and brands may be claimed as the property of others

[1]:https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/purley/intel-xeon-scalable-processors.html
[2]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8950-brief.html
[3]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8960-8970-brief.html
[4]:https://www.intel.com/content/www/us/en/products/docs/processors/atom/c-series/c3000-family-brief.html
[5]:https://www.intel.com/content/www/us/en/products/details/processors/xeon/scalable.html
[6]:https://www.intel.com/content/www/us/en/developer/topic-technology/open/quick-assist-technology/overview.html
[7]:https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
[8]:https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md
[9]:https://www.intel.com/content/www/us/en/content-details/710060/intel-quickassist-technology-software-for-linux-programmer-s-guide-hw-version-1-7.html
[10]:https://www.intel.com/content/www/us/en/content-details/743912/intel-quickassist-technology-intel-qat-software-for-linux-programmers-guide-hardware-version-2-0.html
[11]:https://www.intel.com/content/www/us/en/content-details/710059/intel-quickassist-technology-software-for-linux-getting-started-guide-customer-enabling-release.html
[12]:https://www.intel.com/content/www/us/en/content-details/710089/intel-communications-chipset-89xx-series-software-for-linux-getting-started-guide.html
[13]:https://www.intel.com/content/www/us/en/content-details/632506/intel-quickassist-technology-intel-qat-software-for-linux-getting-started-guide-hardware-version-2-0.html
