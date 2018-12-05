/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2018 Intel Corporation. All rights reserved.
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


#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

/* QAT headers */
#include <cpa.h>
#include <cpa_dc.h>

#include <qatzip.h>
#include <qatzip_internal.h>
#include <qz_utils.h>
#include <qae_mem.h>

#define QZ_FMT_NAME         "QZ"
#define GZIP_FMT_NAME       "GZIP"
#define MAX_FMT_NAME        8
#define MAX_NUMA_NODE       8

#define ARRAY_LEN(arr)      (sizeof(arr) / sizeof((arr)[0]))
#define INTER_SZ(src_sz)    (2 * (src_sz))
#define DEST_SZ(src_sz)     (((9 * (src_sz)) / 8) + 1024)
#define KB                  (1024)
#define MB                  (KB * KB)

typedef void *(QzThdOps)(void *);

typedef enum {
    UNKNOWN,
    QZ,
    GZIP,
    FMT_NUM
} QzFormatId_T;

typedef struct QzFormat_S {
    char fmt_name[MAX_FMT_NAME];
    QzFormatId_T fmt;
} QzFormat_T;

QzFormat_T g_format_list[] = {
    {QZ_FMT_NAME,   QZ},
    {GZIP_FMT_NAME, GZIP}
};

typedef struct QzBlock_S {
    QzFormatId_T fmt;
    unsigned int size;
    struct QzBlock_S *next;
} QzBlock_T;

typedef enum {
    COMP = 0,
    DECOMP,
    BOTH
} ServiceType_T;

typedef struct CPUCore_S {
    int seq;
    int used;
} CPUCore_T;

typedef struct NUMANode_S {
    int num_cores;
    CPUCore_T *core;
} NUMANode_T;

typedef struct {
    long thd_id;
    ServiceType_T service;
    int count;
    int verify_data;
    int debug;
    int src_sz;
    int comp_out_sz;
    int decomp_out_sz;
    unsigned char *src;
    unsigned char *comp_out;
    unsigned char *decomp_out;
    int gen_data;
    QzSessionParams_T *params;
    QzThdOps *ops;
    QzBlock_T *blks;
    int init_engine_disabled;
    int init_sess_disabled;
} TestArg_T;

const unsigned int USDM_ALLOC_MAX_SZ = (2 * MB - 5 * KB);
const unsigned int DEFAULT_BUF_SZ    = 256 * KB;
const unsigned int QATZIP_MAX_HW_SZ  = 512 * KB;

QzSession_T g_session1;
QzSession_T g_session_th[100];
QzSessionParams_T g_params_th;

static pthread_mutex_t g_lock_print = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cond_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_start_cond = PTHREAD_COND_INITIALIZER;
static int g_ready_to_start;
static int g_ready_thread_count;
char *g_input_file_name = NULL;

static struct timeval g_timers[100][100];
static struct timeval g_timer_start;
extern void dumpAllCounters();

QzBlock_T *parseFormatOption(char *buf)
{
    char *str = buf, *sub_str = NULL;
    char *delim = "/", *sub_delim = ":";
    char *token, *sub_token;
    char *saveptr, *sub_saveptr;

    int i, j, fmt_idx;
    unsigned int fmt_found = 0;
    QzBlock_T *blk = NULL;
    QzBlock_T *head, *prev, *r;
    unsigned int list_len = sizeof(g_format_list) / sizeof(QzFormat_T);

    head = malloc(sizeof(QzBlock_T));
    assert(NULL != head);
    head->next = NULL;
    prev = head;

    for (i = 1; ; i++, str = NULL) {
        token = strtok_r(str, delim, &saveptr);
        if (NULL == token) {
            break;
        }
        QZ_DEBUG("String[%d]: %s\n", i, token);

        fmt_found = 0;
        blk = NULL;

        for (j = 1, sub_str = token; ; j++, sub_str = NULL) {
            sub_token = strtok_r(sub_str, sub_delim, &sub_saveptr);
            if (NULL == sub_token) {
                break;
            }
            QZ_DEBUG(" -[%d]-> %s\n", j, sub_token);

            if (fmt_found) {
                blk->size = atoi(sub_token);
                break;
            }

            char *tmp = sub_token;
            while (*tmp) {
                *tmp = GET_LOWER_8BITS(toupper(*tmp));
                tmp++;
            }

            for (fmt_idx = 0; fmt_idx < list_len; fmt_idx++) {
                if (0 == strcmp(sub_token, g_format_list[fmt_idx].fmt_name)) {
                    blk = malloc(sizeof(QzBlock_T));
                    assert(NULL != blk);

                    blk->fmt = g_format_list[fmt_idx].fmt;
                    blk->next = NULL;
                    prev->next = blk;
                    fmt_found = 1;
                    break;
                }
            }
        }

        if (NULL != blk) {
            prev = blk;
        }
    }

    blk = head->next;
    i = 1;
    while (blk) {
        QZ_PRINT("[INFO] Block%d:  format -%8s, \tsize - %d\n",
                 i++, g_format_list[blk->fmt - 1].fmt_name, blk->size);
        blk = blk->next;
    }

    if (NULL == head->next) {
        r = head->next;
        free(head);
    } else {
        r = head;
    }
    return r;
}

static void genRandomData(uint8_t *data, size_t size)
{
    size_t i, j;
    char c;
    uint8_t *ptr = data;

    while (ptr < (data + size)) {
        j = rand() % 100;
        c = GET_LOWER_8BITS((rand() % 65 + 90));
        for (i = (size_t)0; i < j; i++) {
            *ptr = c;
            ptr++;
            if (ptr >= (data + size)) {
                break;
            }
        }
    }
}

static void sigInt(int sig)
{
    dumpAllCounters();
    _exit(1);
}

static void timeCheck(int i, long tid)
{
    gettimeofday(&g_timers[i][tid], NULL);
}

#ifdef TESTMAIN_DUMP_TIMERS
static void dumpTimers(int tid)
{
    int i;
    unsigned long long start, local, diff;
    start = (g_timer_start.tv_sec * 1000000) + g_timer_start.tv_usec;
    if (0 == tid) {
        QZ_PRINT("[ts]: %lld\n", start);
    }

    QZ_PRINT("[%5.5d]", tid);
    for (i = 0; i < 15; i++) {
        local = (g_timers[i][tid].tv_sec * 1000000) + g_timers[i][tid].tv_usec;
        if (local > 0) {
            diff = local - start;
            QZ_PRINT(" %lld", diff);
        } else {
            QZ_PRINT(" -");
        }
    }
    QZ_PRINT("\n");
}
#endif

static void dumpInputData(size_t size, uint8_t *data)
{
    int fd;
    ssize_t ulen;
    char temp_file[] = "QATZip_Input_XXXXXX";

    if (0 == size || NULL == data)
        return;

    fd = mkstemp(temp_file);
    if (-1 == fd) {
        QZ_ERROR("Creat dump file Failed\n");
        return;
    }

    ulen = write(fd, data, size);
    if (ulen != (ssize_t) size) {
        QZ_ERROR("Creat dump file Failed\n");
        return;
    }
    close(fd);
}

static void dumpOutputData(size_t size, uint8_t *data, char *filename)
{
    int fd = 0;
    ssize_t ulen;
    char *filename_ptr = NULL;
    char *output_filename = NULL;
    char tmp_filename[] = "QATZip_Output_XXXXXX";
    unsigned int filename_len = 0;
    const unsigned int suffix_len = 3;
    const char *suffix[QZ_FMT_NUM] = {"df", "gz", "gz"};

    if (0 == size || NULL == data)
        return;

    if (NULL == filename) {
        filename_len = sizeof(tmp_filename);
        filename_ptr = tmp_filename;
    } else {
        filename_len = strlen(filename);
        filename_ptr = filename;
    }

    filename_len = (filename_len + suffix_len + 7) / 8 * 8;
    output_filename = (char *) calloc(1, filename_len);
    if (NULL == output_filename) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }
    snprintf(output_filename, filename_len, "%s.%s",
             filename_ptr, suffix[g_params_th.data_fmt]);

    if (NULL == filename) {
        fd = mkstemps(output_filename, suffix_len);
    } else {
        fd = open(output_filename, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
    }

    if (-1 == fd) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }

    ulen = write(fd, data, size);
    if (ulen != (ssize_t) size) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }

done:
    free(output_filename);
    if (fd >= 0) {
        close(fd);
    }
}


static void dumpDecompressedData(size_t size, uint8_t *data, char *filename)
{
    int fd = 0;
    ssize_t ulen;
    char *filename_ptr = NULL;
    char *output_filename = NULL;
    unsigned int filename_len = 0;
    const char *suffix = ".decomp";
    const unsigned int suffix_len = strlen(suffix);

    if (0 == size || NULL == data || NULL == filename)
        return;

    filename_len = strlen(filename);
    filename_ptr = filename;

    filename_len = (filename_len + suffix_len + 1 + 7) / 8 * 8;
    output_filename = (char *) calloc(1, filename_len);
    if (NULL == output_filename) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }
    snprintf(output_filename, filename_len, "%s%s",
             filename_ptr, suffix);

    fd = open(output_filename,
              O_RDWR | O_CREAT | O_APPEND,
              S_IRUSR | S_IRGRP | S_IROTH);

    if (-1 == fd) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }

    ulen = write(fd, data, size);
    if (ulen != (ssize_t) size) {
        QZ_ERROR("Creat dump file Failed\n");
        goto done;
    }

done:
    free(output_filename);
    if (fd >= 0) {
        close(fd);
    }
}

