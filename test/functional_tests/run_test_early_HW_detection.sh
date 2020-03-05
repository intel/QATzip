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
echo "***QZ_ROOT run_test_early_HW_detection.sh start"
readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_main="${BASEDIR}/../test "
test_file_path="/opt/compressdata"
sample_file_name="calgary"

#Early HW detection test
#Stop the service(adf_ctrl down), test_main -m 9 should return QZ_NOSW_NO_HW
function early_HW_detection_service_down_test()
{
    echo "early_HW_detection_service_down_test"
    local test_file_name=$1
    local rc=0

    if [ -d  $ICP_ROOT/CRB_modules ];
    then
        DRIVER_DIR=$ICP_ROOT/CRB_modules;
    else
        DRIVER_DIR=$ICP_ROOT/build;
    fi

    $DRIVER_DIR/adf_ctl down

    if $test_main -m 9 -B 0 > EarlyHWDetectionTestlog 2>&1
    then
        echo "early HW detection service down test PASSED"
        rc=0
    else
        echo "early HW detection service down test FAILED"
        rc=1
    fi

    #error_Key=$(grep "QZ_NOSW_NO_HW" EarlyHWDetectionTestlog)
    error_Key=$(grep "\-101" EarlyHWDetectionTestlog)
    rm -f EarlyHWDetectionTestlog

    if [[ -z $error_Key ]]
    then
        echo "Check error_Key is null, early HW detection service down test FAILED."
        rc=1
    fi

    $DRIVER_DIR/adf_ctl up

    return $rc
}

#Open the service ,test_main -m 9 -b should go to SW, decompress and compare the md5 of the two files
function early_HW_detection_service_up_test()
{
    echo "early_HW_detection_service_up_test"
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    if $test_main -m 9 -B 1 -i $test_file_name -D "both"
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
        echo "Checksum mismatch, early HW detection service up test FAILED."
        rc=1
    fi

    rm $test_file_name

    return $rc
}

#Open hw, set huge page to 0, test_main -m 9 should use HW
function early_HW_detection_service_hugepage0_test()
{
    echo "early_HW_detection_service_hugepage0_test"
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    echo 0 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=0 max_huge_pages_per_process=0

    sleep 5

    if $test_main -m 9 -B 0
    then
        echo "Hugepage = 0 test PASSED"
        rc=0
    else
        echo "Hugepage = 0 test failed"
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}

#Open hw, set huge page to 0,test_main -m 9 should go to HW, decompress and compare the md5 of the two files
function early_HW_detection_service_up_hugepage0_test()
{
    echo "early_HW_detection_service_up_hugepage0_test"
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`
    echo 0 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=0 max_huge_pages_per_process=0

    sleep 5

    if $test_main -m 9 -i $test_file_name -D "both"
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
        echo "Checksum mismatch, early HW detection service up hugepage0 test FAILED."
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    rm $test_file_name

    return $rc
}

if early_HW_detection_service_down_test  && \
   early_HW_detection_service_up_test $sample_file_name  && \
   early_HW_detection_service_hugepage0_test  && \
   early_HW_detection_service_up_hugepage0_test $sample_file_name
then
    echo "Early HW detection test PASSED"
else
    echo "Early HW detection test FAILED!!! :(";
    exit 1
fi
echo "***QZ_ROOT run_test_early_HW_detection.sh end"
exit 0
