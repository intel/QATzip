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

#set -e
test_main=${QZ_ROOT}/test/test
echo "No hardware in platform Test"
echo "test for qzInitPcieCountCheck start"
#get the type and device of QAT hardware
device1=`lspci | grep Co-processor | awk '{print $1}' | sed -n '1p'`
device2=`lspci | grep Co-processor | awk '{print $1}' | sed -n '2p'`
device3=`lspci | grep Co-processor | awk '{print $1}' | sed -n '3p'`
echo "device:$device1  $device2  $device3"

$QZ_TOOL/get_platform/get_platforminfo.sh
platform=`cat $QZ_TOOL/get_platform/PlatformInfo`
echo "platform=$platform"

#Unbind the device from driver
if [ $platform == "DH895XCC" ]
then
    echo "0000:$device1" > /sys/bus/pci/drivers/dh895xcc/unbind
elif [ $platform == "C3000" ]
then
    echo "0000:$device1" > /sys/bus/pci/drivers/c3xxx/unbind
else
    echo "0000:$device1" > /sys/bus/pci/drivers/c6xx/unbind
    echo "0000:$device2" > /sys/bus/pci/drivers/c6xx/unbind
    echo "0000:$device3" > /sys/bus/pci/drivers/c6xx/unbind
fi

adf_ctl status

#test
$test_main -m 19 > resultsw 2>&1
qzinit1_status=$(cat resultsw | grep -c "qzInit1 error. rc = 11")
qzinit2_status=$(cat resultsw | grep -c "qzInit2 error. rc = 11")
rm -f resultsw

echo "qzinit1_status=$qzinit1_status"
echo "qzinit2_status=$qzinit2_status"

#Bind the device back to driver
if [ $platform == "DH895XCC" ]
then
    echo "0000:$device1" > /sys/bus/pci/drivers/dh895xcc/bind
elif [ $platform == "C3000" ]
then
    echo "0000:$device1" > /sys/bus/pci/drivers/c3xxx/bind
else
    echo "0000:$device1" > /sys/bus/pci/drivers/c6xx/bind
    echo "0000:$device2" > /sys/bus/pci/drivers/c6xx/bind
    echo "0000:$device3" > /sys/bus/pci/drivers/c6xx/bind
fi

adf_ctl restart
adf_ctl status

#report
if [[ $qzinit1_status == 1  &&  $qzinit2_status == 1 ]]
then
    echo -e "No hardware in platform Test PASSED."
    exit 0
else
    echo "No hardware in platform Test FAILED."
    exit 1
fi

