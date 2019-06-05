#***************************************************************************
#
#   BSD LICENSE
#
#   Copyright(c) 2007-2019 Intel Corporation. All rights reserved.
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
test_file_path="/opt/compressdata"
sample_file_name="calgary"
huge_file_name="calgary.4G"
small_file_name="calgary.512K"
compressed_with_qzip_file="compressed_1071_src_24957.gz"
decompressed_file="compressed_1071_src_24957"
compressed_with_tar_file="cantrbry.tar"
test_file=test.tmp
out_file=test_out

#Hugepage memory test
echo "Hugepage memory test"
function HugePageTest()
{
    local test_file_name=$1
    local nr_hugepages=$2
    local test_hp=$3
    local test_hpp=$4
    local test_case=$5
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    echo "nr_hugepage=$nr_hugepages max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp"
    echo "Test case:$test_case test file:$test_file_name"

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    echo $nr_hugepages > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name | awk '{print $1}' | head -1`

    if $test_qzip $test_file_name -o $test_file_name-compressed && \
       $test_qzip -d -k "$test_file_name-compressed.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name FAILED";
        rc=1
    fi

    new_checksum=`md5sum $test_file_name-compressed | awk '{print $1}' | head -1`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, hugepage test FAILED."
        rc=1
    fi

    rm $test_file_name-compressed

    if gzip -d "$test_file_name-compressed.gz"
    then
        echo "Decompress $test_file_name-compressed.gz with gzip successfully."
        rc=0
    else
        echo "Decompress $test_file_name-compressed.gz with gzip FAILED."
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    rm $test_file_name-compressed

    return $rc
}

function HugePage_Decompress_Test()
{
    local test_file_name=$1
    local nr_hugepages=$2
    local test_hp=$3
    local test_hpp=$4
    local test_case=$5
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    echo "nr_hugepage=$nr_hugepages max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp"
    echo "Test case:$test_case test file:$test_file_name"

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    echo $nr_hugepages > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp

    cp -f $test_file_path/$test_file_name ./

    if $test_qzip -d $test_file_name -o $decompressed_file
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name FAILED";
        rc=1
    fi

    orig_checksum=`md5sum $decompressed_file | awk '{print $1}' | head -1`

    $test_qzip $decompressed_file
    gzip -d $test_file_name

    new_checksum=`md5sum $decompressed_file | awk '{print $1}' | head -1`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, hugepage test FAILED."
        rc=1
    fi

    rm $decompressed_file

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}

function HugePageTest_with_0_byte_file()
{
    local nr_hugepages=$1
    local test_hp=$2
    local test_hpp=$3
    local test_case=$4
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    echo "nr_hugepage=$nr_hugepages max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp"
    echo "Test case:$test_case"

    echo $nr_hugepages > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$test_hp max_huge_pages_per_process=$test_hpp

    echo "" > $test_file

    $test_qzip $test_file -o $out_file        #compress
    $test_qzip $out_file.gz -d -o $test_file  #decompress

    if [ "" == "$(cat $test_file)" ]; then
        echo "size 0 file compress/decompress test OK :)"
    else
        echo "size 0 file compress/decompress test FAILED!!! :("
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    rm $test_file

    return $rc
}

if HugePageTest $sample_file_name 1024 1024 16 1 && \
   HugePageTest $sample_file_name 1024 1024 0 2 && \
   HugePageTest $sample_file_name 0 1024 0 3 && \
   HugePageTest $sample_file_name 1024 0 16 4 && \
   HugePageTest $sample_file_name 0 0 16 5 && \
   HugePageTest $sample_file_name 1024 0 0 6 && \
   HugePageTest $sample_file_name 0 0 0 7 && \
   #HugePageTest $sample_file_name 0 1024 16 8 && \
   HugePageTest $huge_file_name 1024 1024 16 9 && \
   HugePageTest $huge_file_name 1024 1024 0 10 && \
   HugePageTest $huge_file_name 0 1024 0 11 && \
   HugePageTest $huge_file_name 1024 0 16 12 && \
   HugePageTest $huge_file_name 0 0 16 13 && \
   HugePageTest $huge_file_name 1024 0 0 14 && \
   HugePageTest $huge_file_name 0 0 0 15 && \
   #HugePageTest $huge_file_name 0 1024 16 16 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 1024 1024 16 17 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 1024 1024 0 18 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 0 1024 0 19 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 1024 0 16 20 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 0 0 16 21 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 1024 0 0 22 && \
   HugePage_Decompress_Test $compressed_with_qzip_file 0 0 0 23 && \
   #HugePage_Decompress_Test $compressed_with_qzip_file 0 1024 16 24 && \
   HugePageTest_with_0_byte_file 1024 1024 16 25 && \
   HugePageTest_with_0_byte_file 1024 1024 0 26 && \
   HugePageTest_with_0_byte_file 0 1024 0 27 && \
   HugePageTest_with_0_byte_file 1024 0 16 28 && \
   HugePageTest_with_0_byte_file 0 0 16 29 && \
   HugePageTest_with_0_byte_file 1024 0 0 30 && \
   HugePageTest_with_0_byte_file 0 0 0 31 && \
   #HugePageTest_with_0_byte_file 0 1024 16 32 && \
   HugePageTest $small_file_name 1024 1024 16 33 && \
   HugePageTest $small_file_name 1024 1024 0 34 && \
   HugePageTest $small_file_name 0 1024 0 35 && \
   HugePageTest $small_file_name 1024 0 16 36 && \
   HugePageTest $small_file_name 0 0 16 37 && \
   HugePageTest $small_file_name 1024 0 0 38 && \
   HugePageTest $small_file_name 0 0 0 39 && \
   #HugePageTest $small_file_name 0 1024 16 40 && \
   HugePageTest $compressed_with_tar_file 1024 1024 16 41 && \
   HugePageTest $compressed_with_tar_file 1024 1024 0 42 && \
   HugePageTest $compressed_with_tar_file 0 1024 0 43 && \
   HugePageTest $compressed_with_tar_file 1024 0 16 44 && \
   HugePageTest $compressed_with_tar_file 0 0 16 45 && \
   HugePageTest $compressed_with_tar_file 1024 0 0 46 && \
   HugePageTest $compressed_with_tar_file 0 0 0 47
   #HugePageTest $compressed_with_tar_file 0 1024 16 48
then
    echo "HugePage memory test OK"
else
    echo "HugePage memory test FAILED!!! :(";
    exit 1
fi

exit 0
