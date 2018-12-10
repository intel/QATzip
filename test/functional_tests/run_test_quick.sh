#***************************************************************************
#
#   BSD LICENSE
#
#   Copyright(c) 2007-2018 Intel Corporation. All rights reserved.
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

readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_main="${BASEDIR}/../test "
test_bt="${BASEDIR}/../bt "
test_file_path="/opt/compressdata"
sample_file_name="calgary"
big_file_name="calgary.2G"
huge_file_name="calgary.4G"
highly_compressible_file_name="big-index.html"
CnVnR_file_name="payload6"
residue_issue_file="compressed_1071_src_24957.gz"

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
rm -f ${test_file}.gz ${test_file}

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
rm -f ${test_file}.gz ${test_file}

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
rm -f ${test_file}.gz ${test_file}

function testOn3MBRandomDataFile()
{
    dd if=/dev/urandom of=random-3m.txt bs=3M count=1;
    $test_main -m 4 -t 3 -l 8 -i random-3m.txt;
    rc=`echo $?`;
    rm -f random-3m.txt;
    return $rc;
}

function inputFileTest()
{
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

    return $rc
}

function inputFileTest_pipe_redirection()
{
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

    return $rc
}

function decompressFileTest()
{
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

    return $rc
}

function qzipCompatibleTest()
{
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

    return $rc
}

#Compress with streaming API and valide with gunzip
function streamingCompressFileTest()
{
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

    return $rc
}

#Decompress with streaming API and valide with gunzip
function streamingDecompressFileTest()
{
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

    return $rc
}

#insufficent huge page memory, switch to sw
function switch_to_sw_failover_in_insufficent_HP()
{
    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    echo 8 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

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
    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    dd if=/dev/urandom of=random-3m.txt bs=100M count=1;

    # 9 huge pages needed by each process in mode 4
    echo 12 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    echo -e "\n\nInsufficent huge page for 2 processes"
    cat /proc/meminfo | grep "HugePages_Total\|HugePages_Free"

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
    cat /proc/meminfo | grep "HugePages_Total\|HugePages_Free"
    echo; echo

    for proc in `seq 1 2`; do
      $test_qzip random-3m.txt -k &
    done

    wait
    rm -f random-3m*
    return $?
}


# 2. Very basic misc functional tests

if $test_main -m 1 -t 3 -l 8 && \
    # ignore test2 output (too verbose) \
   $test_main -m 2 -t 3 -l 8 > /dev/null && \
   $test_main -m 3 -t 3 -l 8 && \
   $test_main -m 4 -t 3 -l 8 && \
   $test_main -m 4 -t 3 -l 8 -r 32&& \
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
   $test_main -m 13
   $test_main -m 14
   $test_main -m 15
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

exit 0
