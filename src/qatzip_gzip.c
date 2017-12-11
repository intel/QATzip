/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2017 Intel Corporation. All rights reserved.
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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "qatzip.h"
#include "qatzipP.h"
#include "qz_utils.h"

#pragma pack(push, 1)

inline unsigned long qzGzipHeaderSz(void)
{
    return sizeof(QzGzH_T);
}

inline unsigned long qzGzipFooterSz(void)
{
    return sizeof(QzGzF_T);
}

void qzGzipHeaderExtraFieldGen(unsigned char *ptr, CpaDcRqResults *res)
{
    QzExtraField_T *extra;
    extra = (QzExtraField_T *)ptr;

    extra->st1        = 'Q';
    extra->st2        = 'Z';
    extra->x2_len      = (uint16_t)sizeof(extra->qz_e);
    extra->qz_e.src_sz  = res->consumed;
    extra->qz_e.dest_sz = res->produced;
}

void qzGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    QzGzH_T *hdr;

    hdr = (QzGzH_T *)ptr;
    hdr->id1      = 0x1f;
    hdr->id2      = 0x8b;
    hdr->cm       = QZ_DEFLATE;
    hdr->flag     = 0x04; /*Fextra BIT SET*/
    hdr->mtime[0] = (char)0;
    hdr->mtime[1] = (char)0;
    hdr->mtime[2] = (char)0;
    hdr->mtime[3] = (char)0;
    hdr->xfl      = 0;
    hdr->os       = 255;
    hdr->x_len     = (uint16_t)sizeof(hdr->extra);
    qzGzipHeaderExtraFieldGen((unsigned char *)&hdr->extra, res);
}

int isStdGzipHeader(const unsigned char *const ptr)
{
    QzGzH_T *h = (QzGzH_T *)ptr;

    return (h->id1 == 0x1f       && \
            h->id2 == 0x8b       && \
            h->cm  == QZ_DEFLATE && \
            h->extra.st1 != 'Q'  && \
            h->extra.st2 != 'Z');
}

int qzGzipHeaderExt(const unsigned char *const ptr, QzGzH_T *hdr)
{
    QzGzH_T *h;

    h = (QzGzH_T *)ptr;
    if (h->id1          != 0x1f             || \
        h->id2          != 0x8b             || \
        h->extra.st1    != 'Q'              || \
        h->extra.st2    != 'Z'              || \
        h->cm           != QZ_DEFLATE       || \
        h->flag         != 0x04             || \
        h->xfl          != 0                || \
        h->os           != 255              || \
        h->x_len        != sizeof(h->extra) || \
        h->extra.x2_len != sizeof(h->extra.qz_e)) {
        QZ_ERROR("id1: %x, id2: %x, st1: %c, st2: %c, cm: %d, flag: %d,"
                 "xfl: %d, os: %d, x_len: %d, x2_len: %d\n",
                 h->id1, h->id2, h->extra.st1, h->extra.st2, h->cm, h->flag,
                 h->xfl, h->os, h->x_len, h->extra.x2_len);
        return QZ_FAIL;
    }

    QZ_MEMCPY(hdr, ptr, sizeof(*hdr), sizeof(*hdr));
    return QZ_OK;
}

void qzGzipFooterGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    QzGzF_T *ftr;

    ftr = (QzGzF_T *)ptr;
    ftr->crc32 = res->checksum;
    ftr->i_size = res->consumed;
}

void qzGzipFooterExt(const unsigned char *const ptr, QzGzF_T *ftr)
{
    QZ_MEMCPY(ftr, ptr, sizeof(*ftr), sizeof(*ftr));
}

#pragma pack(pop)
