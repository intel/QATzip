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
echo "***QZ_ROOT run_test_decompress_combined_file.sh start"
test_qzip=${QZ_ROOT}/utils/qzip
test_file_path="/opt/compressdata"
sample_file_name="calgary"
big_file_name="calgary.2G"

function decom_combined_file1()
{
    echo "decom_combined_file1"
    local test_file_name=$1
    cp -f $test_file_path/$test_file_name ./
    $test_qzip -k $test_file_name -o file1
    $test_qzip -C 262144 $test_file_name -o file2
    cat file1.gz file2.gz > file.gz
    rm -f file1.gz file2.gz
    gzip -cd file.gz > file_gzip
    $test_qzip -d file.gz -o file_qzip
    OLDMD5=`md5sum file_gzip | awk '{print $1}'`
    NEWMD5=`md5sum file_qzip | awk '{print $1}'`
    rm -f file_gzip file_qzip
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}

#Qzip should be able to decompress files whose length
#of the last few bytes of the first 512Mb data is not
#enough for a Gzip header with QZ extension.
function decom_combined_file2()
{
    echo "decom_combined_file2"
    local test_file_name1=$1
    local test_file_name2=$2
    cp -f $test_file_path/$test_file_name1 ./
    cp -f $test_file_path/$test_file_name2 ./
    split -b 1371159690 $test_file_name1
    $test_qzip xaa
    $test_qzip $test_file_name2
    cat xaa.gz $test_file_name2.gz > file.gz
    gzip -cd file.gz > file_gzip
    $test_qzip -d file.gz -o file_qzip
    OLDMD5=`md5sum file_gzip | awk '{print $1}'`
    NEWMD5=`md5sum file_qzip | awk '{print $1}'`
    rm -f $test_file_name1 file_gzip file_qzip xab xaa.gz $test_file_name2.gz
    echo "old md5" $OLDMD5
    echo "new md5" $NEWMD5
    if [[ $NEWMD5 != $OLDMD5 ]]
    then
        return 1
    fi

    return 0
}

echo "decom combined_file test START"
if decom_combined_file1 $sample_file_name &&
   decom_combined_file2 $big_file_name $sample_file_name
then
   echo "decom combined_file test PASSED"
else
   echo "decom combined_file test FAILED!!!"
   exit 2
fi
echo "***QZ_ROOT run_test_decompress_combined_file.sh end"
exit 0