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

readonly BASEDIR=$(cd `dirname $0`; pwd)
test_main="${BASEDIR}/../test "
#Using Huge Pages test
echo "Using Huge Pages Test"

#***********************************************************************************
#    set max_huge_pages to 8 (sufficent hugepage for test_main -m 4 with one thread)
#    Use test_main -m 4 to start one process with two thread
#    The first thread use hugepage
#    The second thread has insufficent hugepage
#***********************************************************************************
function run_with_hugepage()
{
    echo "run with hugepage test"
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=8 max_huge_pages_per_process=8

    sleep 5

    $test_main -m 4 -D "both" -t 2 > usehugepagelog 2>&1

    if [ $? -ne 0 ]
    then
        echo "Runing $test_main -m 4 -D "both" -t 2 FAILED!!!"
        rc=1
    else
        echo "Runing $test_main -m 4 -D "both" -t 2 OK!!!"
        rc=0
    fi

    error_Key=$(grep "exceeded max huge pages allocations for this process" usehugepagelog)
    rm -f usehugepagelog

    if [[ -z $error_Key ]]
    then
        echo "Check error_Key is null, run with kernel memory test FAILED."
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}

#***********************************************************************************
#    set max_huge_pages to 10 (insufficent hugepage for test_main -m 4 with two thread)
#    Use test_main -m 4 to start the first process with two thread
#    The first process use hugepage
#    Use test_main -m 4 to start the second process with two thread
#    The second process use kernel memory
#    Kill the first process and hugepage has been released
#    The second process still use kernel memory
#***********************************************************************************
function run_with_kernel_mem()
{
    echo "run with kernel memory test"
    local rc=0

    current_num_HP=`awk '/HugePages_Total/ {print $NF}' /proc/meminfo`

    echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=14 max_huge_pages_per_process=14

    sleep 5

    echo "start first process"
    $test_main -m 4 -D "both" -t 2 -l 1000 &

    if [ $? -ne 0 ]
    then
        echo "Runing $test_main -m 4 -D "both" -t 2 -l 1000 FAILED!!!"
        rc=1
    else
        echo "Runing $test_main -m 4 -D "both" -t 2 -l 1000 OK!!!"
        rc=0
    fi

    pid_first=`ps aux | grep "test -m 4 -D "both" -t 2 -l 1000" | awk '{print $2}' | head -1`
    hugepage_free_first=`cat /proc/meminfo | grep HugePages_Free`
    echo "first pid = $pid_first"
    echo $hugepage_free_first

    echo "start second process"
    $test_main -m 4 -D "both" -t 2 -l 1000 > usekernelmemlog 2>&1 &

    if [ $? -ne 0 ] ; then
        echo "Runing $test_main -m 4 -D "both" -t 2 -l 1000 FAILED!!! :("
        rc=1
    else
        echo "Runing $test_main -m 4 -D "both" -t 2 -l 1000 OK!!!"
        rc=0
    fi

    kill -9 $pid_first
    sleep 2

    hugepage_free_second=`cat /proc/meminfo | grep HugePages_Free`
    echo $hugepage_free_second
    wait

    error_Key=$(grep "exceeded max huge pages allocations for this process" usekernelmemlog)
    rm -f usekernelmemlog

    if [[ ! -z $error_Key ]]
    then
        echo "Check error_Key is not null, run with kernel memory test FAILED."
        rc=1
    fi

    echo $current_num_HP > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    rmmod usdm_drv
    insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=$current_num_HP max_huge_pages_per_process=256

    return $rc
}


if run_with_hugepage  && \
   run_with_kernel_mem
then
    echo "Using Huge Pages test PASSED"
else
    echo "Using Huge Pages test FAILED!!! :(";
    exit 1
fi

exit 0
