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
test_file_path="/opt/compressdata"
test_file="calgary.1G"
test_file_compressed="calgary.1G.gz"
test_qzip=${QZ_ROOT}/utils/qzip
DRV_FILE=${QZ_TOOL}/install_drv/install_upstream.sh

echo "run test chunksz"
#get the type of QAT hardware
platform=`lspci | grep Co-processor | awk '{print $6}' | head -1`
cp $test_file_path/$test_file .
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

if [[ $platform = "37c8" || $platform = "C62x" ]]
then
    DVR_OPT="-D8 -P1 -L"
    # Install upstream driver
    if $DRV_FILE $DVR_OPT > /dev/null
    then
      echo -e "\nInstall upstream driver with NumberDcInstances = 8 OK :)\n"
    else
      echo "Install upstream driver with NumberDcInstances = 8 FAILED!!! :("
      exit 1
    fi
    $test_qzip -C 262144 $test_file > /dev/null 2>&1
    taskset -c 2 $test_qzip -d -C 262144 $test_file_compressed > log_chunksztest
    throught_hardware=$(cat log_chunksztest | grep Throughput | awk '{print $2}')
    $test_qzip -C 262144 $test_file > /dev/null 2>&1
    taskset -c 2 $test_qzip -d -C 65536  $test_file_compressed > log_chunksztest
    throught_software=$(cat log_chunksztest | grep Throughput | awk '{print $2}')
    rm -f log_chunksztest
elif [ $platform = "DH895XCC" ]
then
    DVR_OPT="-D8 -P1"
    # Install upstream driver
    if $DRV_FILE $DVR_OPT > /dev/null
    then
      echo -e "\nInstall upstream driver with NumberDcInstances = 8 OK :)\n"
    else
      echo "Install upstream driver with NumberDcInstances = 8 FAILED!!! :("
      exit 1
    fi
    $test_qzip -C 262144 $test_file > /dev/null 2>&1
    taskset -c 2 $test_qzip -d -C 262144 $test_file_compressed > log_chunksztest
    throught_hardware=$(cat log_chunksztest | grep Throughput | awk '{print $2}')
    $test_qzip -C 262144 $test_file > /dev/null 2>&1
    taskset -c 2 $test_qzip -d -C 65536  $test_file_compressed > log_chunksztest
    throught_software=$(cat log_chunksztest | grep Throughput | awk '{print $2}')
    rm -f log_chunksztest
fi
rm -f $test_file
#deterimine if this test passed
echo "throught_hardware=$throught_hardware"
echo "throught_software=$throught_software"
if [[ ( $platform = "37c8" || $platform = "C62x" ) && \
      $(echo "$throught_hardware > 16367.2" | bc) = 1 && \
      $(echo "$throught_software < 1252.3" | bc) = 1 ]]
then
    echo -e "run test chunksz PASSED:)\n"
    exit 0
elif [[ $platform = "DH895XCC" && \
      $(echo "$throught_hardware > 15718.3" | bc) = 1 && \
      $(echo "$throught_software < 1218.8" | bc) = 1 ]]
then
    echo -e "run test chunksz PASSED:)\n"
    exit 0
else
    echo "run test chunksz FAILED!!! :("
    exit 1
fi
