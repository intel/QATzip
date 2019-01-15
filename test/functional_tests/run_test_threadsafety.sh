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

test_main=${QZ_ROOT}/test/test
DRV_FILE=${QZ_TOOL}/install_drv/install_upstream.sh

#get the type of QAT hardware
platform=`lspci | grep Co-processor | awk '{print $6}' | head -1`
if [ $platform != "37c8" ]
then
    platform=`lspci | grep Co-processor | awk '{print $5}' | head -1`
    if [ $platform != "DH895XCC" ]
    then
        echo "Unsupport Platform: `lspci | grep Co-processor` "
        exit 1
    fi
fi
echo "platform=$platform"

# Install upstream driver
DVR_OPT="-D4 -P8"
if $DRV_FILE $DVR_OPT > /dev/null
then
    echo -e "\nInstall upstream driver with NumProcesses = 8 OK :)\n"
else
    echo "Install upstream driver with NumProcesses = 8 FAILED!!! :("
    exit 1
fi

if [ $platform = "37c8" ]
then
    echo > log_threadsafety
    for((numProc = 0; numProc < 8; numProc ++))
    do
        $test_main -m 18 -l 100 -t 12 -D both  2>&1 >> log_threadsafety &
        if [ $? -ne 0 ] ; then
            echo "Runing $test_main -m 18 -l 100 -t 12 -D both  FAILED!!! :("; exit 1
        fi
    done
    wait
    passed_count=`cat log_threadsafety | grep -c "Check g_process PASSED"`
    rm -f log_threadsafety
elif [ $platform = "DH895XCC" ]
then
    echo > log_threadsafety
    for((numProc = 0; numProc < 8; numProc ++))
    do
        $test_main -m 18 -l 100 -t 4 -D both 2>&1 >> log_threadsafety &
        if [ $? -ne 0 ] ; then
            echo "Runing $test_main -m 18 -l 100 -t 4 -D both FAILED!!! :("; exit 1
        fi
    done
    wait
    passed_count=`cat log_threadsafety | grep -c "Check g_process PASSED"`
    rm -f log_threadsafety
fi

if [ $passed_count = "8" ]
then
    echo -e "run test threadsafety PASSED:)\n"
    exit 0
else
    echo "run test threadsafety FAILED!!! :("
    exit 1
fi
