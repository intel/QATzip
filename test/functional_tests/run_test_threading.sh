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
echo "***QZ_ROOT run_test_threading.sh start"
test_main=${QZ_ROOT}/test/test
DRV_FILE=${QZ_TOOL}/install_drv/install_upstream.sh
test_file_path="/opt/compressdata"
test_file="calgary.1G"

#get the type of QAT hardware
$QZ_TOOL/get_platform/get_platforminfo.sh
platform=`cat $QZ_TOOL/get_platform/PlatformInfo`
echo "platform=$platform"
if [ $platform == "C3000" ]
then
    echo "The threading test case does not need to run on C3000 platform!"
    exit 0
fi

#Block by QATAPP-17269
if [ $platform == "270b" ]
then
    echo "Skip this case on CPM1.8 platform"
    exit 0
fi
#Block by QATAPP-17269

#get the performance data of 8 instances(hw), 12 threads, 4 to SW
echo "Test 8 instances(hw), 12 threads, 4 to SW"
DVR_OPT="-D8 -P1 -L"
# Install upstream driver
if $DRV_FILE $DVR_OPT > /dev/null
then
  echo -e "\nInstall upstream driver with NumberDcInstances = 8 OK :)\n"
else
  echo "Install upstream driver with NumberDcInstances = 8 FAILED!!! :("
  exit 1
fi

cp $test_file_path/$test_file .
$test_main -m 4 -t 12 -i $test_file > result 2>&1
throught_ave_hardware=$(cat result | head -n 9 | tail -n 8 | awk '{sum+=$8} END{sum/=8; print sum}')
throught_ave_software=$(cat result | tail -n 4 | awk '{sum+=$8} END{sum/=4; print sum}')
rm -f result
rm -f $test_file

#deterimine if this test passed
echo "throught_ave_hardware=$throught_ave_hardware"
echo "throught_ave_software=$throught_ave_software"
if [[ ( $platform = "37c8" || $platform = "C62x" ) && \
      $(echo "$throught_ave_hardware > 2.29" | bc) = 1 && \
      $(echo "$throught_ave_software < 0.24" | bc) = 1 ]]
then
    echo -e "run test 8 instances(hw), 12 threads, 4 to SW PASSED:)\n"
elif [[ ( $platform = "DH895XCC" || $platform == "270b" ) && \
      $(echo "$throught_ave_hardware > 1.27" | bc) = 1 && \
      $(echo "$throught_ave_software < 0.22" | bc) = 1 ]]
then
    echo -e "run test 8 instances(hw), 12 threads, 4 to SW PASSED:)\n"
else
    echo "run test 8 instances(hw), 12 threads, 4 to SW FAILED!!! :("
    exit 1
fi

#get the performance data of 1 instances(hw), 12 threads, 11 to SW
echo "Test 1 instances(hw), 12 threads, 11 to SW"
DVR_OPT="-D1 -P1 -L"
# Install upstream driver
if $DRV_FILE $DVR_OPT > /dev/null
then
  echo -e "\nInstall upstream driver with NumberDcInstances = 1 OK :)\n"
else
  echo "Install upstream driver with NumberDcInstances = 1 FAILED!!! :("
  exit 1
fi
cp $test_file_path/$test_file .
$test_main -m 4 -t 12 -i $test_file > result 2>&1
throught_ave_hardware=$(cat result | head -n 2 | tail -n 1 | awk '{sum+=$8} END{sum/=1; print sum}')
throught_ave_software=$(cat result | tail -n 11 | awk '{sum+=$8} END{sum/=11; print sum}')
rm -f result
rm -f $test_file

#deterimine if this test passed
echo "throught_ave_hardware=$throught_ave_hardware"
echo "throught_ave_software=$throught_ave_software"
if [[ ( $platform = "37c8" || $platform = "C62x" ) && \
      $(echo "$throught_ave_hardware > 17.3" | bc) = 1 && \
      $(echo "$throught_ave_software < 0.15" | bc) = 1 ]]
then
   echo -e "run test 1 instances(hw), 12 threads, 11 to SW PASSED:)\n"
elif [[ ( $platform = "DH895XCC" || $platform == "270b" ) && \
      $(echo "$throught_ave_hardware > 10.2" | bc) = 1 && \
      $(echo "$throught_ave_software < 0.15" | bc) = 1 ]]
then
    echo -e "run test 1 instances(hw), 12 threads, 11 to SW PASSED:)\n"
else
    echo "run test 1 instances(hw), 12 threads, 11 to SW FAILED!!! :("
    exit 1
fi
echo "***QZ_ROOT run_test_threading.sh end"
exit 0