/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2024 Intel Corporation. All rights reserved.
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
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include "qatzip.h"

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

typedef struct QzRingHeadTail_S {
    uint32_t head;
    uint32_t tail;
} QzRingHeadTail_T;

typedef struct QzRing_S {
    uint32_t size;           /**< Size of ring. */
    uint32_t mask;           /**< Mask (size-1) of ring. */
    uint32_t capacity;       /**< Usable size of ring */

    void **elems;

    QzRingHeadTail_T prod;
    QzRingHeadTail_T cons;
} QzRing_T;

QzRing_T *QzRingCreate(int size);
void QzRingFree(QzRing_T *ring);
void QzClearRing(QzRing_T *ring);
int QzRingProduceEnQueue(QzRing_T *ring, void *obj, int is_single_producer);
void *QzRingConsumeDequeue(QzRing_T *ring, int is_single_consumer);

extern void initDebugLock(void);
extern void dumpThreadInfo(void);
extern void insertThread(unsigned int th_id,
                         Serv_T serv_type,
                         Engine_T engine_type);

/*  QATzip log API
 *  could change the log level on runtime
 */
void logMessage(QzLogLevel_T level, const char *file, int line,
                const char *format, ...);

#define LOG(level, ...) logMessage(level, __FILE__, __LINE__, __VA_ARGS__)
#define QZ_PRINT(...) LOG(LOG_NONE, __VA_ARGS__)
#define QZ_ERROR(...) LOG(LOG_ERROR, __VA_ARGS__)
#define QZ_WARN(...) LOG(LOG_WARNING, __VA_ARGS__)
#define QZ_INFO(...) LOG(LOG_INFO, __VA_ARGS__)
#define QZ_DEBUG(...) LOG(LOG_DEBUG1, __VA_ARGS__)
#define QZ_TEST(...) LOG(LOG_DEBUG2, __VA_ARGS__)
#define QZ_MEM_PRINT(...) LOG(LOG_DEBUG3, __VA_ARGS__)

#ifdef ENABLE_TESTLOG
#define QZ_TESTLOG(debuglevel, Readable, tag, ...) { \
    FILE *fd = debuglevel > 1 ? stdout : stderr; \
    fprintf(fd, "Tag: %s; ", tag); \
    if (Readable) { \
        fprintf(fd, "Time: %s %s; Location: %s->%s->%d; ", \
                __DATE__, __TIME__, __FILE__, __func__, __LINE__); \
    } else { \
        struct timespec ts = { 0 }; \
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts); \
        fprintf(fd, "Time: %ld.%06lds; ", ts.tv_sec, ts.tv_nsec / 1000); \
    } \
    fprintf(fd, "%s", "Info: "); \
    fprintf(fd, __VA_ARGS__); \
    fprintf(fd, " \n"); \
}
#endif


#endif
