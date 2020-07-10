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

rm -f result_comp
rm -f result_decomp
rm -f result_comp_stderr
rm -f result_decomp_stderr

CURRENT_PATH=`dirname $(readlink -f "$0")`

#get the type of QAT hardware
platform=`lspci | grep Co-processor | awk '{print $6}' | head -1`
if [ $platform != "37c8" ]
then
    platform=`lspci | grep Co-processor | awk '{print $5}' | head -1`
    if [[ $platform != "DH895XCC" && $platform != "C62x" ]]
    then
        echo "Unsupport Platform: `lspci | grep Co-processor` "
        exit 1
    fi
fi
echo "platform=$platform"

#Replace the driver configuration files and configure hugepages
echo "Replace the driver configuration files and configure hugepages."
if [[ $platform = "37c8" || $platform = "C62x" ]]
then
    process=24
    \cp $CURRENT_PATH/config_file/c6xx/c6xx_dev0.conf /etc
    \cp $CURRENT_PATH/config_file/c6xx/c6xx_dev1.conf /etc
    \cp $CURRENT_PATH/config_file/c6xx/c6xx_dev2.conf /etc
elif [ $platform = "DH895XCC" ]
then
    process=8
    \cp $CURRENT_PATH/config_file/dh895xcc/dh895xcc_dev0.conf /etc
fi
service qat_service restart
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
rmmod usdm_drv
insmod $ICP_ROOT/build/usdm_drv.ko max_huge_pages=1024 max_huge_pages_per_process=24


#Perform performance test
echo "Perform performance test"
thread=4

echo > result_comp
for((numProc_comp = 0; numProc_comp < $process; numProc_comp ++))
do
    $QZ_ROOT/test/test -m 4 -l 1000 -t 4 -D comp >> result_comp 2>> result_comp_stderr &
done
wait
compthroughput=`awk '{sum+=$8} END{print sum}' result_comp`
echo "compthroughput=$compthroughput Gbps"

echo > result_decomp
for((numProc_decomp = 0; numProc_decomp < $process; numProc_decomp ++))
do
    $QZ_ROOT/test/test -m 4 -l 1000 -t $thread -D decomp >> result_decomp 2>> result_decomp_stderr &
done
wait
decompthroughput=`awk '{sum+=$8} END{print sum}' result_decomp`
echo "decompthroughput=$decompthroughput Gbps"

