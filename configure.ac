################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2026 Intel Corporation. All rights reserved.
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

# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([qatzip], [1.3.2], [])
AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability foreign subdir-objects tar-pax])
AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_MACRO_DIRS([m4])

# Library versioning (libtool format: current:revision:age)
# Resulting .so version: (current-age).age.revision
AC_SUBST([LIBQATZIP_VERSION], [5:1:2])

# Programs
AC_PROG_AWK
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_AR
AC_PATH_TOOL([PKGCONFIG], [pkg-config])

LT_PREREQ([2.4])
LT_INIT

# Type checks
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Header checks
AC_CHECK_HEADERS([fcntl.h limits.h pthread.h sched.h stdint.h stdio.h stdlib.h string.h sys/ioctl.h sys/stat.h sys/time.h sys/types.h unistd.h utime.h])

# Function checks
AC_CHECK_FUNCS([fork getcwd gettimeofday malloc memmove memset mkdir mmap realpath sched_getcpu strerror strrchr strtol strtoul utime])

# Required libraries
m4_ifdef([AX_PTHREAD], [], [
  m4_fatal([AX_PTHREAD not found. Please install autoconf-archive or place ax_pthread.m4 in m4/])
])
AX_PTHREAD
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"
AC_CHECK_LIB([numa], [numa_node_of_cpu], [],
    [AC_MSG_ERROR([libnuma not found. Install libnuma-dev or numactl-devel.])])

# zlib >= 1.2.7
AC_CHECK_LIB([z], [deflate], [],
    [AC_MSG_ERROR([zlib not found. Install zlib1g-dev or zlib-devel.])])
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <zlib.h>]],
        [[#if ZLIB_VERNUM < 0x1270
          #error zlib too old
          #endif]])], [],
    [AC_MSG_ERROR([zlib >= 1.2.7 required])])

# lz4
AC_CHECK_LIB([lz4], [LZ4F_compressFrame], [],
    [AC_MSG_ERROR([liblz4 not found. Install liblz4-dev or lz4-devel.])])

# Internal xxhash with namespaced symbols
AC_DEFINE([XXH_NAMESPACE], [QATZIP_], [Prefix xxhash API with QATZIP_])

#check for zstd lib
AC_CHECK_HEADER([zstd.h], , [AC_MSG_ERROR([zstd.h not found])])
AC_CHECK_LIB([zstd], [ZSTD_compressCCtx], ,AC_MSG_ERROR([ZSTD_compressCCtx not found]))

# CPM5.x detection: checks for three symbols present only in CPM5.x builds
# Argument $1: QAT library name (qat for system, qat_s for OOT)
AC_DEFUN([CHECK_CPM5X], [
    AC_CHECK_LIB([$1], [cpaDcQueryCapabilityByType],
        [have_qat_dc_cap=yes], [have_qat_dc_cap=no],
        [-lusdm -lcrypto])
    AS_IF([test "x$have_qat_dc_cap" = "xyes"],
        [AC_CHECK_LIB([$1], [cpaDcCompressDataWithDict],
            [AC_CHECK_LIB([$1], [cpaDcZstdCompressBound],
                [CFLAGS+=" -DCPM_5X"],
                [have_qat_dc_cap=no],
                [-lusdm -lcrypto])],
            [have_qat_dc_cap=no],
            [-lusdm -lcrypto])])
])

# QAT configuration
AC_ARG_WITH([ICP_ROOT],
    [AS_HELP_STRING([--with-ICP_ROOT=PATH], [Path to QAT driver source])],
    [AS_IF([test "$withval" = "yes"],
        [AC_MSG_ERROR([--with-ICP_ROOT requires a path])],
        [ICP_ROOT="$withval"])])

ICP_INCLUDE_CFLAGS=
AS_IF([test -n "${ICP_ROOT}"],
[
    # Out-of-tree QAT build
    AC_MSG_CHECKING([QAT headers in ${ICP_ROOT}])
    AS_IF([test ! -e "${ICP_ROOT}/quickassist/include/cpa.h" ||
           test ! -e "${ICP_ROOT}/quickassist/include/dc/cpa_dc.h" ||
           test ! -e "${ICP_ROOT}/quickassist/utilities/libusdm_drv/qae_mem.h" ||
           test ! -e "${ICP_ROOT}/quickassist/lookaside/access_layer/include/icp_sal_poll.h" ||
           test ! -e "${ICP_ROOT}/quickassist/lookaside/access_layer/include/icp_sal_user.h"],
        [AC_MSG_RESULT([no])
         AC_MSG_ERROR([QAT headers not found in ${ICP_ROOT}])],
        [AC_MSG_RESULT([yes])
         ICP_INCLUDE_CFLAGS="-I${ICP_ROOT}/quickassist/include \
                             -I${ICP_ROOT}/quickassist/include/dc \
                             -I${ICP_ROOT}/quickassist/utilities/libusdm_drv \
                             -I${ICP_ROOT}/quickassist/lookaside/access_layer/include"
         LDFLAGS="${LDFLAGS} -Wl,-rpath,${ICP_ROOT}/build -L${ICP_ROOT}/build"])
    AC_SUBST([ICP_INCLUDE_CFLAGS])

    # Check for CPM5.x dictionary compression support
    CHECK_CPM5X([qat_s])

    AC_CHECK_LIB([qat_s], [cpaDcCompressData2], [],
        [AC_MSG_ERROR([libqat_s not found in ${ICP_ROOT}/build])])

    AC_CHECK_LIB([qat_s], [icp_adf_get_numDevices],
        [ADF_CFLAGS="-DADF_PCI_API"
         AC_SUBST([ADF_CFLAGS])],
        [AC_CHECK_LIB([qat_s], [icp_sal_userIsQatAvailable],
            [SAL_CFLAGS="-DSAL_DEV_API"
             AC_SUBST([SAL_CFLAGS])],
            [AC_MSG_ERROR([QAT device API not found in libqat_s])])])

    AC_CHECK_LIB([usdm_drv_s], [qaeMemAllocNUMA], [],
        [AC_MSG_ERROR([libusdm_drv_s not found in ${ICP_ROOT}/build])])
],
[
    # System QAT installation in-tree
    AC_CHECK_HEADERS([qat/cpa.h qat/cpa_dc.h qat/qae_mem.h qat/icp_sal_poll.h qat/icp_sal_user.h],
        [AC_DEFINE([HAVE_QAT_HEADERS], [1], [QAT headers in system include path])],
        [AC_MSG_ERROR([QAT headers not found. Install qatlib-devel.])],
        [[#ifdef HAVE_QAT_CPA_H
          #include <qat/cpa.h>
          #endif]])

    # Detect QAT 5.1 (CPM5.x) driver via library symbol checks.
    CHECK_CPM5X([qat])

    AC_CHECK_LIB([qat], [cpaDcCompressData2], [],
        [AC_MSG_ERROR([libqat not found. Install qatlib.])],
        [-lusdm -lcrypto])

    AC_CHECK_LIB([qat], [icp_adf_get_numDevices],
        [ADF_CFLAGS="-DADF_PCI_API"
         AC_SUBST([ADF_CFLAGS])],
        [AC_CHECK_LIB([qat], [icp_sal_userIsQatAvailable],
            [SAL_CFLAGS="-DSAL_DEV_API"
             AC_SUBST([SAL_CFLAGS])],
            [AC_MSG_ERROR([QAT device API not found])],
            [-lusdm -lcrypto])])

    AC_CHECK_LIB([usdm], [qaeMemAllocNUMA], [],
        [AC_MSG_ERROR([libusdm not found. Install qatlib.])])
])

# Compiler flags
CFLAGS="${CFLAGS} -Wall -Werror -std=gnu99 -pedantic -fstack-protector-strong -fPIE -fPIC -fno-delete-null-pointer-checks -fwrapv"
AS_IF([test "x${CC}" = "xgcc"],
    [CFLAGS="${CFLAGS} -fno-strict-overflow"])

LDFLAGS="${LDFLAGS} -fstack-protector-strong -fPIC -pie -z relro -z now -Wl,-z,noexecstack"

# Optional features
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug], [Enable debug mode])],
    [debug=true
     CFLAGS="${CFLAGS} -g -DQATZIP_DEBUG -O0"],
    [debug=false
     CFLAGS="${CFLAGS} -O2"
     AS_IF([test "x${CC}" = "xgcc"],
         [CFLAGS="${CFLAGS} -D_FORTIFY_SOURCE=2"])])
