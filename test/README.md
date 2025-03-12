# Intel&reg; QuickAssist Technology (QAT) qatzip-test app

## Table of Contents

- [Introduction](#introduction)
- [Input Parameters](#Input-Parameters)
- [Limitations](#limitations)
- [qatzip-test command](#qatzip-test-command)

## Introduction

qatzip-test is a utility to verify qatzip library functions and collect performance data.
And you can use it to verify the peak performance of QAT devices also. It has a rich set of input
parameters that can help you simulate QAT usage in various situations. And you can also use it as
a demo of how to use the qatzip library API.

## Licensing

The Licensing of the files within this project is split as follows:

Intel&reg; Quickassist Technology (QAT) QATzip - BSD License. Please see the `LICENSE`
file contained in the top level folder. Further details can be found in the file headers
of the relevant files.

Example Intel&reg; Quickassist Technology Driver Configuration Files contained within the
folder hierarchy `config_file` - Dual BSD/GPLv2 License.
Please see the file headers of the configuration files, and the full GPLv2 license
contained in the file `LICENSE.GPL` within the `config_file` folder.

## Input Parameters
                                                                   
Required options:
  
- -m testMode  
                        
      2  test hugepage alloc Memory                                   
      4  test comp/decomp by configurable parameters
      5  test comp/decomp by format parameters      
      6  test session setup process according to configurable parameters
      7  test decompress sw failover
      8  test compression/decompression mixed sw failover
      9  test stream compression
      10 test stream compression based on common memory
      11 test stream compression with multi-block
      12 test stream decompression with multi-block
      13 test negative case, stream compression with invalid chuck size
      14 test negative case, stream compression with invalid params size
      15 test negative case, stream decompression with invalid params size
      16 test negative case, end of stream with invalid params size
      17 test negative case, Mixed decompression sw failover
      18 test comp/decomp by configurable parameters with thread safe flag
      20 test stream with pending out
      21 test hugepage alloc Memory in fork process
      22 test negative case, stream decompression with buffer error
      23 test compression performance with LSM enable
      24 test decompression performance with LSM enable
      25 test Heterogeneous offload performance with LSM enable
      26 test comp/decomp by configurable parameters with extra setup session
      27 test end of stream flag detection
      28 test Async comp/decomp by configurable parameters
      29 test Async comp/decomp performance by configurable parameters
      30 test negative case, decompression with invalid end of stream
      31 test decompression with valid end of stream during multi-stream

Optional options can be:                                                                                                                     
- ``` -i inputfile```  
  - Input test file, default by generating random data and default test size is 512KB.
- ``` -C hw_buff_sz``` 
  - HW buffer size, default is 64K.
- ``` -b block_size``` 
  - Input src buffer size at API level, It must be the power of 2. The minimum is 4k, and maximum is 1M. Default is input file size.
- ``` -t thread_count``` 
  - Maximum fork thread permitted in the current test, 0 means no forking permitted.
- ``` -l loop_count``` 
  - The loop for same test condition, default is 2.
- ``` -L comp_lvl``` 
  - compression level. default is 1.
- ``` -A comp_algorithm``` 
  - deflate | lz4 | lz4s.
- ``` -T huffmanType``` 
  - static | dynamic, set huffmanType to deflate algorithm.
- ``` -D direction``` 
  - Test compression/decompression direction, comp | decomp | both, default is comp.
- ``` -O data_fmt``` 
  - deflate | gzip | gzipext | deflate_4B | lz4 | lz4s | zlib. For zlib and deflate raw the block size must be same as HW buffer size, e.g '-b' and '-C' should be same.
- ``` -B swBack``` 
  - enable | disable SW failover, enabled by default.
- ``` -e init engine``` 
  - enable | disable HW engine. enabled by default.
- ``` -s init session``` 
  - enable | disable session setup. enabled by default.
- ``` -r req_cnt_thrshold``` 
  - max in-flight request num, default is 16.
- ``` -M svm ``` 
  - set perf mode with file input, default is non svm mode. When set to svm, all memory will be allocated with malloc instead of qzMalloc, This option is only applied to mode 4.
- ``` -p compress_buf_type```  
  - pinned | common, default is common,This option is only applied to file compression test in mode 4, If set common, memory of compress buffer will be allocated through malloc, If set pinned, memory of compress buffer will be allocated in huge page, allocation limit is 2M\n" 
- ``` -P polling``` 
  - set polling mode, default is periodical polling, when set busy polling mode, it would automatically enable the LSM(latency sensitive mode)
- ``` -g loglevel``` 
  - set qatzip loglevel(none|error|warn|info|debug)
- ``` -q async_queue_sz``` 
  - set async queue size, default is 1024
- ``` -v ``` 
  - verify compression/decompression result, disabled by default.
- ``` -a ``` 
  - Enable Latency sensitive mode.
- ``` -h ``` 
  - Print this help message

## Limitations

* If "-t" thread number is larger than driver dc instances, have to enable SW failover, like "-B 1" 
* For LSM test(mode 23-25), it require the maximum dc instance configure to build heavy pressure situation
  for QAT device, please setup 64 dc instances in driver config, and use the same number of threads.
  because LSM would consume the cpu resource, please make sure cpu usage is not in pressure when you test.
* For Async test(mode 28-29), if you want to test very smaller input file or you set the very smaller block
  size like "-b 4096", It maybe cause the async queue full and failed, then you have to increase the queue
  size by setting "-q".
* For negative case, it may dump the error log from the qatzip library, those logs are expected, but if you
  don't want to dump those log, please set "-g none".
* For some code format like: deflate raw or zlib, if you want to test decompression of them, you have to enable
  SW failover, like "-B 1", otherwise, you have to make sure the block size is smaller or equal to HW buffer
  size, like "-b 4096 -C 4096".

## qatzip-test command

Run the following command as the example to verify qatzip library functions and collect performance data,
Test mode 4 is major test mode for different format and algorithm verify.

### Test mode 4:
```bash
    qatzip-test -m 4 -t 8 -l 100 -i inputfile -C 65536 -b 524288 -L 1 -A deflate -O gzipext -T dynamic
    qatzip-test -m 4 -t 10 -l 100 -l 1 -A lz4 -O lz4
```
### Test mode 23:
```bash
    qatzip-test -m 23 -l 1000 -t 64 -i calgary -b 65536 -e enable -B 1 -a
    qatzip-test -m 23 -l 1000 -t 64 -i calgary -b 65536 -e enable -B 0 -a
    qatzip-test -m 23 -l 1000 -t 64 -i calgary -b 65536 -e disable -B 0 -a
```
### Test mode 29:
```bash
    qatzip-test -m 4 -l 2000 -t 8 -B 0 -D decomp -L 1 -i calgary -T dynamic -C 4096 -b 4096
    qatzip-test -m 4 -l 2000 -t 8 -D comp -L 1 -i calgary -A lz4 -O lz4 -q 2048
```
