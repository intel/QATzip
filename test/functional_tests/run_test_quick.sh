#***************************************************************************
#
#   BSD LICENSE
#
#   Copyright(c) 2007-2020 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#**************************************************************************

#! /bin/bash
set -e
echo "***QZ_ROOT run_test_quick.sh start"
readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_main="${BASEDIR}/../test "
DRV_FILE=${QZ_TOOL}/install_drv/install_upstream.sh
test_bt="${BASEDIR}/../bt "
test_file_path="/opt/compressdata"
sample_file_name="calgary"
big_file_name="calgary.2G"
huge_file_name="calgary.4G"
highly_compressible_file_name="big-index.html"
CnVnR_file_name="payload6"
residue_issue_file="compressed_1071_src_24957.gz"
qzCompressStream_pendingout_test_file="64kb_file.html"
large_file_name="calgary.13G"

# 1. Trivial file compression
echo "Performing file compression and decompression..."
test_file=test.tmp
out_file=test_out
test_str="THIS IS TEST STRING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
format_option="qz:$((32*1024))/qz:$((64*1024))/qz:$((128*1024))/qz:$((64*1024))"
format_option+="/qz:$((32*1024))/qz:$((256*1024))"
echo "format_option=$format_option"


# 1.1 QAT Compression and Decompress
echo $test_str > $test_file
$test_qzip $test_file -o $out_file         #compress
$test_qzip $out_file.gz -d -o $test_file   #decompress

if [ "$test_str" == "$(cat $test_file)" ]; then
    echo "QAT file compression and decompression OK :)"
else
    echo "QAT file compression and decompression FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${test_file}

# 1.2 SW Compression and Decompress
if [ -d  $ICP_ROOT/CRB_modules ];
then
  DRIVER_DIR=$ICP_ROOT/CRB_modules;
else
  DRIVER_DIR=$ICP_ROOT/build;
fi
$DRIVER_DIR/adf_ctl down

echo $test_str > $test_file
$test_qzip $test_file 1>/dev/null 2>/dev/null          #compress
$test_qzip ${test_file}.gz -d  1>/dev/null 2>/dev/null #decompress

if [ "$test_str" == "$(cat $test_file)" ]; then
    echo "SW file compression and decompression OK :)"
else
    echo "SW file compression and decompression FAILED!!! :("
    exit 1
fi

$DRIVER_DIR/adf_ctl up

#clear test file
rm -f ${test_file}

# 1.3 check sw  compatibility with extra flag
head -c $((4*1024*1024)) /dev/urandom | od -x > $test_file
gzip $test_file
$test_qzip $test_file.gz -d
[[ $? -ne 0 ]] && { echo "QAT file compression FAILED !!!"; exit 1; }

#clear test file
rm -f ${test_file}.gz ${test_file}

#1.4 size 0 file compress/decompress test
echo "" > $test_file
$test_qzip $test_file -o $out_file         #compress
$test_qzip $out_file.gz -d -o $test_file   #decompress

if [ "" == "$(cat $test_file)" ]; then
    echo "size 0 file compress/decompress test OK :)"
else
    echo "size 0 file compress/decompress test FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${test_file}

# 1.5 QAT pipe & redirection Compress and Decompress
echo $test_str > $test_file
cat $test_file | $test_qzip > $out_file.gz         #compress
cat $out_file.gz | $test_qzip -d > $test_file         #decompress

if [ "$test_str" == "$(cat $test_file)" ]; then
    echo "QAT pipe & redirection Compress and Decompress OK :)"
else
    echo "QAT pipe & redirection Compress and Decompress FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${out_file}.gz ${test_file}

# 1.6 QAT SW pipe & redirection Compression and Decompress
if [ -d  $ICP_ROOT/CRB_modules ];
then
  DRIVER_DIR=$ICP_ROOT/CRB_modules;
else
  DRIVER_DIR=$ICP_ROOT/build;
fi
$DRIVER_DIR/adf_ctl down

echo $test_str > $test_file
cat $test_file | $test_qzip > $out_file.gz 2>/dev/null
cat $out_file.gz | $test_qzip -d > $test_file 2>/dev/null

