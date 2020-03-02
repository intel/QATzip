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

test_qzip=${QZ_ROOT}/utils/qzip
test_file_path="/opt/compressdata"
small_file_name="64kb_file.html"
sample_file_name="calgary"
big_file_name="calgary.1G"
huge_file_name="calgary.2G"

function try_HW_decom_for_GZIP_header_produced_by_qzip()
{
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name ./
    OLDMD5=`md5sum $test_file_name | awk '{print $1}'`
    $test_qzip -O gzip $test_file_name > /dev/null 2>&1
    $test_qzip -d $test_file_name.gz
    NEWMD5=`md5sum $test_file_name | awk '{print $1}'`
    rm -f $test_file_name
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}

function try_HW_decom_for_GZIP_header_produced_by_gzip()
{
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name ./
    OLDMD5=`md5sum $test_file_name | awk '{print $1}'`
    gzip -n $test_file_name
    $test_qzip -d $test_file_name.gz
    NEWMD5=`md5sum $test_file_name | awk '{print $1}'`
    rm -f $test_file_name
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}

if try_HW_decom_for_GZIP_header_produced_by_qzip $small_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_qzip $sample_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_qzip $big_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_qzip $huge_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_gzip $small_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_gzip $sample_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_gzip $big_file_name && \
   try_HW_decom_for_GZIP_header_produced_by_gzip $huge_file_name
then
   echo "try HW decom for GZIP header test PASSED"
else
   echo "try HW decom for GZIP header test FAILED!!!"
   exit 2
fi

exit 0