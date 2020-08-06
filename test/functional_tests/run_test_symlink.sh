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
echo "***QZ_ROOT run_test_symlink.sh start"
readonly BASEDIR=$(cd `dirname $0`; pwd)
test_qzip="${BASEDIR}/../../utils/qzip "
test_file_path="/opt/compressdata"
sample_file_name="calgary"
symlink_file_name="symlink"

#Not Exist Symlink Test
#Add a case for testing symlink, verify that Qzip will not open a file outside it's realm
echo "Not Exist Symlink Test"
function NotExistSymlinkTest()
{
    local test_file_name=$1
    local rc=0

    if [ ! -f "$test_file_path/$test_file_name" ]
    then
        echo "$test_file_path/$test_file_name does not exit!"
        rc=1
    fi

    cp -f $test_file_path/$test_file_name ./
    ln -s $test_file_name $symlink_file_name
    rm -f $test_file_name

    $test_qzip $symlink_file_name > NotExistSymlinkTestlog 2>&1

    Keyword=$(grep "symlink: No such file or directory" NotExistSymlinkTestlog)
    rm -f NotExistSymlinkTestlog

    if [[ -z $Keyword ]]
    then
        echo "Check Keyword is null, Not Exist Symlink Test FAILED."
        rc=1
    else
        echo "Check Keyword is ok, Not Exist Symlink Test PASSED."
        rc=0
    fi

    rm -f $symlink_file_name
    rm -f $sample_file_name
    return $rc
}

if NotExistSymlinkTest $sample_file_name
then
    echo "Not Exist Symlink Test PASSED"
else
    echo "Not Exist Symlink Test FAILED!!!"
    exit 1
fi
echo "***QZ_ROOT run_test_symlink.sh end"
exit 0