if [ "$test_str" == "$(cat $test_file)" ]; then
    echo "QAT SW pipe & redirection Compression and Decompress OK :)"
else
    echo "QAT SW pipe & redirection Compression and Decompress FAILED!!! :("
    exit 1
fi

$DRIVER_DIR/adf_ctl up

#clear test file
rm -f ${out_file}.gz ${test_file}

# 1.7 Check QAT SW pipe & redirection compatibility with extra flag
head -c $((4*1024*1024)) /dev/urandom | od -x > $test_file
orig_checksum=`md5sum $test_file`
cat $test_file | gzip > $test_file.gz
cat $test_file.gz | $test_qzip -d > $test_file
[[ $? -ne 0 ]] && { echo "Check QAT SW pipe & redirection compatibility with extra flag FAILED !!! :("; exit 1; }
new_checksum=`md5sum $test_file`
if [[ $new_checksum == $orig_checksum ]]; then
    echo "Check QAT SW pipe & redirection compatibility with extra flag OK :)"
else
    echo "Check QAT SW pipe & redirection compatibility with extra flag FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${test_file}.gz ${test_file}

# 1.8 Check QAT SW pipe & redirection compatibility with extra flag reversal
echo $test_str > $test_file
head -c $((4*1024*1024)) /dev/urandom | od -x > $test_file
orig_checksum=`md5sum $test_file`
cat $test_file | $test_qzip > $test_file.gz
cat $test_file.gz | gzip -d > $test_file
[[ $? -ne 0 ]] && { echo "Check QAT SW pipe & redirection compatibility with extra flag reversal FAILED !!! :("; exit 1; }
new_checksum=`md5sum $test_file`
if [[ $new_checksum == $orig_checksum ]]; then
    echo "Check QAT SW pipe & redirection compatibility with extra flag reversal OK :)"
else
    echo "Check QAT SW pipe & redirection compatibility with extra flag reversal FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${test_file}.gz ${test_file}

#1.9 QAT pipe & redirection size 0 file compress/decompress test
echo "" > $test_file

cat $test_file | $test_qzip > $out_file.gz         #compress
cat $out_file.gz | $test_qzip -d > $test_file      #decompress

if [ "" == "$(cat $test_file)" ]; then
    echo "QAT pipe & redirection size 0 file compress/decompress test OK :)"
else
    echo "QAT pipe & redirection size 0 file compress/decompress test FAILED!!! :("
    exit 1
fi

#clear test file
rm -f ${out_file}.gz ${test_file}

function testOn3MBRandomDataFile()
{
    echo "testOn3MBRandomDataFile"
    dd if=/dev/urandom of=random-3m.txt bs=3M count=1;
    $test_main -m 4 -t 3 -l 8 -i random-3m.txt;
    rc=`echo $?`;
    rm -f random-3m.txt;
    return $rc;
}

function inputFileTest()
{
    echo "inputFileTest"
    local test_file_name=$1
    local comp_level=$2
    local req_cnt_thrshold=$3
    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    if [ -z $comp_level ]
    then
            comp_level=1
    fi

    if [ -z $req_cnt_thrshold ]
    then
            req_cnt_thrshold=16
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`
    if $test_qzip -L $comp_level -r $req_cnt_thrshold $test_file_name && \
        $test_qzip -r $req_cnt_thrshold -d "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, input file test failed."
        rc=1
    fi
    rm -f $test_file_name
    return $rc
}

function inputFileTest_pipe_redirection()
{
    echo "inputFileTest_pipe_redirection"
    local test_file_name=$1
    local comp_level=$2
    local req_cnt_thrshold=$3
    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    if [ -z $comp_level ]
    then
            comp_level=1
    fi

    if [ -z $req_cnt_thrshold ]
    then
            req_cnt_thrshold=16
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`
    if cat $test_file_name | $test_qzip -L $comp_level -r $req_cnt_thrshold > $test_file_name.gz && cat $test_file_name.gz | $test_qzip -d -r $req_cnt_thrshold > $test_file_name
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, input file test failed."
        rc=1
    fi
    rm -f $test_file_name $test_file_name.gz
    return $rc
}