void *qzDecompressSwQz(void *arg)
{
    int rc, k;
    unsigned char *src = NULL, *comp_out = NULL;
    unsigned char *decomp_sw_out = NULL, *decomp_qz_out = NULL;
    size_t src_sz, comp_out_sz, decomp_sw_out_sz, decomp_qz_out_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    const size_t org_src_sz = ((TestArg_T *)arg)->src_sz;
    const size_t org_comp_out_sz = ((TestArg_T *)arg)->comp_out_sz;
    const long tid = ((TestArg_T *)arg)->thd_id;
    const int verify_data = 1;
    const int count = ((TestArg_T *)arg)->count;

    QzSessionParams_T cus_params;
    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto done;
    }

    src_sz = org_src_sz;
    comp_out_sz = org_comp_out_sz;
    decomp_sw_out_sz = org_src_sz;
    decomp_qz_out_sz = org_src_sz;

    QZ_DEBUG("Hello from qzDecompressSwQz tid=%ld, count=%d, service=2, "
             "verify_data=%d\n", tid, count, verify_data);

    src = qzMalloc(src_sz, 0, PINNED_MEM);
    comp_out = qzMalloc(comp_out_sz, 0, PINNED_MEM);
    decomp_sw_out = qzMalloc(decomp_sw_out_sz, 0, PINNED_MEM);
    decomp_qz_out = qzMalloc(decomp_qz_out_sz, 0, PINNED_MEM);

    if (!src || !comp_out || !decomp_sw_out || !decomp_qz_out) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    QZ_DEBUG("Gen Data...\n");
    genRandomData(src, src_sz);

    // Start the testing
    for (k = 0; k < count; k++) {
        (void)gettimeofday(&ts, NULL);
        comp_out_sz = org_src_sz;
        //Compress 1st
        {
            //Set default hwBufferSize to 64KB
            cus_params.hw_buff_sz = 64 * 1024;
            if (qzSetDefaults(&cus_params) != QZ_OK) {
                QZ_ERROR("Err: set params fail with incorrect hw_buff_sz %d.\n",
                         cus_params.hw_buff_sz);
                goto done;
            }

            QZ_DEBUG("thread %ld before Compressed %d bytes into %d\n", tid,
                     src_sz, comp_out_sz);

            unsigned int last = 0;
            rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), comp_out,
                            (uint32_t *)(&comp_out_sz), last);
            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
                goto done;
            }

            if (src_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                         org_src_sz);
                goto done;
            }
            QZ_DEBUG("thread %ld after Compressed %d bytes into %d\n", tid, src_sz,
                     comp_out_sz);
        }

        //Decompress SW
        {
            cus_params.hw_buff_sz = 32 * 1024; //32KB
            if (qzSetDefaults(&cus_params) != QZ_OK) {
                QZ_ERROR("Err: set params fail with incorrect hw_buff_sz %d.\n",
                         cus_params.hw_buff_sz);
                goto done;
            }

            QZ_DEBUG("thread %ld before Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_sw_out_sz);

            unsigned int tmp_comp_out_sz = GET_LOWER_32BITS(comp_out_sz);
            rc = qzDecompress(&g_session_th[tid], comp_out, (uint32_t *)(&tmp_comp_out_sz),
                              decomp_sw_out, (uint32_t *)(&decomp_sw_out_sz));
            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
                goto done;
            }

            if (decomp_sw_out_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Decompression decomp_out_sz: %d != org_src_sz: %d \n!",
                         decomp_sw_out_sz, org_src_sz);
                goto done;
            }
            QZ_DEBUG("thread %ld after SW Decompressed %d bytes into %d\n", tid,
                     comp_out_sz, decomp_sw_out_sz);
        }

        //Decompress QAT
        {
            //Reset default hwBufferSize to 64KB
            cus_params.hw_buff_sz = 64 * 1024;
            if (qzSetDefaults(&cus_params) != QZ_OK) {
                QZ_ERROR("Err: set params fail with incorrect hw_buff_sz %d.\n",
                         cus_params.hw_buff_sz);
                goto done;
            }

            QZ_DEBUG("thread %ld before Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_qz_out_sz);

            unsigned int tmp_comp_out_sz = GET_LOWER_32BITS(comp_out_sz);
            rc = qzDecompress(&g_session_th[tid], comp_out, (uint32_t *)&tmp_comp_out_sz,
                              decomp_qz_out, (uint32_t *)(&decomp_qz_out_sz));
            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
                goto done;
            }

            if (decomp_qz_out_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Decompression decomp_out_sz: %d != org_src_sz: %d \n!",
                         decomp_qz_out_sz, org_src_sz);
                goto done;
            }
            QZ_DEBUG("thread %ld after QZ Decompressed %d bytes into %d\n", tid,
                     comp_out_sz, decomp_qz_out_sz);
        }

        (void)gettimeofday(&te, NULL);

        {
            QZ_DEBUG("verify data..\n");
            if (memcmp(src, decomp_sw_out, org_src_sz)) {
                QZ_ERROR("ERROR: SW Decompression FAILED on thread %ld with size: %d \n!", tid,
                         src_sz);
                goto done;
            }

            if (memcmp(src, decomp_qz_out, org_src_sz)) {
                QZ_ERROR("ERROR: QZip Decompression FAILED on thread %ld with size: %d \n!",
                         tid, src_sz);
                goto done;
            }

            QZ_DEBUG("reset data..\n");
            memset(comp_out, 0, comp_out_sz);
            memset(decomp_sw_out, 0, decomp_sw_out_sz);
            memset(decomp_qz_out, 0, decomp_qz_out_sz);
        }

        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }

    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = org_src_sz * 8;// bits
    rate *= 2;
    rate /= 1024;
    rate *= count;
    rate /= 1024 * 1024; // gigbits
    rate /= sec;// Gbps
    rc = pthread_mutex_lock(&g_lock_print);
    assert(0 == rc);
    QZ_PRINT("[INFO] srv=BOTH, tid=%ld, verify=%d, count=%d, msec=%lld, "
             "bytes=%d, %Lf Gbps", tid, verify_data, count, el_m, org_src_sz, rate);
    QZ_PRINT(", input_len=%d, comp_len=%d, ratio=%f%%",
             org_src_sz, comp_out_sz,
             ((double)comp_out_sz / (double)org_src_sz) * 100);
    QZ_PRINT(", comp_len=%d, sw_decomp_len=%d",
             comp_out_sz, decomp_sw_out_sz);
    QZ_PRINT(", comp_len=%d, qz_decomp_len=%d",
             comp_out_sz, decomp_qz_out_sz);
    QZ_PRINT("\n");
    rc = pthread_mutex_unlock(&g_lock_print);
    assert(0 == rc);

done:
    qzFree(src);
    qzFree(comp_out);
    qzFree(decomp_sw_out);
    qzFree(decomp_qz_out);
    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressDecompressWithFormatOption(void *arg)
{
    int rc = 0, k;
    unsigned char *src, *comp_out, *decomp_out;
    size_t src_sz, comp_out_sz, decomp_out_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    const size_t org_src_sz = ((TestArg_T *)arg)->src_sz;
    const size_t org_comp_out_sz = ((TestArg_T *)arg)->comp_out_sz;
    const long tid = ((TestArg_T *)arg)->thd_id;
    const int verify_data = 1;
    const int count = ((TestArg_T *)arg)->count;
    const int gen_data = ((TestArg_T *)arg)->gen_data;
    QzBlock_T *head, *blk;

    head = ((TestArg_T *)arg)->blks;
    if (head == NULL) {
        pthread_exit((void *)"No Input -F options or phrase options failed\n");
    }
    blk = head->next;
    if (blk == NULL) {
        pthread_exit((void *)"No Input -F options or phrase options failed\n");
    }

    src_sz = org_src_sz;
    comp_out_sz = org_comp_out_sz;
    decomp_out_sz = org_src_sz;

    QZ_DEBUG("Hello from qzCompressDecompressWithFormatOption tid=%ld, count=%d, service=2, "
             "verify_data=%d\n", tid, count, verify_data);

    if (!((TestArg_T *)arg)->init_engine_disabled) {
        rc = qzInit(&g_session_th[tid], ((TestArg_T *)arg)->params->sw_backup);
        if (rc != QZ_OK && rc != QZ_DUPLICATE) {
            pthread_exit((void *)"qzInit failed");
        }
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    if (!((TestArg_T *)arg)->init_sess_disabled) {
        rc = qzSetupSession(&g_session_th[tid], ((TestArg_T *)arg)->params);
        if (rc != QZ_OK &&
            rc != QZ_NO_INST_ATTACH &&
            rc != QZ_NONE) {
            pthread_exit((void *)"qzSetupSession failed");
        }
    }
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    if (gen_data) {
        src = qzMalloc(src_sz, 0, PINNED_MEM);
        comp_out = qzMalloc(comp_out_sz, 0, PINNED_MEM);
        decomp_out = qzMalloc(decomp_out_sz, 0, PINNED_MEM);
    } else {
        src = ((TestArg_T *)arg)->src;
        comp_out = ((TestArg_T *)arg)->comp_out;
        decomp_out = ((TestArg_T *)arg)->decomp_out;
    }

    if (!src || !comp_out || !decomp_out) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    // Start the testing
    for (k = 0; k < count; k++) {
        (void)gettimeofday(&ts, NULL);

        //Compress 1st
        {
            comp_out_sz = org_comp_out_sz;

            QZ_DEBUG("thread %ld before Compressed %d bytes into %d\n", tid,
                     src_sz, comp_out_sz);

            unsigned int remaining = GET_LOWER_32BITS(src_sz), tmp_src_sz = 0, last = 0;
            unsigned int tmp_comp_out_sz = GET_LOWER_32BITS(comp_out_sz);
            unsigned int comp_available_out = GET_LOWER_32BITS(comp_out_sz);
            unsigned char *tmp_src = src, *tmp_comp_out = comp_out;

            comp_out_sz = (size_t)0;
            while (remaining) {
                if (remaining > blk->size) {
                    tmp_src_sz = blk->size;
                    last = 0;
                } else {
                    tmp_src_sz = remaining;
                    last = 1;
                }

                tmp_comp_out_sz = comp_available_out;

                if (QZ == blk->fmt) {
                    rc = qzCompress(&g_session_th[tid], tmp_src, &tmp_src_sz,
                                    tmp_comp_out, &tmp_comp_out_sz, last);
                } else {
                    rc = qzSWCompress(&g_session_th[tid], tmp_src, &tmp_src_sz,
                                      tmp_comp_out, &tmp_comp_out_sz, last);
                }

                tmp_src += tmp_src_sz;
                tmp_comp_out += tmp_comp_out_sz;
                comp_out_sz += tmp_comp_out_sz;
                comp_available_out -= tmp_comp_out_sz;
                remaining -= tmp_src_sz;

                QZ_DEBUG("[Thead%d] Compress: format is %4s, remaining %d, tmp_src_sz is %d\n",
                         tid,
                         g_format_list[blk->fmt - 1].fmt_name, remaining, tmp_src_sz);

                blk = (blk->next) ? blk->next : head->next;
            }

            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
                goto done;
            }

            if (src_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                         org_src_sz);
                goto done;
            }
            QZ_DEBUG("thread %ld after Compressed %d bytes into %d\n", tid, src_sz,
                     comp_out_sz);
        }

        //Decompress
        {
            QZ_DEBUG("thread %ld before Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_out_sz);

            unsigned int remaining = GET_LOWER_32BITS(comp_out_sz);
            unsigned int decomp_available_out = GET_LOWER_32BITS(decomp_out_sz);
            unsigned char *tmp_comp_out = comp_out, *tmp_decomp_out = decomp_out;
            unsigned int tmp_comp_out_sz, tmp_decomp_out_sz, decomp_out_sz = 0;

            while (remaining) {
                tmp_comp_out_sz = remaining;
                tmp_decomp_out_sz = decomp_available_out;

                rc = qzDecompress(&g_session_th[tid], tmp_comp_out, &tmp_comp_out_sz,
                                  tmp_decomp_out, &tmp_decomp_out_sz);
                if (rc != QZ_OK) {
                    QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
                    goto done;
                }

                tmp_comp_out += tmp_comp_out_sz;
                tmp_decomp_out += tmp_decomp_out_sz;
                decomp_out_sz += tmp_decomp_out_sz;
                remaining -= tmp_comp_out_sz;
                decomp_available_out -= tmp_decomp_out_sz;

                QZ_DEBUG("[Thead%d] Decompress: remaining %d, tmp_decomp_out_sz is %d\n",
                         tid, remaining, tmp_decomp_out_sz);
            }

            if (decomp_out_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Decompression decomp_out_sz: %d != org_src_sz: %d \n!",
                         decomp_out_sz, org_src_sz);
                goto done;
            }
            QZ_DEBUG("thread %ld after Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_out_sz);
        }

        (void)gettimeofday(&te, NULL);

        QZ_DEBUG("verify data..\n");
        if (memcmp(src, decomp_out, org_src_sz)) {
            QZ_ERROR("ERROR: Decompression FAILED on thread %ld with size: %d \n!", tid,
                     src_sz);
            goto done;
        }

        QZ_DEBUG("reset data..\n");
        memset(comp_out, 0, (size_t)comp_out_sz);
        memset(decomp_out, 0, (size_t)decomp_out_sz);
        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }

    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = org_src_sz * 8;// bits
    rate *= 2;
    rate /= 1024;
    rate *= count;
    rate /= 1024 * 1024; // gigbits
    rate /= sec;// Gbps
    rc = pthread_mutex_lock(&g_lock_print);
    assert(0 == rc);
    QZ_PRINT("[INFO] srv=BOTH, tid=%ld, verify=%d, count=%d, msec=%lld, "
             "bytes=%d, %Lf Gbps", tid, verify_data, count, el_m, org_src_sz, rate);
    QZ_PRINT(", input_len=%d, comp_len=%d, ratio=%f%%",
             org_src_sz, comp_out_sz,
             ((double)comp_out_sz / (double)org_src_sz) * 100);
    QZ_PRINT(", comp_len=%d, decomp_len=%d",
             comp_out_sz, decomp_out_sz);
    QZ_PRINT("\n");
    rc = pthread_mutex_unlock(&g_lock_print);
    assert(0 == rc);

done:
    if (gen_data) {
        qzFree(src);
        qzFree(comp_out);
        qzFree(decomp_out);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzSetupParamFuncTest(void *arg)
{
    QzSessionParams_T def_params;
    QzSessionParams_T new_params;
    QzSessionParams_T cus_params;
    unsigned char *src, *dest;
    size_t src_sz, dest_sz, test_dest_sz;;
    int rc;
    const long tid = ((TestArg_T *)arg)->thd_id;

    src_sz = 256 * 1024;
    test_dest_sz = dest_sz = 256 * 1024 * 2;
    src = qzMalloc(src_sz, 0, COMMON_MEM);
    dest = qzMalloc(dest_sz, 0, COMMON_MEM);

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        return NULL;
    }

    if (qzGetDefaults(&def_params) != QZ_OK ||
        qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    rc = qzInit(&g_session_th[tid], 0);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        QZ_ERROR("Err: fail to init HW with ret: %d.\n", rc);
        goto end;
    }
    rc = qzSetupSession(&g_session_th[tid], &def_params);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        QZ_ERROR("Err: fail to setup session with ret: %d\n", rc);
        goto end;
    }
    rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), dest,
                    (uint32_t *)(&test_dest_sz), 1);
    if (rc != QZ_OK) {
        QZ_ERROR("Err: fail to compress data with ret: %d\n", rc);
        goto end;
    }
    QZ_PRINT("With default params, input_len:%d, output_len:%d.\n",
             src_sz, test_dest_sz);
    test_dest_sz = dest_sz;

    // Negative Test
    cus_params.huffman_hdr = QZ_STATIC_HDR + 1;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect huffman: %d.\n",
                 cus_params.huffman_hdr);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.direction = QZ_DIR_BOTH + 1;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect direction: %d.\n",
                 cus_params.direction);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.comp_lvl = 0;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect comp_level: %d.\n",
                 cus_params.comp_lvl);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.comp_lvl = 10;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect comp_level: %d.\n",
                 cus_params.comp_lvl);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.comp_algorithm = QZ_SNAPPY;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect algorithm: %d.\n",
                 cus_params.comp_algorithm);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.sw_backup = 2;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect sw_backup: %d.\n",
                 cus_params.sw_backup);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.hw_buff_sz = 0;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect hw_buff_sz %d.\n",
                 cus_params.hw_buff_sz);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.hw_buff_sz = 1025;
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect hw_buff_sz %d.\n",
                 cus_params.hw_buff_sz);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    cus_params.hw_buff_sz = 2 * 1024 * 1024; //2M
    if (qzSetDefaults(&cus_params) != QZ_PARAMS) {
        QZ_ERROR("Err: set params should fail with incorrect hw_buff_sz %d.\n",
                 cus_params.hw_buff_sz);
        goto end;
    }

    if (qzGetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    // Positive Test
    cus_params.huffman_hdr = (QZ_HUFF_HDR_DEFAULT == QZ_DYNAMIC_HDR) ?
                             QZ_STATIC_HDR : QZ_DYNAMIC_HDR;
    if (qzSetDefaults(&cus_params) != QZ_OK) {
        QZ_ERROR("Err: fail to set defulat params.\n");
        goto end;
    }

    if (qzGetDefaults(&new_params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto end;
    }

    if (memcmp(&def_params, &new_params, sizeof(QzSessionParams_T)) == 0) {
        QZ_ERROR("Err: set default params fail.\n");
        goto end;
    }

    if (memcmp(&cus_params, &new_params, sizeof(QzSessionParams_T)) != 0) {
        QZ_ERROR("Err: set default params fail with incorrect value.\n");
        QZ_ERROR("  cus_params.huff(%d) != new_params.huff(%d).\n",
                 cus_params.huffman_hdr, new_params.huffman_hdr);
        goto end;
    }

    rc = qzSetupSession(&g_session_th[tid], &new_params);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        QZ_ERROR("Err: fail to setup session with ret: %d\n", rc);
        goto end;
    }

    rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), dest,
                    (uint32_t *)(&test_dest_sz), 1);
    if (rc != QZ_OK) {
        QZ_ERROR("Err: fail to compress data with ret: %d\n", rc);
        goto end;
    }
    QZ_ERROR("With custom params, input_len:%d, output_len:%d.\n",
             src_sz, test_dest_sz);

