################################################################
#   BSD LICENSE
#
#   Copyright(c) 2007-2022 Intel Corporation. All rights reserved.
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

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([qatzip], [1.0.9], [])
AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability foreign subdir-objects tar-pax])

AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS

AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])

AC_SUBST([LIBQATZIP_VERSION], [3:0:0])
# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_AR
AC_PATH_TOOL(PKGCONFIG, pkg-config)

LT_PREREQ([2.4])
LT_INIT

# Checks for typedefs, structures, and compiler characteristics.
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


# Checks for system header files.
AC_CHECK_HEADERS([stdio.h stdarg.h fcntl.h limits.h memory.h \
    unistd.h stdint.h stdlib.h string.h sys/ioctl.h \
    sys/time.h sys/types.h sys/stat.h unistd.h utime.h \
    pthread.h], ,
    [AC_MSG_ERROR([not found header file])]
)

# Checks for library functions.
AC_CHECK_FUNCS([atexit getcwd gettimeofday \
    memmove memset mkdir munmap \
    realpath strdup strerror strrchr \
    strtol strtoul utime fork malloc mmap], ,
    [AC_MSG_ERROR([not found function])]
)

# Checks for pthread.
AC_CHECK_LIB([pthread], [pthread_create], ,
    [AC_MSG_ERROR([not found pthread_create in -lpthread])])

#check for zlib.
ZLIB_V_MIN="1.2.7"
AC_CHECK_HEADER([zlib.h],
    [
        ZLIB_PATH=`whereis zlib.h | awk '{print $2}'`
        ZLIB_V=`grep ^'#define ZLIB_VERSION' ${ZLIB_PATH} |\
                awk '{print $3}'| sed 's/"//g'`
        AC_MSG_CHECKING([zlib version ${ZLIB_V}])
        AS_VERSION_COMPARE(${ZLIB_V}, ${ZLIB_VERSION},
            [AC_MSG_ERROR([zlib version ${ZLIB_V} < ${ZLIB_V_MIN}])],
            [AC_MSG_RESULT([yes])],
            [AC_MSG_RESULT([yes])])
    ],
    [AC_MSG_ERROR([not found zlib.h])]
)

AC_CHECK_LIB([z], [deflate], , AC_MSG_ERROR([not found deflate in -lz]))
AC_CHECK_LIB([z], [inflate], , AC_MSG_ERROR([not found inflate in -lz]))

#check for lz4 lib
AC_CHECK_HEADER([lz4.h],,[AC_MSG_ERROR([not found lz4.h])])
AC_CHECK_HEADER([lz4frame.h],,[AC_MSG_ERROR([not found lz4frame.h])])
AC_CHECK_LIB([lz4], [LZ4F_compressBegin], ,AC_MSG_ERROR([not found LZ4F_compressBegin in -llz4]))

##check for xxhash lib
#AC_CHECK_HEADER([xxhash.h],,[AC_MSG_ERROR([not found xxhash.h])])
#AC_CHECK_LIB([xxhash], [XXH32_update], ,AC_MSG_ERROR([not found XXH32_update in -lxxhash]))

#check for qat header file.
QAT_HEADER=
AS_IF([test ! -z "${ICP_ROOT}"],,[ICP_ROOT="/opt/intel/QAT/quickassist"])
AC_MSG_CHECKING([qat header files from ${ICP_ROOT}])
ICP_INCLUDE_CFLAGS=
ICP_LDDFLAGS=
AS_IF([test ! -e ${ICP_ROOT}/quickassist/include/cpa.h ||
       test ! -e ${ICP_ROOT}/quickassist/include/dc/cpa_dc.h ||
       test ! -e ${ICP_ROOT}/quickassist/utilities/libusdm_drv/qae_mem.h ||
       test ! -e ${ICP_ROOT}/quickassist/lookaside/access_layer/include/icp_sal_poll.h ||
       test ! -e ${ICP_ROOT}/quickassist/lookaside/access_layer/include/icp_sal_user.h],
    [
        AC_MSG_RESULT([no]);
    ],
    [
        AC_MSG_RESULT([yes])
        ICP_INCLUDE_CFLAGS=" -I${ICP_ROOT}/quickassist/include \
                             -I${ICP_ROOT}/quickassist/include/dc/ \
                             -I${ICP_ROOT}/quickassist/utilities/libusdm_drv/ \
                             -I${ICP_ROOT}/quickassist/lookaside/access_layer/include/ "
        ICP_LDFLAGS=" -Wl,-rpath,${ICP_ROOT}/build/ -L${ICP_ROOT}/build/"
        QAT_HEADER=1
    ]
)
AS_IF([test -z "${QAT_HEADER}"],
    [
        AC_CHECK_HEADERS(
            [qat/cpa.h qat/cpa_dc.h qat/qae_mem.h qat/icp_sal_poll.h qat/icp_sal_user.h],
            [
                QAT_HEADER=$(find /usr -name cpa.h | xargs dirname)
                ICP_INCLUDE_CFLAGS=" -I${QAT_HEADER}"
                QAT_HEADER=1
            ],
            [AC_MSG_ERROR([not found qat header files])],
            [[#ifdef HAVE_QAT_CPA_H
              # include <qat/cpa.h>
              #endif
            ]]
        )
    ]
)
AC_SUBST(ICP_INCLUDE_CFLAGS)
AC_SUBST(ICP_LDFLAGS)