AM_CONDITIONAL([QATZIP_DEBUG_AC], [test "x${debug}" = "xtrue"])

AC_ARG_WITH([debug_file],
    [AS_HELP_STRING([--with-debug_file=PATH], [Debug log file path])],
    [AS_IF([test "$withval" = "yes"],
        [AC_MSG_ERROR([--with-debug_file requires a path])],
        [CFLAGS="${CFLAGS} -DQZ_DEBUG_FILE=$withval"])])

AC_ARG_ENABLE([symbol],
    [AS_HELP_STRING([--enable-symbol], [Include debug symbols])],
    [symbol=true
     CFLAGS="${CFLAGS} -g"],
    [symbol=false])
AM_CONDITIONAL([QATZIP_SYMBOL_AC], [test "x${symbol}" = "xtrue"])

AC_ARG_ENABLE([pthread_barrier],
    [AS_HELP_STRING([--enable-pthread-barrier], [Enable pthread_barrier])],
    [pthread_barrier=true
     CFLAGS="${CFLAGS} -DENABLE_THREAD_BARRIER"],
    [pthread_barrier=false])
AM_CONDITIONAL([QATZIP_PTHREAD_BARRIER_AC], [test "x${pthread_barrier}" = "xtrue"])

AC_ARG_ENABLE([lz4s_postprocessing],
    [AS_HELP_STRING([--enable-lz4s-postprocessing], [Enable LZ4S/ZSTD post-processing])],
    [lz4s_postprocessing=true
     SAVE_CFLAGS="${CFLAGS}"
     CFLAGS=
     AC_CHECK_HEADER([zstd.h])
     AC_CHECK_LIB([zstd], [ZSTD_compressCCtx])
     CFLAGS="${SAVE_CFLAGS}"
     ZSTD_LIBADD="-lpthread -Bstatic -lzstd"
     AC_SUBST([ZSTD_LIBADD])],
    [lz4s_postprocessing=false])
AM_CONDITIONAL([QATZIP_LZ4S_POSTPROCESS_AC], [test "x${lz4s_postprocessing}" = "xtrue"])

# Output
AC_CONFIG_FILES([
    Makefile
    qatzip.spec
    qatzip.pc
    src/Makefile
    test/Makefile
    utils/Makefile
])
AC_OUTPUT