function decompressFileTest()
{
    echo "decompressFileTest"
    local test_file_name=$1
    local output_file_name=`echo $test_file_name | cut -d '.' -f 1`

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    cp -f $test_file_path/$test_file_name ./
    if $test_qzip -k -d $test_file_name
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
    fi
    mv $output_file_name $output_file_name.qzip

    if gzip -d "$test_file_name"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
    fi
    mv $output_file_name $output_file_name.gzip
    qzip_checksum=`md5sum $output_file_name.qzip | cut -d ' ' -f 1`
    gzip_checksum=`md5sum $output_file_name.gzip | cut -d ' ' -f 1`

    if [[ $qzip_checksum != $gzip_checksum ]]
    then
        echo "Checksum mismatch, decompress file test failed."
        rc=1
    fi
    rm -f $output_file_name.gzip $output_file_name.qzip
    return $rc
}

function qzipCompatibleTest()
{
    echo "qzipCompatibleTest"
    local test_file_name=$1
    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    rm -f ./$test_file_name ./$test_file_name.gz

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`
    if gzip $test_file_name && $test_qzip -d "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, qzip compatible test failed."
        rc=1
    fi
    rm -f $test_file_name
    return $rc
}

#Compress with streaming API and valide with gunzip
function streamingCompressFileTest()
{
    echo "streamingCompressFileTest"
    local test_file_name=$1
    local fmt_list="gzipext"
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    for fmt_opt in $fmt_list
    do
        rm -f $test_file_name $test_file_name.gz

        cp -f $test_file_path/$test_file_name ./
        orig_checksum=`md5sum $test_file_name | awk '{print $1}'`
        if $test_main -m 11 -O $fmt_opt -i $test_file_name && \
            gzip -df "$test_file_name.gz"
        then
            echo "(De)Compress $test_file_name OK";
            rc=0
        else
            echo "(De)Compress $test_file_name Failed";
            rc=1
        fi
        new_checksum=`md5sum $test_file_name | awk '{print $1}'`

        if [[ $new_checksum != $orig_checksum ]]
        then
            echo "Checksum mismatch, streaming compress file test failed."
            rc=1
        fi

        if [ $rc -eq 1 ]
        then
            break 1;
        fi
    done
    rm -f $test_file_name
    return $rc
}

#Decompress with streaming API and valide with gunzip
function streamingDecompressFileTest()
{
    echo "streamingDecompressFileTest"
    local test_file_name=$1
    local rc=0
    local suffix="decomp"

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    rm -f $test_file_name $test_file_name.gz $test_file_name.gz.$suffix

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name | awk '{print $1}'`
    gzip "$test_file_name"
    if $test_main -m 12 -O gzip -i $test_file_name.gz
    then
        echo "Decompress $test_file_name.gz OK";
        rc=0
    else
        echo "Decompress $test_file_name.gz Failed";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name.gz.$suffix | awk '{print $1}'`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, streaming decompress file test failed."
        rc=1
    fi

    if [ $rc -eq 1 ]
    then
        break 1;
    fi
    rm -f $test_file_name.gz $test_file_name.gz.$suffix
    return $rc
}

#insufficent huge page memory, switch to sw
function switch_to_sw_failover_in_insufficent_HP()
{
    echo "switch_to_sw_failover_in_insufficent_HP"
    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    echo 8 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

    sleep 5

    for service in 'comp' 'decomp'
    do
       if $test_main -m 4 -D $service ; then
           echo "test qatzip $service with insufficent huge page memory PASSED"
       else
           echo "test qatzip $service with insufficent huge page memory FAILED"
           break
       fi
    done

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    return $?
}

#get available huge page memory while some processes has already swithed to the sw
function resume_hw_comp_when_insufficent_HP()
{
    echo "resume_hw_comp_when_insufficent_HP"
    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    dd if=/dev/urandom of=random-3m.txt bs=100M count=1;

    # 9 huge pages needed by each process in mode 4
    echo 12 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    echo -e "\n\nInsufficent huge page for 2 processes"
    cat /proc/meminfo | grep "HugePages_Total\|HugePages_Free"

    sleep 5

    for proc in `seq 1 2`; do
        $test_qzip random-3m.txt -k &
    done

    sleep 1
    #show current huge page
    echo -e "\n\nCurrent free huge page"
    cat /proc/meminfo | grep "HugePages_Total\|HugePages_Free"

    #re-allocate huge page
    echo -e "\n\nResume huge age"
    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    sleep 5

    cat /proc/meminfo | grep "HugePages_Total\|HugePages_Free"
    echo; echo

    for proc in `seq 1 2`; do
      $test_qzip random-3m.txt -k &
    done

    wait
    rm -f random-3m*
    return $?
}

#block device compress test
function blockDeviceTest()
{
    echo "blockDeviceTest"
    local block_device_test_file="block_device_file"
    local rc=0

    dd if=/dev/zero of=$block_device_test_file bs=1M count=100
    [[ $? -ne 0 ]] && { echo "dd error, block device compress test failed"; return 1; }
    loop_dev=`losetup --find --show $block_device_test_file`
    [[ $? -ne 0 ]] && { echo "device not created, block device compress test failed"; return 1; }
    orig_checksum=`md5sum $block_device_test_file`
    $test_qzip -k $loop_dev -o $block_device_test_file
    gzip -fd $block_device_test_file.gz
    new_checksum=`md5sum $block_device_test_file`
    losetup -d $loop_dev
    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, block device compress test failed"
        rc=1
    fi

    rm -f ${block_device_test_file}.gz ${block_device_test_file}
    return $rc
}

# 2. Very basic misc functional tests

if $test_main -m 1 -t 3 -l 8 && \
   $test_main -m 1 -O gzip -t 3 -l 8 && \
    # ignore test2 output (too verbose) \
   $test_main -m 2 -t 3 -l 8 > /dev/null && \
   $test_main -m 3 -t 3 -l 8 && \
   $test_main -m 3 -O gzip -t 3 -l 8 && \
   $test_main -m 4 -t 3 -l 8 && \
   $test_main -m 4 -t 3 -l 8 -r 32&& \
   blockDeviceTest && \
   testOn3MBRandomDataFile && \
   inputFileTest $highly_compressible_file_name &&\
   inputFileTest $big_file_name &&\
   inputFileTest $big_file_name 1 32&&\
   inputFileTest $CnVnR_file_name 4 &&\
   inputFileTest $huge_file_name &&\
   inputFileTest_pipe_redirection $highly_compressible_file_name &&\
   inputFileTest_pipe_redirection $big_file_name &&\
   inputFileTest_pipe_redirection $big_file_name 1 32&&\
   inputFileTest_pipe_redirection $CnVnR_file_name 4 &&\
   inputFileTest_pipe_redirection $huge_file_name &&\
   decompressFileTest $residue_issue_file &&\
   qzipCompatibleTest $big_file_name &&\
   switch_to_sw_failover_in_insufficent_HP && \
   resume_hw_comp_when_insufficent_HP && \
   $test_main -m 5 -t 3 -l 8 -F $format_option && \
   $test_main -m 6 && \
   $test_main -m 7 -t 1 -l 8 && \
   $test_main -m 8 && \
   $test_main -m 9 -O deflate && \
   $test_main -m 9 -O gzipext && \
   $test_main -m 10 -l 1000  -t 1 && \
   streamingCompressFileTest $sample_file_name && \
   streamingDecompressFileTest $sample_file_name && \
   $test_main -m 13 && \
   $test_main -m 14 && \
   $test_main -m 15 && \
   $test_main -m 16 && \
   $test_main -m 17
then
    echo "Functional tests OK"
else
    echo "Functional tests FAILED!!! :(";
    exit 2
fi

# 3. Basic tests

echo "Performing basic tests..."
if $test_bt -c 0 -S 200000 && \
   $test_bt -c 1 -f -S 200000 && \
   $test_bt -c 1 -S 200000 && \
   $test_bt -c 2 -S 200000
then
    echo "Basic tests OK"
else
    echo "Basic tests FAILED!!! :(";
    exit 2
fi

#test for chunksz
echo "test for chunksz"
if ! ${BASEDIR}/run_test_chunksz.sh
then
    exit 2
fi

#Mim allocated memory test
echo "Mim allocated memory test"
function minAllocatedMemoryTest1()
{
    echo "minAllocatedMemoryTest1"
    local test_file_name=$1
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    echo 1 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1 max_huge_pages_per_process=1

    sleep 5

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    if $test_qzip $test_file_name > minAllocatedMemoryTestlog 2>&1 && \
        gzip -d "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name FAILED";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, mim allocated memory test1 FAILED."
        rc=1
    fi

    error_Key=$(grep "QZ_NO_HW" minAllocatedMemoryTestlog)
    if [[ -z $error_Key ]]
    then
        echo "Check error_Key is null, mim allocated memory test1 FAILED."
        rc=1
    fi
    rm -f minAllocatedMemoryTestlog $test_file_name
    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}

function minAllocatedMemoryTest2()
{
    echo "minAllocatedMemoryTest2"
    local test_file_name=$1
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1024 max_huge_pages_per_process=1

    sleep 5

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    if $test_qzip $test_file_name > minAllocatedMemoryTestlog 2>&1 && \
        gzip -d "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name FAILED";
        rc=1
    fi
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, mim allocated memory test2 FAILED."
        rc=1
    fi

    error_Key=$(grep "QZ_NO_HW" minAllocatedMemoryTestlog)
    if [[ -z $error_Key ]]
    then
        echo "Check error_Key is null, mim allocated memory test2 FAILED."
        rc=1
    fi
    rm -f minAllocatedMemoryTestlog $test_file_name
    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}

if minAllocatedMemoryTest1 $sample_file_name && \
   minAllocatedMemoryTest2 $sample_file_name
then
    echo "Mim allocated memory test OK"
else
    echo "Mim allocated memory test FAILED!!! :(";
    exit 2
fi

#test for hw down sw up and down
if ! ${BASEDIR}/run_test_sw.sh
then
    exit 2
fi

#Early HW detection test
echo "Early HW detection test"
if ! ${BASEDIR}/run_test_early_HW_detection.sh
then
    exit 1
fi

function none_QAT_stream_compress_decompress_test()
{
    echo "none_QAT_stream_compress_decompress_test"
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    rm -f $test_file_name $test_file_name.gz $test_file_name.gz.decomp

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`
    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    $ICP_BUILD_OUTPUT/adf_ctl down

   if $test_main -m 11 -i $test_file_name && \
        $test_main -m 12 -i "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
        return $rc
    fi

    new_checksum=`md5sum $test_file_name.gz.decomp`

    echo 'new_checksum '$new_checksum
    echo 'orig_checksum '$orig_checksum

    if [[ ${new_checksum%% *} != ${orig_checksum%% *} ]]
    then
        echo "Checksum mismatch, stream compress decompress test with adf_ctl down FAILED."
        rc=1
        return $rc
    else
        echo "Checksum match, stream compress decompress test with adf_ctl down PASSED."
        rc=0
    fi

    $ICP_BUILD_OUTPUT/adf_ctl up

    rm -f $test_file_name $test_file_name.gz $test_file_name.gz.decomp

    cp -f $test_file_path/$test_file_name ./

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv

    sleep 5

    if $test_main -m 11 -i $test_file_name && \
        $test_main -m 12 -i "$test_file_name.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name Failed";
        rc=1
        return $rc
    fi

    new_checksum=`md5sum $test_file_name.gz.decomp`

    echo 'new_checksum '$new_checksum
    echo 'orig_checksum '$orig_checksum

    if [[ ${new_checksum%% *} != ${orig_checksum%% *} ]]
    then
        echo "Checksum mismatch, stream compress decompress test without usdm_drv FAILED."
        rc=1
        return $rc
    else
        echo "Checksum match, stream compress decompress test without usdm_drv PASSED."
        rc=0
    fi

    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256
    rm -f $test_file_name $test_file_name.gz $test_file_name.gz.decomp
    return $rc
}

