/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2021-2023 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#ifndef _UTILS_QZSTD_H
#define _UTILS_QZSTD_H

#define ZSTD_STATIC_LINKING_ONLY
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <qatzip.h>
#include <zstd.h>
#include <zstd_errors.h>
#include <lz4.h>
#include <qz_utils.h>
#ifdef HAVE_QAT_HEADERS
#include <qat/cpa_dc.h>
#else
#include <cpa_dc.h>
#endif
#include <qatzip_internal.h>

/*  For qzstd app, there are three types of return errors.
*   1. The errors are from top layer software stack
*       e.g read/write file and app input parameter error.
*   2. The errors are from qatzip lib.
*       e.g QZ_DUPLICATE or QZ_NOSW_NO_HW, error code defined
*       in qatzip.h
*   3. The errors come from zstd lib.
*       e.g ZSTD_ErrorCode list. error code defined in zstd_error.h
*
*   Note:
*       Because different type of error may have the same error code
*       value, To avoid app return value confusion in shell. Use
*       QZ_ERROR function to print error type and error code.
*/

// app return status
#define QZSTD_OK 0
#define QZSTD_ERROR 1

// Error type for log capture.
#define QZSTD_ERROR_TYPE    "[QZSTD_ERROR]"
#define QZ_ERROR_TYPE       "[QZ_LIB_ERROR]"
#define ZSTD_ERROR_TYPE     "[ZSTD_LIB_ERROR]"

#define KB                  (1024)
#define MB                  (KB * KB)

#define ZSTD_SEQUENCES_SIZE (1024 * 32)

#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

typedef uint8_t BYTE;
typedef uint16_t U16;
typedef int16_t S16;
typedef uint32_t U32;
typedef int32_t S32;
typedef uint64_t U64;
typedef int64_t S64;

#define QATZIP_MAX_HW_SZ (512 * KB)
#define ZSRC_BUFF_LEN (512 * MB)
#define MAX_BLOCK_SIZE (128 * KB)

#define DECODE_ZSTD_ERROR_CODE(error_code)                          \
        ZSTD_getErrorString(ZSTD_getErrorCode((size_t)error_code))

void QzstdDisplayStats(double time, off_t insize, off_t outsize,
                       int is_compress);

void qzstd_help();

void decLz4Block(unsigned char *lz4s, int lz4sSize, ZSTD_Sequence *zstdSeqs,
                 unsigned int *seq_offset);

int qzZstdGetDefaults(QzSessionParamsLZ4S_T *defaults);

int compressFile(int in_file, int out_file);

int decompressFile(int in_file, int out_file);

int zstdCallBack(void *external, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len, int *ExtStatus);

int getLz4FrameHeaderSz();

int getLz4BlkHeaderSz();

int getLZ4FooterSz();

int getContentSize(unsigned char *const ptr);

unsigned int getBlockSize(unsigned char *const ptr);

#endif
