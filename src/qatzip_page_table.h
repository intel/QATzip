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


#ifndef _QATZIP_PAGE_TABLE_H
#define _QATZIP_PAGE_TABLE_H

#include <stdint.h>
#include <sys/mman.h>
#include <qatzip_internal.h>

#define PAGE_SHIFT (12)
#define PAGE_SIZE  (0x1000)
#define PAGE_MASK  (~(PAGE_SIZE - 1))
#define LEVEL_SIZE (PAGE_SIZE / sizeof(uint64_t))
#define PINNED     (-1)

typedef struct __attribute__((__packed__))
{
    uint32_t offset : 12;
    uint32_t idxl0 : 9;
    uint32_t idxl1 : 9;
    uint32_t idxl2 : 9;
    uint32_t idxl3 : 9;
}
QzPageEntry_T;

typedef union {
    uint64_t addr;
    QzPageEntry_T pg_entry;
} QzPageIndex_T;

typedef struct QzPageTable_T {
    union {
        uint64_t mt;
        struct QzPageTable_T *pt;
    } next[LEVEL_SIZE];
} QzPageTable_T;


static inline void *nextLevel(QzPageTable_T *volatile *ptr)
{
    QzPageTable_T *old_ptr = *ptr;
    QzPageTable_T *new_ptr;

    if (NULL != old_ptr)
        return old_ptr;

    new_ptr = mmap(NULL,
                   sizeof(QzPageTable_T),
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0);
    if ((void *) - 1 == new_ptr) {
        QZ_ERROR("nextLevel: mmap error\n");
        return NULL;
    }

    if (!__sync_bool_compare_and_swap(ptr, NULL, new_ptr))
        munmap(new_ptr, sizeof(QzPageTable_T));

    return *ptr;
}

static inline void freePageLevel(QzPageTable_T *const level, const size_t iter)
{
    size_t i = 0;

    if (0 == iter)
        return;

    for (i = 0; i < LEVEL_SIZE; ++i) {
        QzPageTable_T *pt = level->next[i].pt;
        if (NULL != pt) {
            freePageLevel(pt, iter - 1);
            munmap(pt, sizeof(QzPageTable_T));
        }
    }
}

static inline void freePageTable(QzPageTable_T *const table)
{
    /* There are 1+3 levels in 48-bit page table for 4KB pages. */
    freePageLevel(table, 3);
    /* Reset global root table. */
    qzMemSet(table, 0, sizeof(QzPageTable_T));
}

static inline int storeAddr(QzPageTable_T *level,
                            uintptr_t virt,
                            uint64_t type)
{
    QzPageIndex_T id;

    id.addr = virt;

    level = nextLevel(&level->next[id.pg_entry.idxl3].pt);
    if (NULL == level) {
        return -1;
    }

    level = nextLevel(&level->next[id.pg_entry.idxl2].pt);
    if (NULL == level) {
        return -1;
    }

    level = nextLevel(&level->next[id.pg_entry.idxl1].pt);
    if (NULL == level) {
        return -1;
    }

    level->next[id.pg_entry.idxl0].mt = type;
    return 0;
}

static inline int storeMmapRange(QzPageTable_T *p_level,
                                 void *p_virt,
                                 uint64_t type,
                                 size_t p_size)
{
    size_t offset;
    size_t page_size = PAGE_SIZE;
    const uintptr_t virt = (uintptr_t)p_virt;

    for (offset = 0; offset < p_size; offset += page_size) {
        if (0 != storeAddr(p_level, virt + offset, type)) {
            return -1;
        }
    }

    return 0;
}

static inline uint64_t loadAddr(QzPageTable_T *level, void *virt)
{
    QzPageIndex_T id;

    id.addr = (uintptr_t)virt;

    level = level->next[id.pg_entry.idxl3].pt;
    if (NULL == level)
        return 0;

    level = level->next[id.pg_entry.idxl2].pt;
    if (NULL == level)
        return 0;

    level = level->next[id.pg_entry.idxl1].pt;
    if (NULL == level)
        return 0;

    return level->next[id.pg_entry.idxl0].mt;
}

#endif
