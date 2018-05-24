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
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "qatzip.h"
#include "qae_mem.h"
#include "qz_utils.h"
#include "qatzip_internal.h"

#define PAGE_MASK          ((unsigned long)(0x1fffff))
#define MAX_HUGEPAGE_FILE  "/sys/module/usdm_drv/parameters/max_huge_pages"

QzMem_T *g_qz_mem = NULL;
size_t g_mem_entries = 0;
static int g_init1 = 0, g_init2 = 0;
static pthread_mutex_t g_qz_mem_lock;
static __thread unsigned char *g_a;

int qzGetMaxHugePages(void)
{
    char max_hp_str[10] = {0};
    char *stop = NULL;
    int hp_params_fd;

    hp_params_fd = open(MAX_HUGEPAGE_FILE, O_RDONLY);
    if (hp_params_fd < 0) {
        QZ_ERROR("Open %s failed\n", MAX_HUGEPAGE_FILE);
        return QZ_LOW_MEM;
    }

    if (read(hp_params_fd, max_hp_str, sizeof(max_hp_str)) < 0) {
        QZ_ERROR("Read max_huge_pages from %s failed\n", MAX_HUGEPAGE_FILE);
        goto error;
    }

    g_mem_entries = strtoul(max_hp_str, &stop, 0);
    if (*stop != '\n') {
        QZ_ERROR("convert from %s to size_t failed\n", max_hp_str);
        goto error;
    }

    close(hp_params_fd);
    return QZ_OK;

error:
    close(hp_params_fd);
    return QZ_LOW_MEM;
}

int qzMemFindAddr(unsigned char *a)
{
    int i, rc = 0;
    unsigned long al, b;
    al = (unsigned long)a;
    b = (al - (al & PAGE_MASK));

    for (i = 0; i < g_mem_entries; i++) {
        if (g_qz_mem[i].addr == (unsigned char *)b) {
            QZ_DEBUG("Found 0x%lx at slot %d\n", b, i);
            rc = 1;
            break;
        }
    }

    return rc;
}

static void qzMemUnRegAddr(unsigned char *a)
{
    return;
}

static void qzMemRegAddr(unsigned char *a)
{
    unsigned long al, b;
    int i;

    /*addr already registered*/
    if (1 == qzMemFindAddr(a)) {
        return;
    }

    al = (unsigned long)a;
    b = (al - (al & PAGE_MASK));
    QZ_DEBUG("2 MB page is 0x%lx\n", b);

    if (0 != pthread_mutex_lock(&g_qz_mem_lock)) {
        return;
    }

    /*find an empty slot and insert addr*/
    for (i = 0; i < g_mem_entries; i++) {
        if (g_qz_mem[i].addr == NULL) {
            g_qz_mem[i].addr = (unsigned char *)b;
            QZ_DEBUG("Inserting 0x%lx at slot %d\n", b, i);
            break;
        }
    }

    pthread_mutex_unlock(&g_qz_mem_lock);
}

void *qzMalloc(size_t sz, int numa, int pinned)
{
    int i;

    if (0 == g_init1) {
        if (0 != pthread_mutex_lock(&g_qz_mem_lock)) {
            return NULL;
        }


        if (NULL == g_qz_mem) {
            if (QZ_OK != qzGetMaxHugePages() || \
                0 == g_mem_entries) {
                (void)pthread_mutex_unlock(&g_qz_mem_lock);
                return NULL;
            }

            g_qz_mem = (QzMem_T *)calloc(g_mem_entries, sizeof(QzMem_T));
            if (NULL ==  g_qz_mem) {
                (void)pthread_mutex_unlock(&g_qz_mem_lock);
                return NULL;
            }
        }

        if (0 == g_init2) {
            g_init1 = 1;
            g_init2 = 1;
            for (i = 0; i < g_mem_entries; i++) {
                g_qz_mem[i].flag = 0;
                g_qz_mem[i].addr = NULL;
                g_qz_mem[i].sz   = 0;
                g_qz_mem[i].numa = 0;
            }
        }

        if (0 != pthread_mutex_unlock(&g_qz_mem_lock)) {
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
        qzMemRegAddr(g_a);
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