if none_QAT_stream_compress_decompress_test $sample_file_name
then
    echo "None QAT stream compress decompress test OK"
else
    echo "None QAT stream compress decompress test FAILED!!! :("
    exit 2
fi

#test for calling qzInit() again after a qzClose(), in "no HW" environment (pcie_count == 0)
if ! ${BASEDIR}/run_test_qzInit.sh
then
    exit 2
fi

#test reading special-name files
if ! ${BASEDIR}/run_test_readfiles.sh
then
    exit 1
fi

function configuration_file_test()
{
    echo "QAT driver configuration file configurable section name test 1 start ..."
    $QZ_TOOL/get_platform/get_platforminfo.sh
    platform=`cat $QZ_TOOL/get_platform/PlatformInfo`
    echo "platform=$platform"
    numberOfCPM=`lspci | grep Co-processor | wc -l`

    dd if=/dev/urandom of=random.tmp bs=1M count=1;
    export QATZIP_SECTION_NAME="CFTEST"

    #Change to CFTEST
    for ((i = 0; i < $numberOfCPM; i++)); do
        if [[ $platform = "37c8" || $platform = "C62x" ]]; then
            sed -i 's/^\[SHIM\]$/\[CFTEST\]/g' /etc/c6xx_dev$i.conf
        elif [ $platform == "DH895XCC" ]; then
            sed -i 's/^\[SHIM\]$/\[CFTEST\]/g' /etc/dh895xcc_dev$i.conf
        elif [ $platform == "C3000" ]; then
            sed -i 's/^\[SHIM\]$/\[CFTEST\]/g' /etc/c3xxx_dev$i.conf
        fi
    done
    service qat_service restart

    OLDMD5=`md5sum random.tmp`
    echo "old md5" $OLDMD5

    $test_qzip -k random.tmp > result.log 2>&1
    ERROR_MSG=`grep -rn userStarMultiProcess result.log`
    echo $ERROR_MSG
    if [ -z "$ERROR_MSG" ]; then
        echo "QAT section name is configurable :)"
    else
        echo "QAT section name is not configurable :("
        RESULT=1
    fi

    rm -f random.tmp

    gzip -df random.tmp.gz

    NEWMD5=`md5sum random.tmp`
    echo "new md5 " $NEWMD5

    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        echo "Checksum mismatch FAILED"
        RESULT=1
    else
        echo "Checksum match PASSED"
    fi

    #Change back to SHIM
    export QATZIP_SECTION_NAME="SHIM"
    for ((i = 0; i < $numberOfCPM; i++)); do
        if [[ $platform = "37c8" || $platform = "C62x" ]]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/c6xx_dev$i.conf
        elif [ $platform == "DH895XCC" ]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/dh895xcc_dev$i.conf
        elif [ $platform == "C3000" ]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/c3xxx_dev$i.conf
        fi
    done
    service qat_service restart
    rm -f random.tmp
    rm -f random.tmp.gz
    rm -f result.log
    if [ ! -z $RESULT ];then
        return 1
    fi
    return 0
}

