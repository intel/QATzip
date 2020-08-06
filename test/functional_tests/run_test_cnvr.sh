#! /bin/bash
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

set -e
echo "***QZ_ROOT run_test_cnvr.sh start"
readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_file_name="payload6"
test_file_name4M="payload7"
test_file_path="/opt/compressdata"
hwChunkSz=("$((64*1024))" "$((128*1024))" "$((256*1024))")

echo "CnV+R working correctly for various buffer sizes test"
if [ ! -f "$test_file_path/$test_file_name4M" ]
then
    echo "$test_file_path/$test_file_name4M is not exit!"
    for i in $(seq 1 1024)
    do
        cat $test_file_path/$test_file_name >> $test_file_path/$test_file_name4M
    done
else
    filesize=`ls -l $test_file_path/$test_file_name4M | awk '{print $5}'`
    requiresize=$((1024*1024*4))
    echo "filesize=$filesize"
    if [ $filesize -eq $requiresize ]
    then
        echo "$test_file_path/$test_file_name4M  exit, and size is 4M!"
    else
        echo "$test_file_path/$test_file_name4M  exit, but size is not 4M!"
        cat /dev/null > $test_file_path/$test_file_name4M
        for i in $(seq 1 1024)
        do
            cat $test_file_path/$test_file_name >> $test_file_path/$test_file_name4M
        done
    fi
fi

cp -f $test_file_path/$test_file_name4M ./
orig_checksum=`md5sum $test_file_name4M`
echo "orig_checksum = $orig_checksum";
for hwSz in "${hwChunkSz[@]}"; do
    if cat $test_file_name4M | $test_qzip -C $hwSz > $test_file_name4M.gz && cat $test_file_name4M.gz | gzip -d > $test_file_name4M
    then
        echo "hwsz = $hwSz, (De)Compress $test_file_name4M OK";
    else
        echo "hwsz = $hwSz, (De)Compress $test_file_name4M Failed";
        exit 1
    fi
    new_checksum=`md5sum $test_file_name4M`
    echo "new_checksum = $new_checksum";
    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "hwsz = $hwSz, Checksum mismatch, CnV+R working for various buffer sizes test FAILED."
        exit 1
    else
        echo "hwsz = $hwSz, Checksum match, CnV+R working for various buffer sizes test PASSED.";
    fi

done
rm -f $test_file_name4M
rm -f $test_file_name4M.gz
echo "***QZ_ROOT run_test_cnvr.sh end"
exit 0