end:
    qzFree(src);
    qzFree(dest);
    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressAndDecompress(void *arg)
{
    int rc = -1, k;
    unsigned char *src, *comp_out, *decomp_out;
    size_t src_sz, comp_out_sz, decomp_out_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    const int org_src_sz = ((TestArg_T *)arg)->src_sz;
    const int org_comp_out_sz = ((TestArg_T *)arg)->comp_out_sz;
    const long tid = ((TestArg_T *)arg)->thd_id;
    const ServiceType_T service = ((TestArg_T *)arg)->service;
    const int verify_data = ((TestArg_T *)arg)->verify_data;
    const int count = ((TestArg_T *)arg)->count;
    const int gen_data = ((TestArg_T *)arg)->gen_data;

    src_sz = org_src_sz;
    comp_out_sz = org_comp_out_sz;
    decomp_out_sz = org_src_sz;

    QZ_DEBUG("Hello from qzCompressAndDecompress tid=%ld, count=%d, service=%d, "
             "verify_data=%d\n",
             tid, count, service, verify_data);

    if (!((TestArg_T *)arg)->init_engine_disabled) {
        rc = qzInit(&g_session_th[tid], ((TestArg_T *)arg)->params->sw_backup);
        if (rc != QZ_OK && rc != QZ_DUPLICATE && rc != QZ_NO_HW) {
            pthread_exit((void *)"qzInit failed");
        }
    }

    QZ_DEBUG("qzInit  rc = %d\n", rc);
    //timeCheck(2, tid);
    if (!((TestArg_T *)arg)->init_sess_disabled) {
        rc = qzSetupSession(&g_session_th[tid], ((TestArg_T *)arg)->params);
        if (rc != QZ_OK &&
            rc != QZ_NO_INST_ATTACH &&
            rc != QZ_NONE &&
            rc != QZ_NO_HW) {
            pthread_exit((void *)"qzSetupSession failed");
        }
    }

    //timeCheck(3, tid);
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    if (gen_data) {
        src = qzMalloc(src_sz, 0, PINNED_MEM);
        comp_out = qzMalloc(comp_out_sz, 0, PINNED_MEM);
        decomp_out = qzMalloc(decomp_out_sz, 0, PINNED_MEM);
    } else {
        src = ((TestArg_T *)arg)->src;
        comp_out = ((TestArg_T *)arg)->comp_out;
        decomp_out = ((TestArg_T *)arg)->decomp_out;
    }

    if (!src || !comp_out || !decomp_out) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }
    //timeCheck(4, tid);

    // Compress the data for testing
    if (DECOMP == service) {
        rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), comp_out,
                        (uint32_t *)(&comp_out_sz), 1);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
            dumpInputData(src_sz, src);
            goto done;
        }

        if (src_sz != org_src_sz) {
            QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                     org_src_sz);
            dumpInputData(src_sz, src);
            goto done;
        }
    }

    /* mutex lock for thread count */
    rc = pthread_mutex_lock(&g_cond_mutex);
    if (rc != 0) {
        QZ_ERROR("Failure to release Mutex Lock, status = %d\n", rc);
        goto done;
    }
    g_ready_thread_count++;
    rc = pthread_cond_signal(&g_ready_cond);
    if (rc != 0) {
        QZ_ERROR("Failure to pthread_cond_signal, status = %d\n", rc);
        goto done;
    }
    while (!g_ready_to_start) {
        rc = pthread_cond_wait(&g_start_cond, &g_cond_mutex);
        if (rc != 0) {
            QZ_ERROR("Failure to pthread_cond_wait, status = %d\n", rc);
            goto done;
        }
    }
    rc = pthread_mutex_unlock(&g_cond_mutex);
    if (rc != 0) {
        QZ_ERROR("Failure to release Mutex Lock, status = %d\n", rc);
        goto done;
    }

    // Start the testing
    for (k = 0; k < count; k++) {
        (void)gettimeofday(&ts, NULL);
        if (DECOMP != service) {
            comp_out_sz = org_comp_out_sz;
            QZ_DEBUG("thread %ld before Compressed %d bytes into %d\n", tid, src_sz,
                     comp_out_sz);
            rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), comp_out,
                            (uint32_t *)(&comp_out_sz), 1);
            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
                dumpInputData(src_sz, src);
                goto done;
            }

            if (src_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                         org_src_sz);
                dumpInputData(src_sz, src);
                goto done;
            }
            QZ_DEBUG("thread %ld after Compressed %d bytes into %d\n", tid, src_sz,
                     comp_out_sz);
        }

        if (COMP != service) {
            QZ_DEBUG("thread %ld before Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_out_sz);
            rc = qzDecompress(&g_session_th[tid], comp_out, (uint32_t *)(&comp_out_sz),
                              decomp_out, (uint32_t *)(&decomp_out_sz));
            if (rc != QZ_OK) {
                QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
                dumpInputData(src_sz, src);
                goto done;
            }

            if (decomp_out_sz != org_src_sz) {
                QZ_ERROR("ERROR: After Decompression decomp_out_sz: %d != org_src_sz: %d \n!",
                         decomp_out_sz, org_src_sz);
                dumpInputData(src_sz, src);
                goto done;
            }
            QZ_DEBUG("thread %ld after Decompressed %d bytes into %d\n", tid, comp_out_sz,
                     decomp_out_sz);
        }

        (void)gettimeofday(&te, NULL);

        if (verify_data && COMP != service) {
            QZ_DEBUG("verify data..\n");
            if (memcmp(src, decomp_out, org_src_sz)) {
                QZ_ERROR("ERROR: Decompression FAILED on thread %ld with size: %d \n!", tid,
                         src_sz);
                dumpInputData(src_sz, src);
                goto done;
            }

            if (BOTH == service) {
                QZ_DEBUG("reset data..\n");
                memset(comp_out, 0, comp_out_sz);
                memset(decomp_out, 0, decomp_out_sz);
            }
        }
        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }

    //timeCheck(5, tid);
    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = org_src_sz;
    rate /= 1024;
    rate *= 8;// Kbits
    if (BOTH == service) {
        rate *= 2;
    }
    rate *= count;
    rate /= 1024 * 1024; // gigbits
    rate /= sec;// Gbps
    if (0 != pthread_mutex_lock(&g_lock_print)) {
        goto done;
    }
    QZ_PRINT("[INFO] srv=");
    if (COMP == service) {
        QZ_PRINT("COMP");
    } else if (DECOMP == service) {
        QZ_PRINT("DECOMP");
    } else if (BOTH == service) {
        QZ_PRINT("BOTH");
    } else {
        QZ_ERROR("UNKNOWN\n");
        pthread_mutex_unlock(&g_lock_print);
        goto done;
    }
    QZ_PRINT(", tid=%ld, verify=%d, count=%d, msec=%lld, "
             "bytes=%d, %Lf Gbps",
             tid, verify_data, count, el_m, org_src_sz, rate);
    if (DECOMP != service) {
        QZ_PRINT(", input_len=%d, comp_len=%d, ratio=%f%%",
                 org_src_sz, comp_out_sz,
                 ((double)comp_out_sz / (double)org_src_sz) * 100);
    }
    if (COMP != service) {
        QZ_PRINT(", comp_len=%d, decomp_len=%d",
                 comp_out_sz, decomp_out_sz);
    }
    QZ_PRINT("\n");
    pthread_mutex_unlock(&g_lock_print);

