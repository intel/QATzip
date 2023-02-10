#!/usr/bin/bash
################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
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
################################################################


# exit on errors
set -e
: ${QZ_ROOT?}

readonly BASEDIR=$(readlink -f $(dirname $0))
declare -i rc=0
rm -f $BASEDIR/astyle.log

if hash astyle; then
  echo -n "Checking coding style..."
  find $QZ_ROOT -iregex '.*\.[ch]'         | \
  xargs astyle --options=$BASEDIR/astylerc | \
  tee -a $BASEDIR/astyle.log

  if grep -q "^Formatted" $BASEDIR/astyle.log; then
    echo -e "ERRORS detected\n"
    grep --color=auto "^Formatted.*" $BASEDIR/astyle.log
    echo "Incorrect code style detected in one or more files."
    echo "The files have been automatically formatted."
    rc=1
  else
    echo " OK"
  fi
else
  echo "You do not have astyle installed so your code style is not being checked!"
  rc=2
fi

exit $rc
