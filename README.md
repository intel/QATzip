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
    - [Test QATzip](#test-qatzip)
    - [Performance Test With QATzip](#performance-test-with-qatzip)
- [QATzip API manual](#qatzip-api-manual)
- [Intended Audience](#intended-audience)
- [Legal](#legal)

## Introduction

QATzip is a user space library which builds on top of the Intel&reg; QuickAssist
Technology user space library, to provide extended accelerated compression and
decompression services by offloading the actual compression and decompression
request(s) to the Intel&reg; Chipset Series. QATzip produces data using the standard
gzip\* format (RFC1952) with extended headers. The data can be decompressed with a
compliant gzip\* implementation. QATzip is designed to take full advantage of the
performance provided by Intel&reg; QuickAssist Technology.

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
* Automatic recovery from hardware compression failure.
* Provide streaming interface of compression and decompression to achieve better compression
ratio and throughput for data sets that are submitted piecemeal.
* 'qzip' utility supports compression from regular file, pipeline and block device.
* For standard GZIP format, try hardware decompression 1st before switch to software decompression.
* Enable adaptive polling mechanism to save CPU usage in stress mode.
* 'qzip' utility supports compression files and directories into 7z format.

## Hardware Requirements

This QATzip library supports compression and decompression offload to the following
acceleration devices:

* [Intel&reg; C62X Series Chipset][1]
* [Intel&reg; Communications Chipset 8925 to 8955 Series][2]
* [Intel&reg; Communications Chipset 8960 to 8970 Series][3]
* [Intel&reg; C3XXX Series Chipset][4]

[1]:https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/purley/intel-xeon-scalable-processors.html
[2]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8950-brief.html
[3]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8960-8970-brief.html
[4]:https://www.intel.com/content/www/us/en/products/docs/processors/atom/c-series/c3000-family-brief.html

## Software Requirements

This release was validated on the following:

* QATzip has been tested with the latest Intel&reg; QuickAssist Acceleration Driver.
Please download the QAT driver from the link https://01.org/intel-quickassist-technology
* QATzip has been tested by Intel&reg; on CentOS 7.8.2003 with kernel 3.10.0-1127.19.1.el7.x86\_64
* Zlib\* library of version 1.2.7 or higher
* Suggest GCC\* of version 4.8.5 or higher

## Additional Information

The compression level in QATzip could be mapped to standard zlib\* as below:
* QATzip level 1 - 4, similar to zlib\* level 1 - 4.
* QATzip level 5 - 8, we map them to QATzip level 4.
* QATzip level 9, we will use software zlib\* to compress as level 9.

## Limitations

* The partitioned internal chunk size of 16 KB is disabled, this chunk is used for QAT hardware DMA.
* For stream object, user should reset the stream object by calling qzEndStream() before reuse it
  in the other session.
* For stream object, user should clear stream object by calling qzEndStream() before clear session
  object with qzTeardownSession(). Otherwise, memory leak happens.
* For stream object, stream lenth must be smaller than `strm_buff_sz`, or QATzip would generate multiple
  deflate block in order and has the last block with BFIN set.
* For 7z format, decompression only supports \*.7z archives compressed by qzip.
* For 7z format, decompression only supports software.
* For 7z format, the header compression is not supported.
* Currently,the [upstream driver](https://github.com/intel/qatlib) don't support compression.
  So that please uninstall the [upstream driver](https://github.com/intel/qatlib) before installing QATzip.



## Installation Instructions

### Build Intel&reg; QuickAssist Technology Driver

Please follow the instructions contained in:

**For Intel&reg; C62X Series Chipset:**
Intel&reg; QuickAssist Technology Software for Linux\* - Getting Started Guide - HW version 1.7 (336212)

**For Intel&reg; Communications Chipset 89XX Series:**
Intel&reg; Communications Chipset 89xx Series Software for Linux\* - Getting
Started Guide (330750)

These instructions can be found on the 01.org website in the following section:

[Intel&reg; Quickassist Technology][3]

[3]:https://01.org/packet-processing/intel%C2%AE-quickassist-technology-drivers-and-patches

### Install QATzip As Root User

**Set below environment variable**

`ICP_ROOT`: the root directory of your QAT driver source tree

`QZ_ROOT`: the root directory of your QATzip source tree

**Enable huge page**

```bash
    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1024 max_huge_pages_per_process=16
```

**Compile and install QATzip**

```bash
    cd $QZ_ROOT
    ./configure --with-ICP_ROOT=$ICP_ROOT
    make clean
    make all install
```

For more configure options, please run "./configure -h" for help

**Update configuration files**

The Intel&reg; QATzip comes with some example conf files to use with the Intel&reg; QAT Driver.
The Intel&reg; QATzip will not function with the default Intel&reg; QAT Driver conf file because
the default conf does not contain a [SHIM] section which the Intel&reg; QATzip requires by default.
The default section name in the QATzip can be modified if required by setting the environment
variable "QAT_SECTION_NAME".

To update the configuration file, copy the configure file(s) from directory of
`$QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf`
to directory of `/etc`

`YOUR_PLATFORM`: the QAT hardware platform, c6xx for Intel&reg; C62X Series
Chipset, dh895xcc for Intel&reg; Communications Chipset 8925 to 8955 Series

`CONFIG_TYPE`: tuned configure file(s) for different usage,
`multiple_process_opt` for multiple process optimization,
`multiple_thread_opt` for multiple thread optimization

**Restart QAT driver**

```bash
    service qat_service restart
```

With current configuration, each PCI-e device in C6XX platform could support
32 processes in maximum.

**Enable QATzip For Non-root user**

Execute the following script as root user to modify the file properties.

```bash
    cd $QZ_ROOT
    ./setenv.sh
```

### Install QATzip As Non-root User

**Enable huge page as root user**

```bash
    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1024 max_huge_pages_per_process=16
```

**Execute the following script as root user to modify the file properties**

```bash
    cd $QZ_ROOT
    ./setenv.sh
```

**Update the configuration files as root user**

To update the configuration file, copy the configure file(s) from directory of
`$QZ_ROOT/config_file/$YOUR_PLATFORM/$CONFIG_TYPE/*.conf`
to directory of `/etc`

`YOUR_PLATFORM`: the QAT hardware platform, c6xx for Intel&reg; C62X Series
Chipset, dh895xcc for Intel&reg; Communications Chipset 8925 to 8955 Series

`CONFIG_TYPE`: tuned configure file(s) for different usage,
`multiple_process_opt` for multiple process optimization,
`multiple_thread_opt` for multiple thread optimization

**Restart the QAT driver as root user**

```bash
    service qat_service restart
```

With current configuration, each PCI-e device in C6XX platform could support
32 processes in maximum

**Set below environment variable as non-root user**

`ICP_ROOT`: the root directory of your QAT driver source tree

`QZ_ROOT`: the root directory of your QATzip source tree

**Compile and install QATzip as non-root user**

```bash
    cd $QZ_ROOT
    ./configure --with-ICP_ROOT=$ICP_ROOT
    make clean
    make all
    export LD_LIBRARY_PATH=$QZ_ROOT/utils:$LD_LIBRARY_PATH
```

For more configure options, please run "./configure -h" for help

### Test QATzip

Run the following command to check if the QATzip is setup correctly for
compressing or decompressing files:

```bash
    qzip -k $your_input_file (add -h for help)
    or
    cat $your_input_file | qzip > $yout_output_file

    This compression and decompression util could support below options:
    "  -A, --algorithm   set algorithm type, currently only support deflate",
    "  -d, --decompress  decompress",
    "  -f, --force       force overwrite of output file and compress links",
    "  -h, --help        give this help",
    "  -H, --huffmanhdr  set huffman header type",
    "  -k, --keep        keep (don't delete) input files",
    "  -V, --version     display version number",
    "  -L, --level       set compression level",
    "  -C, --chunksz     set chunk size",
    "  -O, --output      set output header format(gzip|gzipext|7z)",
    "  -r,               set max inflight request number",
    "  -R,               set Recursive mode for decompressing a directory
                         It only supports for gzip/gzipext format and
                         decompression operation",
    "  -o,               set output file name",
    "  -P, --polling     set polling mode, only supports busy polling settings"
```

#### File compession in 7z:
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
