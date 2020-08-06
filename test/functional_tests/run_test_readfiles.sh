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
echo "***QZ_ROOT run_test_readfiles.sh start"
readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_file_path="/opt/compressdata"
sample_file_name="file-in-00"
highly_compressible_file_name="big-index.html"

#Reading Special-name Files test
echo "Reading Special-name Files test"

function reading_file_named_with_symbols()
{
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    mv $test_file_name $test_file_name%00abcdef.pdf
    test_file_rename=$test_file_name%00abcdef.pdf

    if $test_qzip $test_file_rename -o $test_file_rename-compressed && \
        $test_qzip -d "$test_file_rename-compressed.gz"
    then
        echo "(De)Compress $test_file_rename OK";
        rc=0
    else
        echo "(De)Compress $test_file_rename FAILED";
        rc=1
    fi

    mv $test_file_rename-compressed $test_file_name
    new_checksum=`md5sum $test_file_name`

    if [[ $new_checksum != $orig_checksum ]]
    then
        echo "Checksum mismatch, reading file named with symbols test FAILED."
        rc=1
    fi

    rm $test_file_name

    return $rc
}

function reading_file_all_zero()
{
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        return 1
    fi

    cp -f $test_file_path/$test_file_name ./
    orig_checksum=`md5sum $test_file_name`

    if $test_qzip $test_file_name -o $test_file_name-compressed && \
        $test_qzip -d "$test_file_name-compressed.gz"
    then
        echo "(De)Compress $test_file_name OK";
        rc=0
    else
        echo "(De)Compress $test_file_name FAILED";
        rc=1
    fi

    new_checksum=`md5sum $test_file_name-compressed`

    if [[ $new_checksum != $orig_checksum-compressed ]]
    then
        echo "Checksum mismatch, reading file all zero test FAILED."
        rc=1
    fi

    rm $test_file_name-compressed

    return $rc
}

if reading_file_named_with_symbols $highly_compressible_file_name && \
   reading_file_all_zero $sample_file_name
then
    echo "Reading Special-name Files test PASSED"
else
    echo "Reading Special-name Files test FAILED!!! :(";
    exit 1
fi
echo "***QZ_ROOT run_test_readfiles.sh end"
exit 0