done:
    if (gen_data) {
        qzFree(src);
        qzFree(comp_out);
        qzFree(decomp_out);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressOnPinnedMem(void *thd_arg)
{
    int rc, k;
    unsigned char *src, *dest;
    size_t src_sz, dest_sz, avail_dest_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_DEBUG("Hello from qzCompressOnPinnedMem id %d\n", tid);

    timeCheck(0, tid);
    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);
    timeCheck(1, tid);

    //set by default configurations
    rc = qzSetupSession(&g_session_th[tid], NULL);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    timeCheck(2, tid);
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    src_sz = (rand() % 256 * 1024) + 20000;
    src_sz = 256 * 1024;
    avail_dest_sz = dest_sz = 256 * 1024 * 2;

    if (gen_data) {
        src = qzMalloc(src_sz, 0, PINNED_MEM);
        dest = qzMalloc(dest_sz, 0, PINNED_MEM);
    } else {
        src = test_arg->src;
        dest = qzMalloc(dest_sz, 0, PINNED_MEM);
    }

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    timeCheck(3, tid);
    for (k = 0; k < test_arg->count; k++) {

        dest_sz = avail_dest_sz;
        (void)gettimeofday(&ts, NULL);
        rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), dest,
                        (uint32_t *)(&dest_sz), 1);
        if (rc != QZ_OK) {
            QZ_ERROR("qzCompress FAILED, return: %d", rc);
            goto done;
        }
        (void)gettimeofday(&te, NULL);
        QZ_DEBUG("Compressed %d bytes into %d\n", src_sz, dest_sz);

        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }

    timeCheck(4, tid);
    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = src_sz * 8; // bits
    rate = rate / 1000000000.0; // gigbits
    rate = rate / sec;// Gbps
    rate = rate * 1000;
    QZ_PRINT("[%ld] elasped microsec = %lld bytes = %d rate = %Lf\n",
             tid, el_m, src_sz, rate);

done:
    timeCheck(5, tid);
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzMemFuncTest(void *test_arg)
{
    int i;
    unsigned char *ptr[1000];
    unsigned char *ptr2[1000];
    unsigned success = 0;
    const long tid = ((TestArg_T *)test_arg)->thd_id;

    QZ_DEBUG("Hello from test2 thread id %ld\n", tid);

    for (i = 0; i < 1000; i++) {
        ptr[i] = qzMalloc(100000, 0, PINNED_MEM);
        ptr2[i] = qzMalloc(100000, 0, COMMON_MEM);
        if (ptr[i] == NULL || ptr2[i] == NULL) {
            QZ_ERROR("[Test2 %ld]\tptr[%d]=0x%lx\t0x%lx\n", tid, i,
                     (unsigned long)ptr[i], (unsigned long)ptr2[i]);
            break;
        }
        success++;
    }

    for (i = 0; i < success; i++) {
        qzFree(ptr[i]);
        qzFree(ptr2[i]);
    }

    for (i = 0; i < success; i++) {
        if (1 == qzMemFindAddr(ptr[i])) {
            QZ_DEBUG("[Test2 %ld]\tptr[%d]=0x%lx\tstill as pinned memory after qzFree.\n",
                     tid, i, (unsigned long)ptr[i]);
            break;
        }
    }
    pthread_exit((void *)NULL);
}

void *qzCompressOnCommonMem(void *thd_arg)
{
    int rc, k;
    unsigned char *src, *dest;
    size_t src_sz, dest_sz, avail_dest_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_DEBUG("Hello from thread id %d\n", tid);

    timeCheck(0, tid);
    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }

    QZ_DEBUG("qzInit  rc = %d\n", rc);
    timeCheck(1, tid);
    rc = qzSetupSession(&g_session_th[tid], test_arg->params);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    timeCheck(2, tid);
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    src_sz = 256 * 1024;
    avail_dest_sz = dest_sz = 256 * 1024 * 2;
    if (gen_data) {
        src = qzMalloc(src_sz, 0, COMMON_MEM);
        dest = qzMalloc(dest_sz, 0, COMMON_MEM);
    } else {
        src = test_arg->src;
        dest = qzMalloc(dest_sz, 0, COMMON_MEM);
    }

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    timeCheck(3, tid);
    for (k = 0; k < test_arg->count; k++) {
        dest_sz = avail_dest_sz;
        (void)gettimeofday(&ts, NULL);
        rc = qzCompress(&g_session_th[tid], src, (uint32_t *)(&src_sz), dest,
                        (uint32_t *)(&dest_sz), 1);
        if (rc != QZ_OK) {
            QZ_ERROR("qzCompress FAILED, return: %d", rc);
            goto done;
        }

        (void)gettimeofday(&te, NULL);
        QZ_DEBUG("Compressed %d bytes into %d\n", src_sz, dest_sz);

        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }
    timeCheck(4, tid);
    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = src_sz * 8; // bits
    rate = rate / 1000000000.0; // gigbits
    rate = rate / sec;// Gbps
    rate = rate * 1000;
    QZ_PRINT("[%ld] elasped microsec = %lld bytes = %d rate = %Lf\n",
             tid, el_m, src_sz, rate);

done:
    if (! gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    timeCheck(5, tid);
    pthread_exit((void *)NULL);
}

