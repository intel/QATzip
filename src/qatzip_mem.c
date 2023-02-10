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
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#include <qat/qae_mem.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#include <qae_mem.h>
#endif
#include "qatzip.h"
#include "qz_utils.h"
#include "qatzip_internal.h"
#include "qatzip_page_table.h"


#define __STDC_WANT_LIB_EXT1__ 1
#include <linux/string.h>

static QzPageTable_T g_qz_page_table = {{{0}}};
static pthread_mutex_t g_qz_table_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_table_init = 0;
static __thread unsigned char *g_a;
extern processData_T g_process;

void *doUserMemset(void *const ptr, unsigned char filler,
                   const unsigned int count)
{
    unsigned int lim = 0;
    volatile unsigned char *volatile dstPtr = ptr;

    while (lim < count) {
        dstPtr[lim++] = filler;
    }
    return (void *)dstPtr;
}

/*
 *  * Fills a memory zone with a given constant byte,
 *   * returns pointer to the memory zone.
 *    */
void *qzMemSet(void *ptr, unsigned char filler, unsigned int count)
{
    if (ptr == NULL) {
        QZ_ERROR("Invaild input memory pointer!");
        return NULL;
    }
#ifdef __STDC_LIB_EXT1__
    errno_t result = memset_s(
                         ptr, sizeof(ptr), filler, count) /* Supported on C11 standard */
    if (result != 0) {
        QZ_ERROR("memset failed by the reason of %d", result);
    }
    return ptr;
#else
    return doUserMemset(
               ptr, filler, count); /* Platform-independent secure memset */
#endif /* __STDC_LIB_EXT1__ */
}

int qzMemFindAddr(unsigned char *a)
{
    int rc = 0;
    unsigned long al, b;
    al = (unsigned long)a;
    b = (al & PAGE_MASK);

    rc = (PINNED == loadAddr(&g_qz_page_table,
                             (void *)b)) ? PINNED_MEM : COMMON_MEM;
    if (0 != rc) {
        QZ_DEBUG("Find 0x%lx in page table\n", b);
    }
    return rc;
}

static void qzMemUnRegAddr(unsigned char *a)
{
    return;
}

static int qzMemRegAddr(unsigned char *a, size_t sz)
{
    int rc;
    unsigned long al, b;

    if (0 != pthread_mutex_lock(&g_qz_table_lock)) {
        return -1;
    }

    /*addr already registered*/
    if ((1 == qzMemFindAddr(a)) &&
        (1 == qzMemFindAddr(a + sz - 1))) {
        pthread_mutex_unlock(&g_qz_table_lock);
        return 0;
    }

    al = (unsigned long)a;
    b = (al & PAGE_MASK);
    sz += (al - b);
    QZ_DEBUG("4 KB page is 0x%lx\n", b);

    QZ_DEBUG("Inserting 0x%lx size %lx to page table\n", b, sz);
    rc = storeMmapRange(&g_qz_page_table, (void *)b, PINNED, sz);

    pthread_mutex_unlock(&g_qz_table_lock);

    return rc;
}

void qzMemDestory(void)
{
    if (0 == g_table_init) {
        return;
    }

    if (0 != pthread_mutex_lock(&g_qz_table_lock)) {
        return;
    }

    freePageTable(&g_qz_page_table);
    g_table_init = 0;

    if (0 != pthread_mutex_unlock(&g_qz_table_lock)) {
        return;
    }
}

void *qzMalloc(size_t sz, int numa, int pinned)
{
    int status;
    QzSession_T temp_sess;
    qzMemSet(&temp_sess, 0, sizeof(QzSession_T));

    if (0 == g_table_init) {
        if (0 != pthread_mutex_lock(&g_qz_table_lock)) {
            return NULL;
        }

        if (0 == g_table_init) {
            qzMemSet(&g_qz_page_table, 0, sizeof(QzPageTable_T));
            g_table_init = 1;
        }

        if (0 != pthread_mutex_unlock(&g_qz_table_lock)) {
            return NULL;
        }
    }

    if (1 == pinned && QZ_NONE == g_process.qz_init_status) {
        status = qzInit(&temp_sess, 1);
        if (QZ_INIT_FAIL(status)) {
            QZ_ERROR("QAT init failed with error: %d\n", status);
            return NULL;
        }
    }

    g_a = qaeMemAllocNUMA(sz, numa, 64);
    if (NULL == g_a) {
        if (0 == pinned) {
            QZ_DEBUG("regular malloc\n");
            g_a = malloc(sz);
        }
    } else {
        if (0 != qzMemRegAddr(g_a, sz)) {
            qaeMemFreeNUMA((void **)&g_a);
            return NULL;
        }
    }

    return g_a;
}

void qzFree(void *m)
{
    if (NULL == m) {
        return;
    }

    QZ_DEBUG("\t\tfreeing 0x%lx\n", (unsigned long)m);
    if (1 == qzMemFindAddr(m)) {
        qaeMemFreeNUMA((void **)&m);
        qzMemUnRegAddr(m);
    } else {
        free(m);
    }

    m = NULL;
}
