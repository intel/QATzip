/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
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

/**
 *****************************************************************************
 * @file qz_utils.h
 *
 * @defgroup qatzip debug API
 *
 * @description
 *      These functions specify the API for debug operations.
 *
 * @remarks
 *
 *
 *****************************************************************************/

#ifndef _QZ_UTILS_H_
#define _QZ_UTILS_H_

#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>

typedef enum SERV_E {
    COMPRESSION = 0,
    DECOMPRESSION
} Serv_T;

typedef enum ENGINE_E {
    HW = 0,
    SW
} Engine_T;

typedef struct ThreadList_S {
    unsigned int thread_id;
    unsigned int comp_hw_count;
    unsigned int comp_sw_count;
    unsigned int decomp_hw_count;
    unsigned int decomp_sw_count;
    struct ThreadList_S *next;
} ThreadList_T;

typedef struct QatThread_S {
    ThreadList_T *comp_th_list;
    unsigned int num_comp_th;
    pthread_mutex_t comp_lock;
    ThreadList_T *decomp_th_list;
    unsigned int num_decomp_th;
    pthread_mutex_t decomp_lock;
} QatThread_T;

extern void initDebugLock(void);
extern void dumpThreadInfo(void);
extern void insertThread(unsigned int th_id,
                         Serv_T serv_type,
                         Engine_T engine_type);

#ifdef QATZIP_DEBUG
static inline void QZ_DEBUG(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}
#else
#define QZ_DEBUG(...)
#endif

static inline void QZ_PRINT(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}

static inline void QZ_ERROR(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#endif