int qzCompressDecompressWithParams(const TestArg_T *arg,
                                   QzSessionParams_T *comp_params, QzSessionParams_T *decomp_params)
{
    int rc = -1;
    QzSession_T comp_sess = {0}, decomp_sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;

    orig_sz = comp_sz = decomp_sz = arg->src_sz;
    orig_src = qzMalloc(orig_sz, 0, PINNED_MEM);
    comp_src = qzMalloc(comp_sz, 0, PINNED_MEM);
    decomp_src = qzMalloc(orig_sz, 0, PINNED_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        return -1;
    }

    genRandomData(orig_src, orig_sz);

    /*do compress Data*/
    src_sz = orig_sz;
    if (qzSetDefaults(comp_params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    rc = qzCompress(&comp_sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    /*do decompress Data*/
    if (qzSetDefaults(decomp_params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    rc = qzDecompress(&decomp_sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
        goto done;
    }
    rc = 0;

done:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    (void)qzTeardownSession(&comp_sess);
    (void)qzTeardownSession(&decomp_sess);
    qzClose(&comp_sess);
    qzClose(&decomp_sess);
    return rc;
}

void *qzCompressStreamAndDecompress(void *arg)
{
    int rc = -1;
    QzSession_T comp_sess = {0}, decomp_sess = {0};
    QzStream_T comp_strm = {0};
    QzSessionParams_T comp_params = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, comp_sz, decomp_sz;
    unsigned int slice_sz = 0, done = 0;
    unsigned int consumed = 0, produced = 0;
    unsigned int input_left = 0, last = 0;
    unsigned int decomp_out_sz = 0;
    int org_in_sz;

    TestArg_T *test_arg = (TestArg_T *) arg;

    orig_sz = comp_sz = decomp_sz = test_arg->src_sz;
    orig_src = malloc(orig_sz);
    comp_src = malloc(comp_sz);
    decomp_src = calloc(orig_sz, 1);

    if (qzGetDefaults(&comp_params) != QZ_OK) {
        QZ_ERROR("Err: get params fail with incorrect compress params.\n");
        goto exit;
    }
    slice_sz = comp_params.hw_buff_sz / 4;

    if (NULL == orig_src ||
        NULL == comp_src ||
        NULL == decomp_src) {
        free(orig_src);
        free(comp_src);
        free(decomp_src);
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        return NULL;
    }
    comp_params.data_fmt = test_arg->params->data_fmt;
    QZ_DEBUG("*** Data Format: %d ***\n", comp_params.data_fmt);

    genRandomData(orig_src, orig_sz);

    while (!done) {
        input_left      = orig_sz - consumed;
        comp_strm.in    = orig_src + consumed;
        comp_strm.out   = comp_src + produced;
        comp_strm.in_sz = (input_left > slice_sz) ? slice_sz : input_left;
        comp_strm.out_sz =  comp_sz - produced;
        last = (((consumed + comp_strm.in_sz) == orig_sz) ? 1 : 0);

        rc = qzCompressStream(&comp_sess, &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
            goto exit;
        }

        consumed += comp_strm.in_sz;
        produced += comp_strm.out_sz;
        QZ_DEBUG("consumed is %u, in_sz is %d\n", consumed, comp_strm.in_sz);

        if (1 == last && 0 == comp_strm.pending_in && 0 == comp_strm.pending_out) {
            done = 1;
        }
    }
    decomp_out_sz = produced;
    qzEndStream(&comp_sess, &comp_strm);

    QZ_DEBUG("qzCompressStream consumed: %d produced: %d\n", consumed, produced);
    comp_sz = produced;

    rc = qzDecompress(&decomp_sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
        dumpInputData(produced, comp_src);
        dumpOutputData(decomp_sz, decomp_src, "decomp_out");
        goto exit;
    }


    if (memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Decompression FAILED with size: %d \n!", orig_sz);
        dumpInputData(orig_sz, orig_src);
        dumpOutputData(comp_sz, comp_src, "comp_out");
        dumpOutputData(decomp_sz, decomp_src, "decomp_out");
        goto exit;
    }
    QZ_DEBUG("qzDecompress Test PASS\n");

    QZ_DEBUG("*** Decompress Stream Test 1 ***\n");
    comp_sz = produced;
    done = 0;
    consumed = 0;
    produced = 0;
    memset(decomp_src, 0, orig_sz);

    while (!done) {
        input_left      = comp_sz - consumed;
        comp_strm.in    = comp_src + consumed;
        comp_strm.out   = decomp_src + produced;
        comp_strm.in_sz = (input_left > slice_sz) ? slice_sz : input_left;
        comp_strm.out_sz =  decomp_sz - produced;
        last = (comp_sz == (consumed + comp_strm.in_sz)) ? 1 : 0;
        org_in_sz = comp_strm.in_sz;

        rc = qzDecompressStream(&comp_sess, &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
            dumpOutputData(comp_sz, comp_src, "decomp_stream__input");
            goto exit;
        }

        consumed += comp_strm.in_sz;
        produced += comp_strm.out_sz;

        QZ_DEBUG("consumed: %d produced: %d input_left: %d last: %d, pending_in: %d, pending_out: %d\n",
                 consumed, produced, input_left, last, comp_strm.pending_in,
                 comp_strm.pending_out);
        if (1 == last && 0 == comp_strm.pending_in && 0 == comp_strm.pending_out &&
            org_in_sz == comp_strm.in_sz) {
            done = 1;
        }
    }

    QZ_DEBUG("Total consumed: %d produced: %d\n", consumed, produced);
    QZ_DEBUG("verify data of size %d ...\n", orig_sz);
    if (produced != orig_sz || consumed != decomp_out_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Memory compare FAILED with size: %d \n!", orig_sz);
        dumpInputData(orig_sz, orig_src);
        dumpInputData(orig_sz, decomp_src);
        goto exit;
    }
    QZ_DEBUG("*** Decompress Stream Test 1 PASS ***\n");

    if (comp_params.data_fmt != QZ_DEFLATE_GZIP_EXT) {
        goto test_2_end;
    }
    QZ_DEBUG("*** Decompress Stream Test 2 ***\n");
    done = 0;
    consumed = 0;
    produced = 0;
    memset(decomp_src, 0, orig_sz);
    QzGzH_T hdr;
    int offset = 0;

    while (!done) {
        if (QZ_OK != qzGzipHeaderExt(comp_src + offset, &hdr)) {
            QZ_ERROR("ERROR: extracting header failed\n");
            goto exit;
        }
        input_left      = comp_sz - consumed;
        comp_strm.in    = comp_src + consumed;
        comp_strm.out   = decomp_src + produced;
        comp_strm.in_sz = sizeof(QzGzH_T) + hdr.extra.qz_e.dest_sz + sizeof(StdGzF_T);
        comp_strm.out_sz =  decomp_sz - produced;
        last = 1;
        offset += comp_strm.in_sz;

        rc = qzDecompressStream(&comp_sess, &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Memcmp with return value: %d\n", rc);
            goto exit;
        }

        consumed += comp_strm.in_sz;
        produced += comp_strm.out_sz;

        if (offset == comp_sz) {
            done = 1;
        }
    }

    QZ_DEBUG("Total consumed: %d produced: %d\n", consumed, produced);
    QZ_DEBUG("verify data of size %d ...\n", orig_sz);
    if (produced != orig_sz || consumed != decomp_out_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Decompression FAILED with size: %d \n!", orig_sz);
        dumpInputData(orig_sz, orig_src);
        dumpInputData(orig_sz, decomp_src);

        goto exit;
    }
    QZ_DEBUG("*** Decompress Stream Test 2 PASS ***\n");
test_2_end:

    if (comp_params.data_fmt == QZ_DEFLATE_GZIP_EXT) {
        goto test_3_end;
    }
    QZ_DEBUG("*** Decompress Stream Test 3 ***\n");
    done = 0;
    consumed = 0;
    produced = 0;
    memset(decomp_src, 0, orig_sz);

    while (!done) {
        input_left      = comp_sz - consumed;
        comp_strm.in    = comp_src + consumed;
        comp_strm.out   = decomp_src + produced;
        comp_strm.in_sz = (input_left > 256) ? 256 : input_left;
        comp_strm.out_sz =  decomp_sz - produced;
        last = 1;
        org_in_sz = comp_strm.in_sz;

        rc = qzDecompressStream(&comp_sess, &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
            goto exit;
        }

        consumed += comp_strm.in_sz;
        produced += comp_strm.out_sz;

        QZ_DEBUG("consumed: %d produced: %d input_left: %d last: %d, pending_in: %d, pending_out: %d "
                 "org_in_sz: %d in_sz: %d\n",
                 consumed, produced, input_left, last, comp_strm.pending_in,
                 comp_strm.pending_out,
                 org_in_sz, comp_strm.in_sz);

        if (1 == last && 0 == comp_strm.pending_in && 0 == comp_strm.pending_out &&
            org_in_sz == comp_strm.in_sz && produced == orig_sz &&
            comp_sz - consumed == 0) {
            done = 1;
        }
    }

    QZ_DEBUG("Total consumed: %d produced: %d\n", consumed, produced);
    QZ_DEBUG("verify data of size %d ...\n", orig_sz);
    if (produced != orig_sz || consumed != decomp_out_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Memory compare FAILED with size: %d \n!", orig_sz);
        dumpInputData(orig_sz, orig_src);
        dumpInputData(orig_sz, decomp_src);
        goto exit;
    }
    QZ_DEBUG("*** Decompress Stream Test 3 PASS***\n");
test_3_end:

    QZ_DEBUG("*** Decompress Stream Test 4 ***\n");
    done = 0;
    consumed = 0;
    produced = 0;
    memset(decomp_src, 0, orig_sz);

    while (!done) {
        input_left      = comp_sz - consumed;
        comp_strm.in    = comp_src + consumed;
        comp_strm.out   = decomp_src + produced;
        comp_strm.in_sz = (input_left > slice_sz) ? slice_sz : input_left;
        comp_strm.out_sz =  decomp_sz - produced;
        last = (comp_sz == (consumed + comp_strm.in_sz)) ? 1 : 0;
        org_in_sz = comp_strm.in_sz;

        rc = qzDecompressStream(&comp_sess, &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("ERROR: Decompression FAILED with return value: %d\n", rc);
            goto exit;
        }

        consumed += comp_strm.in_sz;
        produced += comp_strm.out_sz;

        QZ_DEBUG("consumed: %d produced: %d input_left: %d last: %d, pending_in: %d, pending_out: %d "
                 "org_in_sz: %d in_sz: %d\n",
                 consumed, produced, input_left, last, comp_strm.pending_in,
                 comp_strm.pending_out,
                 org_in_sz, comp_strm.in_sz);

        if (1 == last && 0 == comp_strm.pending_in && 0 == comp_strm.pending_out &&
            org_in_sz == comp_strm.in_sz && produced == orig_sz &&
            comp_sz - consumed == 0) {
            done = 1;
        }
    }

    QZ_DEBUG("Total consumed: %d produced: %d\n", consumed, produced);
    QZ_DEBUG("verify data of size %d ...\n", orig_sz);
    if (produced != orig_sz || consumed != decomp_out_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Memory compare FAILED with size: %d \n!", orig_sz);
        dumpInputData(orig_sz, orig_src);
        dumpInputData(orig_sz, decomp_src);
        goto exit;
    }
    QZ_DEBUG("*** Decompress Stream Test 4 PASS***\n");

    QZ_PRINT("Compress Stream and Decompress function test: PASS\n");

exit:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    qzEndStream(&comp_sess, &comp_strm);
    (void)qzTeardownSession(&comp_sess);
    (void)qzTeardownSession(&decomp_sess);
    qzClose(&comp_sess);
    qzClose(&decomp_sess);
    return NULL;
}


void *qzCompressStreamOnCommonMem(void *thd_arg)
{
    int rc, k;
    unsigned char *src = NULL, *dest = NULL;
    unsigned int src_sz, dest_sz, avail_dest_sz;
    struct timeval ts, te;
    unsigned long long ts_m, te_m, el_m;
    long double sec, rate;
    QzStream_T comp_strm = {0};
    QzSessionParams_T params;
    unsigned int last = 0;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_DEBUG("Hello from qzCompressStreamOnCommonMem id %d\n", tid);

    timeCheck(0, tid);

    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    if (qzGetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto done;
    }
    params.strm_buff_sz = DEFAULT_BUF_SZ;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    timeCheck(1, tid);

    //set by default configurations
    rc = qzSetupSession(&g_session_th[tid], NULL);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    timeCheck(2, tid);
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    src_sz = QATZIP_MAX_HW_SZ;
    avail_dest_sz = dest_sz = QATZIP_MAX_HW_SZ;

    if (gen_data) {
        src_sz = QATZIP_MAX_HW_SZ;
        dest_sz = QATZIP_MAX_HW_SZ;
        src = qzMalloc(src_sz, 0, COMMON_MEM);
        dest = qzMalloc(dest_sz, 0, COMMON_MEM);
    } else {
        src = test_arg->src;
        src_sz = test_arg->src_sz;
        dest = test_arg->comp_out;
        dest_sz = test_arg->comp_out_sz;
    }

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    el_m = 0;
    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    timeCheck(3, tid);
    for (k = 0; k < test_arg->count; k++) {

        dest_sz = avail_dest_sz;
        (void)gettimeofday(&ts, NULL);

        comp_strm.in    = src;
        comp_strm.out   = dest;
        comp_strm.in_sz = src_sz;
        comp_strm.out_sz =  dest_sz;
        last = 1;
        rc = qzCompressStream(&g_session_th[tid], &comp_strm, last);
        qzEndStream(&g_session_th[tid], &comp_strm);

        if (rc != QZ_OK) {
            QZ_ERROR("qzCompressStream FAILED, return: %d", rc);
            goto done;
        }
        (void)gettimeofday(&te, NULL);
        QZ_DEBUG("Compressed %d bytes into %d\n", src_sz, dest_sz);

        ts_m = (ts.tv_sec * 1000000) + ts.tv_usec;
        te_m = (te.tv_sec * 1000000) + te.tv_usec;
        el_m += te_m - ts_m;
    }

    timeCheck(4, tid);
    sec = (long double)(el_m);
    sec = sec / 1000000.0;
    rate = src_sz * test_arg->count * 8; // bits
    rate = rate / 1000000000.0; // gigbits
    rate = rate / sec;// Gbps
    QZ_PRINT("[%ld] elasped microsec = %lld bytes = %d rate = %Lf Gbps\n",
             tid, el_m, src_sz, rate);

done:
    timeCheck(5, tid);
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressStreamOutput(void *thd_arg)
{
    int rc;
    unsigned char *src = NULL, *dest = NULL;
    unsigned int src_sz, dest_sz;
    QzStream_T comp_strm = {0};
    QzSessionParams_T params;
    unsigned int last = 0;
    char *filename = NULL;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_DEBUG("Hello from qzCompressStreamOutput id %d\n", tid);

    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    if (qzGetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto done;
    }
    params.strm_buff_sz = DEFAULT_BUF_SZ;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    //set by default configurations
    rc = qzSetupSession(&g_session_th[tid], NULL);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    if (gen_data) {
        src_sz = QATZIP_MAX_HW_SZ;
        dest_sz = QATZIP_MAX_HW_SZ;
        src = qzMalloc(src_sz, 0, COMMON_MEM);
        dest = qzMalloc(dest_sz, 0, COMMON_MEM);
    } else {
        src = test_arg->src;
        src_sz = test_arg->src_sz;
        dest = test_arg->comp_out;
        dest_sz = test_arg->comp_out_sz;
        filename = g_input_file_name;
    }

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    {
        comp_strm.in    = src;
        comp_strm.out   = dest;
        comp_strm.in_sz = src_sz;
        comp_strm.out_sz =  dest_sz;
        last = 1;
        rc = qzCompressStream(&g_session_th[tid], &comp_strm, last);

        if (rc != QZ_OK) {
            QZ_ERROR("qzCompressStream FAILED, return: %d", rc);
            goto done;
        }
        QZ_DEBUG("Compressed %d bytes into %d\n", src_sz, dest_sz);

        dumpOutputData(comp_strm.out_sz, comp_strm.out, filename);
        qzEndStream(&g_session_th[tid], &comp_strm);
    }

done:
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzDecompressStreamInput(void *thd_arg)
{
    int rc;
    unsigned char *src = NULL, *dest = NULL;
    unsigned int src_sz, dest_sz;
    unsigned int consumed, done;
    QzStream_T decomp_strm = {0};
    QzSessionParams_T params;
    unsigned int last = 0;
    char *filename = NULL;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_DEBUG("Hello from qzDecompressStreamInput id %d\n", tid);

    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    if (qzGetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        goto done;
    }
    params.strm_buff_sz = 1024 * 1024;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    //set by default configurations
    rc = qzSetupSession(&g_session_th[tid], NULL);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    if (gen_data) {
        QZ_ERROR("Err: No input file.\n");
        goto done;
    }

    src = test_arg->src;
    src_sz = test_arg->src_sz;
    dest = test_arg->decomp_out;
    dest_sz = test_arg->decomp_out_sz;
    consumed = 0;
    done = 0;
    filename = g_input_file_name;

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    {
        while (!done) {
            decomp_strm.in    = src + consumed;
            decomp_strm.in_sz = src_sz - consumed;
            decomp_strm.out   = dest;
            decomp_strm.out_sz =  dest_sz;
            last = 1;

            rc = qzDecompressStream(&g_session_th[tid], &decomp_strm, last);
            if (rc != QZ_OK) {
                QZ_ERROR("qzDecompressStream FAILED, return: %d\n", rc);
                goto done;
            }

            QZ_DEBUG("Decompressed %d bytes into %d\n", src_sz, dest_sz);
            dumpDecompressedData(decomp_strm.out_sz, decomp_strm.out, filename);
            consumed += decomp_strm.in_sz;

            if (src_sz == consumed && decomp_strm.pending_out == 0) {
                done = 1;
            }
        }

        qzEndStream(&g_session_th[tid], &decomp_strm);
    }

done:
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressStreamInvalidChunkSize(void *thd_arg)
{
    int rc;
    unsigned char *src = NULL, *dest = NULL;
    QzSessionParams_T params;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_PRINT("Hello from qzCompressStreamInvalidChunkSize id %d\n", tid);

    timeCheck(0, tid);

    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    qzGetDefaults(&params);
    params.strm_buff_sz = DEFAULT_BUF_SZ;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    params.hw_buff_sz = 525312;    /*513k*/
    rc = qzSetupSession(&g_session_th[tid], &params);
    if (rc != QZ_PARAMS) {
        pthread_exit((void *)
                     "qzCompressStreamInvalidChunkSize input param check failed");
    }

    params.hw_buff_sz = 100;
    rc = qzSetupSession(&g_session_th[tid], &params);
    if (rc != QZ_PARAMS) {
        pthread_exit((void *)
                     "qzCompressStreamInvalidChunkSize input param check failed");
    }
    QZ_PRINT("qzCompressStreamInvalidChunkSize : PASS\n");

done:
    timeCheck(5, tid);
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}


void *qzCompressStreamInvalidQzStreamParam(void *thd_arg)
{
    int rc;
    unsigned char *src = NULL, *dest = NULL;
    unsigned int src_sz, dest_sz;
    QzStream_T comp_strm = {0};
    QzSessionParams_T params;
    unsigned int last = 0;
    char *filename = NULL;
    TestArg_T *test_arg = (TestArg_T *)thd_arg;
    const int gen_data = test_arg->gen_data;
    const long tid = test_arg->thd_id;

    QZ_PRINT("Hello from qzCompressStreamInvalidQzStreamParam id %d\n", tid);

    rc = qzInit(&g_session_th[tid], test_arg->params->sw_backup);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        pthread_exit((void *)"qzInit failed");
    }
    QZ_DEBUG("qzInit  rc = %d\n", rc);

    qzGetDefaults(&params);
    params.strm_buff_sz = DEFAULT_BUF_SZ;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    rc = qzSetupSession(&g_session_th[tid], NULL);
    if (rc != QZ_OK && rc != QZ_NO_INST_ATTACH) {
        pthread_exit((void *)"qzSetupSession failed");
    }
    QZ_DEBUG("qzSetupSession rc = %d\n", rc);

    if (gen_data) {
        src_sz = QATZIP_MAX_HW_SZ;
        dest_sz = QATZIP_MAX_HW_SZ;
        src = qzMalloc(src_sz, 0, COMMON_MEM);
        dest = qzMalloc(dest_sz, 0, COMMON_MEM);
    } else {
        src = test_arg->src;
        src_sz = test_arg->src_sz;
        dest = test_arg->comp_out;
        dest_sz = test_arg->comp_out_sz;
        filename = g_input_file_name;
    }

    if (!src || !dest) {
        QZ_ERROR("Malloc failed\n");
        goto done;
    }

    if (gen_data) {
        QZ_DEBUG("Gen Data...\n");
        genRandomData(src, src_sz);
    }

    {
        /*case 1: set strm all params to zero*/
        last = 1;
        memset(&comp_strm, 0, sizeof(QzStream_T));
        rc = qzCompressStream(&g_session_th[tid], &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("qzCompressStream failure, return: %d\n", rc);
            goto done;
        }
        QZ_DEBUG("Compressed %d bytes into %d\n", src_sz, dest_sz);

        /*case 2: set strm in, our ptr to NULL, but in_sz, out_sz not zero*/
        memset(&comp_strm, 0, sizeof(QzStream_T));
        comp_strm.in_sz = src_sz;
        comp_strm.out_sz =  dest_sz;
        rc = qzCompressStream(&g_session_th[tid], &comp_strm, last);
        if (rc != QZ_OK) {
            QZ_ERROR("qzCompressStream failure, return: %d\n", rc);
            goto done;
        }

        dumpOutputData(comp_strm.out_sz, comp_strm.out, filename);
        qzEndStream(&g_session_th[tid], &comp_strm);
    }
    QZ_PRINT("qzCompressStreamInvalidQzStreamParam : PASS\n");

done:
    if (gen_data) {
        qzFree(src);
        qzFree(dest);
    }

    (void)qzTeardownSession(&g_session_th[tid]);
    pthread_exit((void *)NULL);
}

void *qzCompressDecompressSwQZMixed(void *arg)
{
    enum TestType_E {
        SW_SW,
        SW_QZ,
        QZ_SW,
        QZ_QZ,
        NUM_OF_TEST
    };

    struct TestParams_T {
        enum TestType_E type;
        char *name;
        QzSessionParams_T *comp_params;
        QzSessionParams_T *decomp_params;
    };

    QzSessionParams_T sw_comp_params, qz_comp_params, sw_decomp_params,
                      qz_decomp_params;
    int i = 0;

    if ((qzGetDefaults(&sw_comp_params) != QZ_OK) ||
        (qzGetDefaults(&qz_comp_params) != QZ_OK) ||
        (qzGetDefaults(&sw_decomp_params) != QZ_OK) ||
        (qzGetDefaults(&qz_decomp_params) != QZ_OK)) {
        QZ_ERROR("Err: fail to get defulat params.\n");
        return NULL;
    }

    sw_comp_params.input_sz_thrshold = 512 * 1024;
    qz_comp_params.input_sz_thrshold = 1024;
    sw_decomp_params.input_sz_thrshold = 512 * 1024;
    qz_decomp_params.input_sz_thrshold = 1024;

    struct TestParams_T test_params[NUM_OF_TEST] = {
        (struct TestParams_T){SW_SW, "SW comparess SW decompss", &sw_comp_params, &sw_decomp_params},
        (struct TestParams_T){SW_QZ, "SW comparess QZ decompss", &sw_comp_params, &qz_decomp_params},
        (struct TestParams_T){QZ_SW, "QZ comparess SW decompss", &qz_comp_params, &sw_decomp_params},
        (struct TestParams_T){QZ_QZ, "QZ comparess QZ decompss", &qz_comp_params, &qz_decomp_params},
    };

    ((TestArg_T *) arg)->src_sz = 128 * 1024; //128KB
    for (i = 0; i < NUM_OF_TEST; i++) {
        if (qzCompressDecompressWithParams(arg, test_params[i].comp_params,
                                           test_params[i].decomp_params) < 0) {
            QZ_ERROR("ERROR: HW/SW mixed function test in: %s \n", test_params[i].name);
            return NULL;
        }
    }

    QZ_PRINT("HW/SW mixed function test: PASS\n");
    return NULL;
}

int qzDecompressFailedAtUnknownGzipHeader(void)
{
    int rc;
    QzGzH_T *hdr = NULL;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;

    orig_sz = comp_sz = decomp_sz = 64 * 1024; /*64K*/
    orig_src = qzMalloc(orig_sz, 0, PINNED_MEM);
    comp_src = qzMalloc(comp_sz, 0, PINNED_MEM);
    decomp_src = qzMalloc(orig_sz, 0, PINNED_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        return -1;
    }

    genRandomData(orig_src, orig_sz);

    /*do compress Data*/
    src_sz = orig_sz;
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    /*wrap bad block*/
    hdr = (QzGzH_T *)comp_src;
    hdr->std_hdr.id1 = 0; /* id1 !=0x1f */

    /*do decompress Data*/
    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_FAIL) {
        QZ_ERROR("ERROR: Decompression success with Error GipHeader\n");
        goto done;
    }
    rc = 0;

done:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzDecompressSWFailedAtUnknownGzipBlock(void)
{
    int rc = 0;
    QzGzH_T *hdr = NULL;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;
    QzSessionParams_T params;
    unsigned int produce;

    orig_sz = comp_sz = decomp_sz = USDM_ALLOC_MAX_SZ;
    orig_src = qzMalloc(orig_sz, 0, PINNED_MEM);
    comp_src = qzMalloc(comp_sz, 0, PINNED_MEM);
    decomp_src = qzMalloc(orig_sz, 0, PINNED_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        goto done;
    }

    rc = qzInit(&sess, 1);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        QZ_ERROR("qzInit for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    qzGetDefaults(&params);
    params.hw_buff_sz = QZ_HW_BUFF_MAX_SZ;
    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("qzSetupSession for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    /*compress Data*/
    src_sz = orig_sz;
    genRandomData(orig_src, orig_sz);
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    hdr = (QzGzH_T *)comp_src;
    produce = hdr->extra.qz_e.dest_sz;

    /*wrap unknown block*/
    memset(hdr + qzGzipHeaderSz(), 0, (size_t)produce);

    /*Scenario1: produce > DEST_SZ(params.hw_buff_sz)*/
    /*set minimum hw size 16K*/
    params.hw_buff_sz = 16 * 1024;
    while (produce < DEST_SZ(params.hw_buff_sz)) {
        params.hw_buff_sz *= 2;
    }

    QZ_DEBUG("produce: %u, DEST_SZ(hw_sz): %d\n", produce,
             DEST_SZ(params.hw_buff_sz));
    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: qzSetupSession FAILED with return value: %d\n", rc);
        goto done;
    }

    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_FAIL && rc != QZ_DATA_ERROR) {
        QZ_ERROR("ERROR: Decompression success with Error Unknown Gzip block\n");
        goto done;
    }
    rc = 0;

done:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzDecompressHWFailedAtUnknownGzipBlock(void)
{
    int rc = 0;
    QzGzH_T *hdr = NULL;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;
    uint32_t produce;

    orig_sz = comp_sz = decomp_sz = USDM_ALLOC_MAX_SZ;
    orig_src = qzMalloc(orig_sz, 0, PINNED_MEM);
    comp_src = qzMalloc(comp_sz, 0, PINNED_MEM);
    decomp_src = qzMalloc(orig_sz, 0, PINNED_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        goto done;
    }

    /*compress Data*/
    src_sz = orig_sz;
    genRandomData(orig_src, orig_sz);
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    hdr = (QzGzH_T *)comp_src;
    produce = hdr->extra.qz_e.dest_sz;

    /*wrap unknown block*/
    memset(hdr + qzGzipHeaderSz(), 'Q', (size_t)produce);

    /*do Decompress Data*/
    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_FAIL) {
        QZ_ERROR("ERROR: Decompression success with Error Unknown Gzip block\n");
        goto done;
    }
    rc = 0;

done:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzDecompressForceSW(void)
{
    int rc = 0;
    QzGzH_T *hdr = NULL;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;
    QzSessionParams_T params;
    uint32_t consume, produce;

    orig_sz = comp_sz = decomp_sz = USDM_ALLOC_MAX_SZ;
    orig_src = qzMalloc(orig_sz, 0, PINNED_MEM);
    comp_src = qzMalloc(comp_sz, 0, PINNED_MEM);
    decomp_src = qzMalloc(orig_sz, 0, PINNED_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        goto done;
    }

    rc = qzInit(&sess, 1);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        QZ_ERROR("qzInit for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    qzGetDefaults(&params);
    params.hw_buff_sz = QZ_HW_BUFF_MAX_SZ;
    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("qzSetupSession for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    /*compress Data*/
    src_sz = orig_sz;
    genRandomData(orig_src, orig_sz);
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    hdr = (QzGzH_T *)comp_src;
    consume = hdr->extra.qz_e.src_sz;
    produce = hdr->extra.qz_e.dest_sz;

    /*Scenario1: produce > DEST_SZ(params.hw_buff_sz)*/
    /*set minimum hw size 2K*/
    params.hw_buff_sz = 2 * 1024;
    while (produce < DEST_SZ(params.hw_buff_sz)) {
        params.hw_buff_sz *= 2;
    }

    QZ_DEBUG("produce: %d, DEST_SZ(hw_sz): %d\n", produce,
             DEST_SZ(params.hw_buff_sz));
    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: qzSetupSession FAILED with return value: %d\n", rc);
        goto done;
    }

    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK          ||
        decomp_sz != orig_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Decompression success with Error GipHeader\n");
        goto done;
    }

    (void)qzTeardownSession(&sess);
    /*Scenario2: consume > qzSess->sess_params.hw_buff_sz*/
    /*set maximum hw size 1M*/
    params.hw_buff_sz = QATZIP_MAX_HW_SZ;
    while (consume <  params.hw_buff_sz) {
        params.hw_buff_sz /= 2;
    }
    QZ_DEBUG("consume: %d, hw_sz: %d\n", consume, params.hw_buff_sz);

    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: qzSetupSession FAILED with return value: %d\n", rc);
        goto done;
    }

    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK          ||
        decomp_sz != orig_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Decompression success with Error GipHeader\n");
        goto done;
    }

done:
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzDecompressStandalone(void)
{
    int rc = 0;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;
    QzSessionParams_T params;

    orig_sz = src_sz = comp_sz = decomp_sz = 4 * KB;
    orig_src = calloc(1, orig_sz);
    comp_src = calloc(1, comp_sz);
    decomp_src = calloc(1, decomp_sz);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        goto done;
    }

    if (qzGetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: get params fail with incorrect compress params.\n");
        goto done;
    }

    /*compression output 4 blocks*/
    params.hw_buff_sz = 1 * KB;
    if (qzSetDefaults(&params) != QZ_OK) {
        QZ_ERROR("Err: set params fail with incorrect compress params.\n");
        goto done;
    }

    genRandomData(orig_src, orig_sz);
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK          ||
        decomp_sz != orig_sz ||
        memcmp(orig_src, decomp_src, orig_sz)) {
        QZ_ERROR("ERROR: Decompression failed, orig_sc:%u != decomp_src:%u\n", orig_sz,
                 decomp_src);
        goto done;
    }
    rc = 0;

done:
    free(orig_src);
    free(comp_src);
    free(decomp_src);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzCompressFailedAtBufferOverflow(void)
{
    int rc = QZ_BUF_ERROR;
    QzSession_T sess = {0};
    uint8_t *src, *low_comp, *comp, *low_decomp;
    size_t orig_sz = 64 * KB, low_comp_sz = 1 * KB, comp_sz = orig_sz,
           low_decomp_sz = 1 * KB;

    src = calloc(1, orig_sz);
    low_comp = calloc(1, low_comp_sz);
    comp = calloc(1, comp_sz);
    low_decomp = calloc(1, low_decomp_sz);

    if (NULL == src || NULL == low_comp || NULL == comp || NULL == low_decomp) {
        goto done;
    }

    genRandomData(src, orig_sz);

    rc = qzCompress(&sess, src, (uint32_t *)(&orig_sz), comp,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: Compression fail with overflow buffer length:rc = %d\n", rc);
        goto done;
    }
    rc = 0;

    rc = qzDecompress(&sess, comp, (uint32_t *)(&comp_sz), low_decomp,
                      (uint32_t *)(&low_decomp_sz));
    if (rc != QZ_BUF_ERROR) {
        QZ_ERROR("ERROR: Decompression success with overflow buffer length:rc = %d\n",
                 rc);
        goto done;
    }
    rc = 0;

    orig_sz = 64 * KB;
    rc = qzCompress(&sess, src, (uint32_t *)(&orig_sz), low_comp,
                    (uint32_t *)(&low_comp_sz), 1);
    if (rc != QZ_BUF_ERROR) {
        QZ_ERROR("ERROR: Compression success with overflow buffer length:rc = %d\n",
                 rc);
        goto done;
    }
    rc = 0;

done:
    free(src);
    free(comp);
    free(low_comp);
    free(low_decomp);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int doQzCompressCrcCheck(size_t orig_sz)
{
    int rc = QZ_BUF_ERROR;
    QzSession_T sess = {0};
    uint8_t *src, *comp;
    size_t comp_sz = orig_sz;
    unsigned long crc_sw = 0, crc_qz = 0;

    src = calloc(1, orig_sz);
    comp = calloc(1, comp_sz);

    if (NULL == src || NULL == comp) {
        goto done;
    }

    genRandomData(src, orig_sz);
    crc_sw = crc32(crc_sw, src, GET_LOWER_32BITS(orig_sz));

    rc = qzCompressCrc(&sess, src, (uint32_t *)(&orig_sz), comp,
                       (uint32_t *)(&comp_sz), 1, &crc_qz);
    if (rc != QZ_OK) {
        QZ_ERROR("ERROR: Compression fail with overflow buffer length:rc = %d\n", rc);
        goto done;
    }

    if (crc_sw != crc_qz) {
        QZ_ERROR("ERROR: Compression fail on CRC check: SW CRC %u, QATzip CRC %u\n",
                 crc_sw, crc_qz);
        rc = QZ_FAIL;
    }

done:
    free(src);
    free(comp);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzCompressCrcCheck(void)
{
    size_t test_sz_qz = (64 * KB), test_sz_sw = (QZ_COMP_THRESHOLD_DEFAULT - 1);
    size_t test_sz[] = {test_sz_qz, test_sz_sw};
    int i, rc = 0;

    for (i = 0; i < ARRAY_LEN(test_sz); i++) {
        rc = doQzCompressCrcCheck(test_sz[i]);
        if (QZ_OK != rc) {
            goto done;
        }
    }

done:
    return rc;
}

int qzCompressSWL9DecompressHW(void)
{
    int rc = 0;
    QzSession_T sess = {0};
    uint8_t *orig_src, *comp_src, *decomp_src;
    size_t orig_sz, src_sz, comp_sz, decomp_sz;
    QzSessionParams_T params;

    orig_sz = comp_sz = decomp_sz = 4 * MB;
    orig_src = qzMalloc(orig_sz, 0, COMMON_MEM);
    comp_src = qzMalloc(comp_sz, 0, COMMON_MEM);
    decomp_src = qzMalloc(orig_sz, 0, COMMON_MEM);

    if (orig_src == NULL ||
        comp_src == NULL ||
        decomp_src == NULL) {
        QZ_ERROR("Malloc Memory for testing %s error\n", __func__);
        goto done;
    }

    rc = qzInit(&sess, 1);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        QZ_ERROR("qzInit for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    qzGetDefaults(&params);
    params.comp_lvl = 9;
    rc = qzSetupSession(&sess, &params);
    if (rc != QZ_OK) {
        QZ_ERROR("qzSetupSession for testing %s error, return: %d\n", __func__, rc);
        goto done;
    }

    /*compress data by software*/
    src_sz = orig_sz;
    genRandomData(orig_src, orig_sz);
    rc = qzCompress(&sess, orig_src, (uint32_t *)(&src_sz), comp_src,
                    (uint32_t *)(&comp_sz), 1);
    if (rc != QZ_OK || src_sz != orig_sz) {
        QZ_ERROR("ERROR: Compression FAILED with return value: %d\n", rc);
        QZ_ERROR("ERROR: After Compression src_sz: %d != org_src_sz: %d \n!", src_sz,
                 orig_sz);
        goto done;
    }

    /*decompress data by hardware*/
    rc = qzDecompress(&sess, comp_src, (uint32_t *)(&comp_sz), decomp_src,
                      (uint32_t *)(&decomp_sz));
    if (rc != QZ_OK          ||
        decomp_sz != orig_sz ||
        memcmp(orig_src, decomp_src, (size_t)orig_sz)) {
        QZ_ERROR("ERROR: Decompression success with Error GipHeader\n");
        goto done;
    }

done:
    qzFree(orig_src);
    qzFree(comp_src);
    qzFree(decomp_src);
    (void)qzTeardownSession(&sess);
    qzClose(&sess);
    return rc;
}

int qzFuncTests(void)
{
    int i = 0;

    int (*sw_failover_func_tests[])(void) = {
        qzDecompressFailedAtUnknownGzipHeader,
        qzDecompressSWFailedAtUnknownGzipBlock,
        qzDecompressHWFailedAtUnknownGzipBlock,
        qzDecompressStandalone,
        qzDecompressForceSW,
        qzCompressSWL9DecompressHW,
    };

    for (i = 0; i < ARRAY_LEN(sw_failover_func_tests); i++) {
        if (sw_failover_func_tests[i]()) {
            QZ_ERROR("SWFailOverFunc[%d] : failed\n", i);
            return -1;
        }
    }
    QZ_PRINT("SWFailOverFunc test : Passed\n");

    int (*qz_compress_negative_tests[])(void) = {
        qzCompressFailedAtBufferOverflow,
    };

    for (i = 0; i < ARRAY_LEN(qz_compress_negative_tests); i++) {
        if (qz_compress_negative_tests[i]()) {
            QZ_ERROR("qzCompressNegative[%d] : failed\n", i);
            return -1;
        }
    }
    QZ_PRINT("qzCompressNegative test : Passed\n");


    int (*qz_compress_crc_positive[])(void) = {
        qzCompressCrcCheck,
    };

    for (i = 0; i < ARRAY_LEN(qz_compress_crc_positive); i++) {
        if (qz_compress_crc_positive[i]()) {
            QZ_ERROR("qz_compress_crc_positive[%d] : failed\n", i);
            return -1;
        }
    }
    QZ_PRINT("qz_compress_crc_positive test : Passed\n");
    return 0;
}

#define USAGE_STRING                                                            \
    "Usage: %s [options]\n"                                                     \
    "\n"                                                                        \
    "Required options:\n"                                                       \
    "\n"                                                                        \
    "    -m testMode           1 test memcpy feature\n"                         \
    "                          2 test Memory\n"                                 \
    "                          3 test comp/decomp by default parameters\n"      \
    "                          4 test comp/decomp by configurable parameters\n" \
    "                          5 test comp/decomp by format parameters\n"       \
    "                          6 test set default configurable parameters\n"    \
    "\n"                                                                        \
    "Optional options can be:\n"                                                \
    "\n"                                                                        \
    "    -i inputfile          input source file\n"                             \
    "                          default by random generate data \n"              \
    "    -t thread_count       maximum forks permitted in the current thread\n" \
    "                          0 means no forking permitted. \n"                \
    "    -l loop count         default is 2\n"                                  \
    "    -v                    verify, disabled by default\n"                   \
    "    -e init engine        enable | disable. enabled by default\n"          \
    "    -s init session       enable | disable. enabled by default\n"          \
    "    -A comp_algorithm     deflate | snappy | lz4\n"                        \
    "    -B                    swBack, disabled by default\n"                   \
    "    -C hw_buff_sz         default 64K\n"                                   \
    "    -D direction          comp | decomp | both\n"                          \
    "    -F format             [comp format]:[orig data size]/...\n"            \
    "    -L comp_lvl           1 - 9\n"                                         \
    "    -O data_fmt           deflate | gzip | gzipext\n"                      \
    "    -T huffmanType        static | dynamic\n"                              \
    "    -P poll_sleep         0 means disable thread polling\n"                \
    "    -r req_cnt_thrshold   max inflight request num, default is 16\n"       \
    "    -h                    Print this help message\n"

void qzPrintUsageAndExit(char *progName)
{
    QZ_ERROR("%s %s", USAGE_STRING, progName);
    exit(-1);
}


int main(int argc, char *argv[])
{
    int rc = 0, ret = 0;
    int i = 0;
    void *p_rc;
    int thread_count = 1, test = 0;
    ServiceType_T service = COMP;
    pthread_t threads[100];
    TestArg_T test_arg[100];
    struct sigaction s1;

    unsigned char *input_buf = NULL;
    unsigned int input_buf_len = QATZIP_MAX_HW_SZ;

    s1.sa_handler = sigInt;
    sigemptyset(&s1.sa_mask);
    s1.sa_flags = 0;
    sigaction(SIGINT, &s1, NULL);

    const char *optstring = "m:t:A:C:D:F:L:T:P:i:l:e:s:r:B:O:vh";
    int opt = 0, loop_cnt = 2, verify = 0;
    int disable_init_engine = 0, disable_init_session = 0;
    char *stop = NULL;
    QzThdOps *qzThdOps = NULL;
    QzBlock_T  *qzBlocks = NULL;

    if (qzGetDefaults(&g_params_th) != QZ_OK) {
        return -1;
    }

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
        case 'm': // test case
            test = GET_LOWER_32BITS(strtol(optarg, &stop, 0));
            if (*stop != '\0' || errno) {
                QZ_ERROR("Error input: %s\n", optarg);
                return -1;
            }
            break;
        case 't':
            thread_count = GET_LOWER_32BITS(strtol(optarg, &stop, 0));
            g_params_th.max_forks = thread_count;
            if (*stop != '\0' || errno || g_params_th.max_forks > 100) {
                QZ_ERROR("Error thread count arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'A':
            if (strcmp(optarg, "deflate") == 0) {
                g_params_th.comp_algorithm = QZ_DEFLATE;
            } else if (strcmp(optarg, "snappy") == 0) {
                g_params_th.comp_algorithm = QZ_SNAPPY;
            } else if (strcmp(optarg, "lz4") == 0) {
                g_params_th.comp_algorithm = QZ_LZ4;
            } else {
                QZ_ERROR("Error service arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'O':
            if (strcmp(optarg, "deflate") == 0) {
                g_params_th.data_fmt = QZ_DEFLATE_RAW;
            } else if (strcmp(optarg, "gzip") == 0) {
                g_params_th.data_fmt = QZ_DEFLATE_GZIP;
            } else if (strcmp(optarg, "gzipext") == 0) {
                g_params_th.data_fmt = QZ_DEFLATE_GZIP_EXT;
            } else {
                QZ_ERROR("Error service arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'B':
            g_params_th.sw_backup = 1;
            break;
        case 'C':
            g_params_th.hw_buff_sz = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (*stop != '\0' || errno || g_params_th.hw_buff_sz > USDM_ALLOC_MAX_SZ / 2) {
                QZ_ERROR("Error chunkSize arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'D':
            if (strcmp(optarg, "comp") == 0) {
                service = QZ_DIR_COMPRESS;
            } else if (strcmp(optarg, "decomp") == 0) {
                service = QZ_DIR_DECOMPRESS;
            } else if (strcmp(optarg, "both") == 0) {
                service = QZ_DIR_BOTH;
            } else {
                QZ_ERROR("Error service arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'F':
            qzBlocks = parseFormatOption(optarg);
            if (NULL == qzBlocks) {
                QZ_ERROR("Error format arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'L':
            g_params_th.comp_lvl = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (*stop != '\0' || errno ||  \
                g_params_th.comp_lvl > 9 || g_params_th.comp_lvl <= 0) {
                QZ_ERROR("Error compLevel arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'T':
            if (strcmp(optarg, "static") == 0) {
                g_params_th.huffman_hdr = QZ_STATIC_HDR;
            } else if (strcmp(optarg, "dynamic") == 0) {
                g_params_th.huffman_hdr = QZ_DYNAMIC_HDR;
            } else {
                QZ_ERROR("Error huffman arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'P':
            g_params_th.poll_sleep = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (*stop != '\0' || errno) {
                QZ_ERROR("Error sleepInterval arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'l':
            loop_cnt = GET_LOWER_32BITS(strtol(optarg, &stop, 0));
            if (*stop != '\0' || errno) {
                QZ_ERROR("Error loop count arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'v':
            verify = 1;
            break;
        case 'i':
            g_input_file_name = optarg;
            break;
        case 'e':
            if (strcmp(optarg, "enable") == 0) {
                disable_init_engine = 0;
            } else if (strcmp(optarg, "disable") == 0) {
                disable_init_engine = 1;
            } else {
                QZ_ERROR("Error init qat engine arg: %s\n", optarg);
                return -1;
            }
            break;
        case 's':
            if (strcmp(optarg, "enable") == 0) {
                disable_init_session = 0;
            } else if (strcmp(optarg, "disable") == 0) {
                disable_init_session = 1;
            } else {
                QZ_ERROR("Error init qat session arg: %s\n", optarg);
                return -1;
            }
            break;
        case 'r':
            g_params_th.req_cnt_thrshold = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (*stop != '\0' || errno) {
                QZ_ERROR("Error req_cnt_thrshold arg: %s\n", optarg);
                return -1;
            }
            break;
        default:
            qzPrintUsageAndExit(argv[0]);
        }
    }

    if (test == 0) {
        qzPrintUsageAndExit(argv[0]);
    }

    if (qzSetDefaults(&g_params_th) != QZ_OK) {
        QZ_ERROR("Err: fail to set default session paramsters.\n");
        return -1;
    }

    if (g_input_file_name != NULL) {
        FILE *file;
        struct stat file_state;

        if (stat(g_input_file_name, &file_state)) {
            QZ_ERROR("ERROR: fail to get stat of file %s\n", g_input_file_name);
            return -1;
        }

        input_buf_len = GET_LOWER_32BITS((file_state.st_size > QATZIP_MAX_HW_SZ ?
                                          QATZIP_MAX_HW_SZ : file_state.st_size));
        if (test == 4 || test == 10 || test == 11 || test == 12) {
            input_buf_len = GET_LOWER_32BITS(file_state.st_size);
        }
        input_buf = qzMalloc(input_buf_len, 0, COMMON_MEM);
        if (!input_buf) {
            QZ_ERROR("ERROR: fail to alloc %d bytes of memory with qzMalloc\n",
                     input_buf_len);
            return -1;
        }

        file = fopen(g_input_file_name, "rb");
        if (!file) {
            QZ_ERROR("ERROR: fail to read file %s\n", g_input_file_name);
            return -1;
        }

        if (fread(input_buf, 1, input_buf_len, file) != input_buf_len) {
            QZ_ERROR("ERROR: fail to read file %s\n", g_input_file_name);
            return -1;
        } else {
            QZ_ERROR("Read %d bytes from file %s\n", input_buf_len, g_input_file_name);
        }
        fclose(file);
    }

    switch (test) {
    case 1:
        qzThdOps = qzCompressOnCommonMem;
        break;
    case 2:
        qzThdOps = qzMemFuncTest;
        break;
    case 3:
        qzThdOps = qzCompressOnPinnedMem;
        break;
    case 4:
        qzThdOps = qzCompressAndDecompress;
        break;
    case 5:
        qzThdOps = qzCompressDecompressWithFormatOption;
        break;
    case 6:
        qzThdOps = qzSetupParamFuncTest;
        break;
    case 7:
        qzThdOps = qzDecompressSwQz;
        break;
    case 8:
        qzThdOps = qzCompressDecompressSwQZMixed;
        break;
    case 9:
        qzThdOps = qzCompressStreamAndDecompress;
        break;
    case 10:
        qzThdOps = qzCompressStreamOnCommonMem;
        break;
    case 11:
        qzThdOps = qzCompressStreamOutput;
        break;
    case 12:
        qzThdOps = qzDecompressStreamInput;
        break;
    case 13:
        qzThdOps = qzCompressStreamInvalidChunkSize;
        break;
    case 14:
        qzThdOps = qzCompressStreamInvalidQzStreamParam;
        break;
    case 15:
        return qzFuncTests();
    default:
        goto done;
    }

    for (i = 0; i < thread_count; i++) {
        test_arg[i].thd_id = i;
        test_arg[i].service = service;
        test_arg[i].verify_data = verify;
        test_arg[i].debug = 0;
        test_arg[i].count = loop_cnt;
        test_arg[i].src_sz = GET_LOWER_32BITS(input_buf_len);
        test_arg[i].comp_out_sz = test_arg[i].src_sz * 2;
        test_arg[i].src = input_buf;
        test_arg[i].comp_out = qzMalloc(test_arg[i].comp_out_sz, 0, COMMON_MEM);
        test_arg[i].decomp_out_sz = test_arg[i].src_sz * 5;
        test_arg[i].decomp_out = qzMalloc(test_arg[i].decomp_out_sz, 0, COMMON_MEM);
        test_arg[i].gen_data = g_input_file_name ? 0 : 1;
        test_arg[i].init_engine_disabled = disable_init_engine;
        test_arg[i].init_sess_disabled = disable_init_session;
        test_arg[i].params = &g_params_th;
        test_arg[i].ops = qzThdOps;
        test_arg[i].blks = qzBlocks;
        if (!test_arg[i].comp_out || !test_arg[i].decomp_out) {
            QZ_ERROR("ERROR: fail to create memory for thread %ld\n", i);
            return -1;
        }
    }

    srand((uint32_t)getpid());
    (void)gettimeofday(&g_timer_start, NULL);
    for (i = 0; i < thread_count; i++) {
        rc = pthread_create(&threads[i], NULL, test_arg[i].ops, (void *)&test_arg[i]);
        if (0 != rc) {
            QZ_ERROR("Error from pthread_create %d\n", rc);
            return rc;
        }
    }

    /*for qzCompressAndDecompress test*/
    if (test == 4) {
        ret = pthread_mutex_lock(&g_cond_mutex);
        if (ret != 0) {
            QZ_ERROR("Failure to get Mutex Lock, status = %d\n", ret);
            goto done;
        }
        while (g_ready_thread_count < thread_count) {
            ret = pthread_cond_wait(&g_ready_cond, &g_cond_mutex);
            if (ret != 0) {
                QZ_ERROR("Failure calling pthread_cond_wait, status = %d\n", ret);
                goto done;
            }
        }
        g_ready_to_start = 1;
        ret = pthread_cond_broadcast(&g_start_cond);
        if (ret != 0) {
            QZ_ERROR("Failure calling pthread_cond_broadcast, status = %d\n", ret);
            goto done;
        }
        ret = pthread_mutex_unlock(&g_cond_mutex);
        if (ret != 0) {
            QZ_ERROR("Failure to release Mutex Lock, status = %d\n", ret);
            goto done;
        }
    }

    for (i = 0; i < thread_count; i++) {
        timeCheck(10, i);
        rc = pthread_join(threads[i], (void *)&p_rc);
        if (0 != rc) {
            QZ_ERROR("Error from pthread_join %d\n", rc);
            break;
        }
        if (NULL != p_rc) {
            QZ_ERROR("Error from pthread_exit %s\n", p_rc);
            ret = -1;
        }
        qzFree(test_arg[i].comp_out);
        qzFree(test_arg[i].decomp_out);
    }

    if (g_input_file_name != NULL) {
        qzFree(input_buf);
    }

done:
    qzClose(&g_session1);

    if (NULL != qzBlocks) {
        QzBlock_T *tmp, *blk = qzBlocks;
        while (blk) {
            tmp = blk;
            blk = blk->next;
            free(tmp);
        }
    }

    return (ret != 0) ? ret : rc;
}
