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
test_file_path="/opt/compressdata"
echo "test_file_path=$test_file_path"
test_file_name="expandable_data"
echo "test_file_name=$test_file_name"
if [ ! -f "$test_file_path/$test_file_name" ]
then
    echo "$test_file_path/$test_file_name is not exit!"
fi
cp -f $test_file_path/$test_file_name ./

orig_checksum=`md5sum $test_file_name`
echo "orig_checksum = $orig_checksum";
src_size=`ls -l $test_file_name | awk '{print $5}'`
echo "src_size=$src_size"

if cat $test_file_name | $test_qzip > $test_file_name.gz
then
    echo "Compress $test_file_name OK";
    comp_size=`ls -l $test_file_name.gz | awk '{print $5}'`
    echo "comp_size=$comp_size"
    if [ $src_size -gt $comp_size ]
    then
        echo "$src_size -gt $comp_size!"
    else
        echo "$src_size -le $comp_size!"
    fi
else
    echo "Compress $test_file_name Failed";
    exit 1
fi

if cat $test_file_name.gz | gzip -d > $test_file_name
then
    echo "DeCompress $test_file_name OK";
else
    echo "DeCompress $test_file_name Failed";
    exit 1
fi

new_checksum=`md5sum $test_file_name`
echo "new_checksum = $new_checksum";

if [[ $new_checksum != $orig_checksum ]]
then
    echo "Checksum mismatch, Files that expand test failed."
    exit 1
else
    echo "Checksum match, Files that expand test success."
fi

exit 0
