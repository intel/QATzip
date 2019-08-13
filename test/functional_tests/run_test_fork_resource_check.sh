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
DRV_FILE=${QZ_TOOL}/install_drv/install_upstream.sh
test_main="${BASEDIR}/../test "

#test for fork resource check
function fork_resource_check_test()
{
    DVR_OPT="-C0 -D1 -P2 -L"
    if $DRV_FILE $DVR_OPT > /dev/null
        then
          echo -e "\nInstall upstream driver OK :)\n"
        else
          echo "Install upstream driver FAILED!!! :("
          return 1
    fi

    $test_main -m 21 > forkResourceCheckTestlog 2>&1
    cat forkResourceCheckTestlog

    DVR_OPT="-L"
    if $DRV_FILE $DVR_OPT > /dev/null
        then
          echo -e "\nInstall upstream driver OK :)\n"
        else
          echo "Install upstream driver FAILED!!! :("
          return 1
    fi

    instID_parent=$(cat forkResourceCheckTestlog | grep "instID in parent process" | awk '{print $6}')
    instID_child=$(cat forkResourceCheckTestlog | grep "instID in child process" | awk '{print $6}')
    number_huge_pages_parent=$(cat forkResourceCheckTestlog | grep "number_huge_pages in parent process" | awk '{print $8}')
    number_huge_pages_child=$(cat forkResourceCheckTestlog | grep "number_huge_pages in child process" | awk '{print $8}')

    echo "instID_parent: $instID_parent"
    echo "instID_child: $instID_child"
    echo "number_huge_pages_parent: $number_huge_pages_parent"
    echo "number_huge_pages_child: $number_huge_pages_child"

    rm -f forkResourceCheckTestlog
    #check resources: instance and hugepage
    if [[ $instID_parent != $instID_child && \
          $(echo "$number_huge_pages_parent == 1792" | bc) = 1 && \
          $(echo "$number_huge_pages_child == 1536" | bc) = 1 ]]
    then
        return 0
    else
        return 1
    fi
}

if fork_resource_check_test
    then
       echo "fork resource check test PASSED"
    else
       echo "fork resource check test FAILED!!!"
       exit 2
fi

exit 0