function configuration_file_test_software_compress()
{
    echo "QAT driver configuration file configurable section name test 2 start ..."
    $QZ_TOOL/get_platform/get_platforminfo.sh
    platform=`cat $QZ_TOOL/get_platform/PlatformInfo`
    echo "platform=$platform"
    numberOfCPM=`lspci | grep Co-processor | wc -l`

    dd if=/dev/urandom of=random.tmp bs=1M count=1;
    export QATZIP_SECTION_NAME="CFTEST"

    OLDMD5=`md5sum random.tmp`
    echo "old md5" $OLDMD5

    $test_qzip -k random.tmp > result.log 2>&1
    ERROR_MSG=`grep -rn userStarMultiProcess result.log`
    echo $ERROR_MSG
    if [ -z "$ERROR_MSG" ]; then
        echo "QATZIP still work in HW mode :("
        RESULT=1
    else
        echo "QATZIP can switch to SW mode :)"
    fi

    rm -f random.tmp

    gzip -df random.tmp.gz

    NEWMD5=`md5sum random.tmp`
    echo "new md5 " $NEWMD5

    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        echo "Checksum mismatch FAILED"
        RESULT=1
    else
        echo "Checksum match PASSED"
    fi

    #Change back to SHIM
    export QATZIP_SECTION_NAME="SHIM"
    for ((i = 0; i < $numberOfCPM; i++)); do
        if [[ $platform = "37c8" || $platform = "C62x" ]]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/c6xx_dev$i.conf
        elif [ $platform == "DH895XCC" ]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/dh895xcc_dev$i.conf
        elif [ $platform == "C3000" ]; then
            sed -i 's/^\[CFTEST\]$/\[SHIM\]/g' /etc/c3xxx_dev$i.conf
        fi
    done
    service qat_service restart
    rm -f random.tmp
    rm -f random.tmp.gz
    rm -f result.log
    if [ ! -z $RESULT ];then
        return 1
    fi
    return 0
}
if configuration_file_test \
   && configuration_file_test_software_compress
