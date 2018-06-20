#**************************************************************************
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
readonly BASEDIR=$(readlink -f $(dirname $0))

run_main="${BASEDIR}/../test "
RESULT_DIR="$BASEDIR/results"
RESULT_FILE="$RESULT_DIR/$(date +%Y%m%d)-$(date +%H:%M:%S)_res.txt"
strip_dir_main=$(echo $run_main | sed 's/\(.*\)\(test$\)/\2/')

declare -a thd_cnt verify algorithm swBack
thd_cnt=("1" "2" "16" "100")
algorithm=("deflate" "snappy" "lz4")

declare -a compLevel huffmanType pollSleep hwBuffSz
compLevel=("1" "6" "9")
huffmanType=("static" "dynamic")
pollSleep=("0" "1000")

#2K 16K 64K
hwBuffSz=("$((2*1024))" "$((64*1024))")
#hwBuffSz=("$((2*1024))" "$((16*1024))" "$((64*1024))")
declare -a direction=("comp" "decomp" "both")
declare -i loop=0 mode=0

function print_usage()
{
  echo "-m|-mode, the arg can be set in 1, 2, 3, 4, 5"
  echo "mode1: test for memcpy feature"
  echo "mode2: test for USDM and malloc/free"
  echo "mode3: test for zero-cpy, by default configuration"
  echo "mode4: test for zero-cpy, by configurable parameters"
  echo "mode5: test for qzInit/qzSetSession inside and outside"
  echo "-i|-input: test data reading from inputfile"
  echo "-l|-loop: set the loop count"
  echo "-d|-delete: delete result directory"
  echo "-h|-help: print usage"
}

function show_test_result()
{
  local test_res=$1
  local failed=$(echo $test_res | egrep -i -e 'ERROR' -e 'err' -e 'FAILED' -n)

  if [ "$failed"x != x ]
  then
    echo "-->FAILED\n$test_res"
  else
    echo "-->PASSED"
  fi
}

#check the input arguments
set -o errexit
GETOPT_ARGS=`getopt -o m:i:l:dh -- "$@"`
eval set -- "$GETOPT_ARGS"
while [ true ]
do
  case "$1" in
    -m|-mode)
      if [ $2 -ne 1 ] && [ $2 -ne 2 ] \
      && [ $2 -ne 3 ] && [ $2 -ne 4 ] \
      && [ $2 -ne 5 ] ; then
        echo "[ERROR] parameter: -m|-mode $2"
        exit 1
      else
        mode=$2; shift 2;
      fi
      ;;
    -i|-input)
      if [ -f $2 ];then
        input_file=$2; shift 2;
      else
        echo "[ERROR] parameter:-i|-input $2"; exit 1;
      fi
      ;;
    -l|-loop)
      if [ -n "$(echo $2| sed -n "/^[0-9]\+$/p")" ];then
        loop=$2; shift 2;
      else
        echo "[ERROR] parameter:-l|-loop $2"; exit 1;
      fi
      ;;
    -d|-delete)
      rm -rf $RESULT_DIR
      shift
      ;;
    -h|-help)
      print_usage; exit 1;
      ;;
    *)
      break;;
  esac
done
set +o errexit

#check result directory
if ! test -d "$RESULT_DIR" ; then
  mkdir "$RESULT_DIR"
fi

#test mode 1
#only support compresssion
function test_mode1()
{
  local test_res=""
  echo -e "\nTest Mode 1" && echo -e "Test Mode 1\n" >> $RESULT_FILE
  for thd_count in "${thd_cnt[@]}"; do
    for huffman in "${huffmanType[@]}"; do
      for interval in "${pollSleep[@]}"; do
          arg="-m 1 -t $thd_count -T $huffman -P $interval"
          if ! test_res=$($run_main $arg 2>&1); then
              echo $test_res; exit 1
          fi
          echo -e "Run $strip_dir_main $arg $(show_test_result "$test_res")"
          echo -e "Run $run_main $arg\n$test_res" >> $RESULT_FILE
      done
    done
  done
}

#test mode 2
function test_mode2()
{
  local test_res=""
  echo -e "\nTest Mode 2" && echo -e "Test Mode 2\n" >> $RESULT_FILE
  for thd_count in "1" "2" ; do
    arg="-m 2 -t $thd_count"
    if ! test_res=$($run_main $arg 2>&1); then
        echo $test_res; exit 1
    fi
    echo -e "Run $strip_dir_main $arg $(show_test_result "$test_res")"
    echo -e "Run $run_main $arg\n$test_res" >> $RESULT_FILE
  done
}

#test mode 3
#redundant with thread_main4
function test_mode3()
{
  local test_res=""
  echo -e "\nTest Mode 3" && echo -e "Test Mode 3\n" >> $RESULT_FILE
  for thd_count in "${thd_cnt[@]}" ; do
    arg="-m 3 -t $thd_count"
    if ! test_res=$($run_main $arg 2>&1); then
        echo $test_res; exit 1
    fi
    echo -e "Run $strip_dir_main $arg $(show_test_result "$test_res")"
    echo -e "Run $run_main $arg\n$test_res" >> $RESULT_FILE
  done
}

#test mode 4
function test_mode4()
{
  local test_res=""
  if test -z $input_file ; then
    echo -e "\nTest Mode 4 by random data" &&
    echo -e "Test Mode 4 by random data\n" >> $RESULT_FILE
  else
    echo -e "\nTest Mode 4 by input data" &&
    echo -e "Test Mode 4 by input data\n" >> $RESULT_FILE
  fi

  for thd_count in "${thd_cnt[@]}" ; do
    for complv in "${compLevel[@]}"; do
      for pollSp in "${pollSleep[@]}"; do
        for hwSz in "${hwBuffSz[@]}"; do
          for huffman in "${huffmanType[@]}"; do
            for verify in "-v" ""; do
              for dir in "${direction[@]}"; do
                arg="-m 4 -t $thd_count -L $complv -P $pollSp -C $hwSz"
                arg+=" -T $huffman $verify -D $dir"

                if [ ! -z $input_file  ] ; then
                  arg+=" -i $input_file"
                fi

                if [ $loop -ne 0 ]; then
                  arg+=" -l $loop"
                fi

                if ! test_res=$($run_main $arg 2>&1);then
                    echo $test_res; exit 1
                fi
                echo -e "Run $strip_dir_main $arg $(show_test_result "$test_res")"
                echo -e "Run $run_main $arg\n$test_res" >> $RESULT_FILE
              done
            done
          done
        done
      done
    done
  done
}

#test mode 5
function test_mode5()
{
  local test_res=""
  echo -e "\nTest Mode 5" && echo -e "Test Mode 5\n" >> $RESULT_FILE
  for thd in "1" "5" ; do
    for engine in "enable" "disable" ; do
      for session in "enable" "disable" ; do
        arg="-m 4 -e $engine -s $session -t $thd"
        if ! test_res=$($run_main $arg 2>&1); then
            echo $test_res; exit 1
        fi
        echo -e "Run $strip_dir_main $arg $(show_test_result "$test_res")"
        echo -e "Run $run_main $arg\n$test_res" >> $RESULT_FILE
      done
    done
  done
}

case $mode in
1)
  test_mode1;;
2)
  test_mode2;;
3)
  test_mode3;;
4)
  test_mode4;;
5)
  test_mode5;;
0)
  test_mode1
  test_mode2
  test_mode3
  test_mode4
  test_mode5
  ;;
esac

#show Error
cat $RESULT_FILE | egrep -i -e 'ERROR' -e 'err' -e 'FAILED' -n
exit 0