#check for qat lib.
QATLIB_NAME=""
QATLIB_FLAGS=""
SAVE_CFLAGS=${CFLAGS}
SAVE_LDFLAGS=${LDFLAGS}
CFLAGS="${ICP_INCLUDE_CFLAGS}"
LDFLAGS="${ICP_LDFLAGS}"
AC_CHECK_LIB(qat_s, cpaDcCompressData2,
    [
        QATLIB_NAME="qat_s"
        QATLIB_FLAGS="-lqat_s"
    ],
    [
        AC_CHECK_LIB(qat, cpaDcCompressData2,
            [QATLIB_NAME="qat"
             QATLIB_FLAGS="-lqat"],
            [AC_MSG_ERROR([not found cpaDcCompressData2 in -lqat_s/qat])]
        )
    ]
)
AC_SUBST(QATLIB_FLAGS)

#check for icp_adf_get_numDevices/icp_sal_userIsQatAvailable
AC_CHECK_LIB(${QATLIB_NAME}, icp_adf_get_numDevices,
    [ADF_CFLAGS="-DADF_PCI_API"
     AC_SUBST(ADF_CFLAGS)],
    [
        AC_CHECK_LIB(${QATLIB_NAME},icp_sal_userIsQatAvailable,
            [SAL_CFLAGS="-DSAL_DEV_API"
             AC_SUBST(SAL_CFLAGS)],
            [AC_MSG_ERROR([Not found function icp_sal_userIsQatAvailable/icp_adf_get_numDevices])]
        )
    ]
)

#check for usdm lib
USDMLIB_FLAGS=""
AC_CHECK_LIB(usdm_drv_s, qaeMemAllocNUMA,
    [USDMLIB_FLAGS="-lusdm_drv_s"],
    [
        AC_CHECK_LIB(usdm, qaeMemAllocNUMA,
            [USDMLIB_FLAGS="-lusdm"],
            [AC_MSG_RESULT([no]);
             AC_MSG_ERROR([not found qaeMemAllocNUMA in -lusdm/usdm_drv_s])]
        )
    ]
)
AC_SUBST(USDMLIB_FLAGS)
CFLAGS=${SAVE_CFLAGS}
LDFLAGS=${SAVE_LDFLAGS}

#check for compile flags
AC_MSG_CHECKING([cflags])
AS_IF([test "x${CFLAGS}" = "x"],
    [
        CFLAGS=' -Wall -Werror -std=gnu99 -pedantic \
                 -fstack-protector -fPIE -fPIC -fno-strict-overflow \
                 -fno-delete-null-pointer-checks -fwrapv'
    ],
    [
        CFLAGS+=' -Wall -Werror -std=gnu99 -pedantic \
                  -fstack-protector -fPIE -fPIC -fno-strict-overflow \
                  -fno-delete-null-pointer-checks -fwrapv'
    ]
)
AC_MSG_RESULT([yes])

#check for compile flags
AC_MSG_CHECKING([ldflags])
AS_IF([test "x${LDFLAGS}" = "x"],
    [
        LDFLAGS=' -fstack-protector -fPIC -pie -z relro -z now -Wl,-z,noexecstack'
    ],
    [
        LDFLAGS+=' -fstack-protector -fPIC -pie -z relro -z now -Wl,-z,noexecstack'
    ]
)
AC_MSG_RESULT([yes])

#Enable debug mode
AC_ARG_ENABLE(debug,
    AS_HELP_STRING([--enable-debug], [Enable debug mode]),
    [
        debug=true
        CFLAGS+=' -g -DQATZIP_DEBUG -O0'
    ]      ,
    [
        debug=false
        CFLAGS+=' -O2 -D_FORTIFY_SOURCE=2'
    ]
)
AM_CONDITIONAL([QATZIP_DEBUG_AC], [test x$debug = xtrue])

#Enable qatzip symbol
AC_ARG_ENABLE(symbol,
    AS_HELP_STRING([--enable-symbol], [Enable qatzip symbol]),
    [
        symbol=true
        CFLAGS+=' -g'
    ],
    [
        symbol=false
    ]
)
AM_CONDITIONAL([QATZIP_SYMBOL_AC], [test x$symbol = xtrue])

#Enable qatzip pthread_barrier
AC_ARG_ENABLE(pthread_barrier,
    AS_HELP_STRING([--enable-pthread-barrier], [Enable pthread_barrier]),
    [
        pthread_barrier=true
        CFLAGS+=' -DENABLE_THREAD_BARRIER'
    ],
    [
        pthread_barrier=false
    ]
)
AM_CONDITIONAL([QATZIP_PTHREAD_BARRIER_AC], [test x$pthread_barrier = xtrue])

#Enable lz4s-postprocessing
AC_ARG_ENABLE(lz4s_postprocessing,
    AS_HELP_STRING([--enable-lz4s-postprocessing], [Enable lz4s and zstd post processing]),
    [
        lz4s_postprocessing=true
        #check for zstd lib
	SAVE_CFLAGS=${CFLAGS}
	CFLAGS=
        AC_CHECK_HEADER([zstd.h])
        AC_CHECK_LIB([zstd], [ZSTD_compressCCtx])
	CFLAGS=${SAVE_CFLAGS}
        ZSTD_LIBADD='-lpthread -Bstatic -lzstd'
	AC_SUBST(ZSTD_LIBADD)

    ],
    [
        lz4s_postprocessing=false
    ]
)
AM_CONDITIONAL([QATZIP_LZ4S_POSTPROCESS_AC], [test x$lz4s_postprocessing = xtrue])

#wiht ICP_ROOT
AC_ARG_WITH([ICP_ROOT],
    AS_HELP_STRING([--with-ICP_ROOT=PATH],[Used to link Cpa library]),
    AS_IF(
        [test "$withval" = "yes"],
        AC_MSG_ERROR([--with-ICP_ROOT=PATH requires a PATH]),
        [ICP_ROOT="$withval"]
    )
)

AC_CONFIG_FILES([Makefile
                 qatzip.spec
                 src/Makefile
                 test/Makefile
                 utils/Makefile])
AC_OUTPUT