then
   echo "QAT driver configuration file confifurable test PASSED"
else
   echo "QAT driver configuration file confifurable test FAILED"
   exit 2
fi

function qzSWCompression_block_test()
{
    echo "qzSWCompression_block_test"
    $DRIVER_DIR/adf_ctl down
    if [ ! -f "$test_file_path/$sample_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi
    cp -f $test_file_path/$sample_file_name ./

    OLDMD5=`md5sum $sample_file_name`
    $test_main -m 11 -i $sample_file_name
    rm -f $sample_file_name
    gzip -d $sample_file_name.gz
    NEWMD5=`md5sum $sample_file_name`
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    OLDMD5=`md5sum $sample_file_name`
    $test_qzip $sample_file_name
    rm -f $sample_file_name
    gzip -d $sample_file_name.gz
    NEWMD5=`md5sum $sample_file_name`
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    $DRIVER_DIR/adf_ctl up
    rm -f $sample_file_name
    rm -f $sample_file_name.gz

    return 0
}
if qzSWCompression_block_test
then
   echo "qzSWCompression block test PASSED"
else
   echo "qzSWCompression block test FAILED"
   exit 2
fi

function qzCompressStream_with_pending_out_test()
{
    echo "qzCompressStream_with_pending_out_test"
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name ./
    OLDMD5=`md5sum $test_file_name`
    $test_main -m 20 -i $test_file_name -D comp
    rm -f $test_file_name
    gzip -d $test_file_name.gz
    NEWMD5=`md5sum $test_file_name`
    rm -f $test_file_name
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}
if qzCompressStream_with_pending_out_test $qzCompressStream_pendingout_test_file
then
   echo "qzCompressStream with pending_out test PASSED"
else
   echo "qzCompressStream with pending_out test FAILED!!!"
   exit 2
fi

function decompress_test_with_large_file()
{
    echo "decompress_test_with_large_file"
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name.gz ./
    OLDMD5=`md5sum $test_file_path/$test_file_name | awk '{print $1}'`
    $test_qzip -d $test_file_name.gz
    NEWMD5=`md5sum $test_file_name | awk '{print $1}'`
    rm -f $test_file_name
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}

if decompress_test_with_large_file $large_file_name
then
   echo "decompress test with large file PASSED"
else
   echo "decompress test with large file FAILED!!!"
exit 2
fi

#test for not exist symlink
echo "test for not exist symlink"
if ! ${BASEDIR}/run_test_symlink.sh
then
    exit 2
fi

#test for fork resource check
echo "test for fork resource check"
if ! ${BASEDIR}/run_test_fork_resource_check.sh
then
    exit 2
fi

#test for qzip compressing with -O options
function qzipCompressTest()
{
    echo "qzipCompressTest"
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name ./

    OLDMD5=`md5sum $test_file_name | awk '{print $1}'`
    $test_qzip -O gzip $test_file_name -o $test_file_name
    gzip -d $test_file_name.gz
    NEWMD5=`md5sum $test_file_name | awk '{print $1}'`
    rm -f $test_file_name*
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}
if qzipCompressTest $big_file_name
then
    echo "qzip compress with -O option PASSED"
else
    echo "qzip compress with -O option FAILED!!!"
    exit 2
fi

$DRIVER_DIR/adf_ctl down
if qzipCompressTest $big_file_name
then
    echo "qzip compress with -O option with hardware down PASSED"
else
    echo "qzip compress with -O option with hardware down FAILED!!!"
    exit 2
fi
$DRIVER_DIR/adf_ctl up

echo "test for decom combined_file"
if ! ${BASEDIR}/run_test_decompress_combined_file.sh
then
    exit 2
fi
echo "***QZ_ROOT run_test_quick.sh end"
exit 0
