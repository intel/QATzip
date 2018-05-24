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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "qatzip.h"
#include "qatzip_internal.h"
#include "qz_utils.h"

#pragma pack(push, 1)

inline unsigned long qzGzipHeaderSz(void)
{
    return sizeof(QzGzH_T);
}

inline unsigned long stdGzipFooterSz(void)
{
    return sizeof(StdGzF_T);
}

inline unsigned long outputFooterSz(QzDataFormat_T data_fmt)
{
    unsigned long size = 0;
    switch (data_fmt) {
    case QZ_DEFLATE_RAW:
        size = 0;
        break;
    case QZ_DEFLATE_GZIP_EXT:
    default:
        size = stdGzipFooterSz();
        break;
    }

    return size;
}

unsigned long outputHeaderSz(QzDataFormat_T data_fmt)
{
    unsigned long size = 0;

    switch (data_fmt) {
    case QZ_DEFLATE_RAW:
        break;
    case QZ_DEFLATE_GZIP_EXT:
    default:
        size = qzGzipHeaderSz();
        break;
    }

    return size;
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
    hdr->std_hdr.id1      = 0x1f;
    hdr->std_hdr.id2      = 0x8b;
    hdr->std_hdr.cm       = QZ_DEFLATE;
    hdr->std_hdr.flag     = 0x04; /*Fextra BIT SET*/
    hdr->std_hdr.mtime[0] = (char)0;
    hdr->std_hdr.mtime[1] = (char)0;
    hdr->std_hdr.mtime[2] = (char)0;
    hdr->std_hdr.mtime[3] = (char)0;
    hdr->std_hdr.xfl      = 0;
    hdr->std_hdr.os       = 255;
    hdr->x_len     = (uint16_t)sizeof(hdr->extra);
    qzGzipHeaderExtraFieldGen((unsigned char *)&hdr->extra, res);
}

void outputHeaderGen(unsigned char *ptr,
                     CpaDcRqResults *res,
                     QzDataFormat_T data_fmt)
{
    QZ_DEBUG("Generate header\n");

    switch (data_fmt) {
    case QZ_DEFLATE_RAW:
        break;
    case QZ_DEFLATE_GZIP_EXT:
    default:
        qzGzipHeaderGen(ptr, res);
        break;
    }
}

int isStdGzipHeader(const unsigned char *const ptr)
{
    QzGzH_T *h = (QzGzH_T *)ptr;

    return (h->std_hdr.id1 == 0x1f       && \
            h->std_hdr.id2 == 0x8b       && \
            h->std_hdr.cm  == QZ_DEFLATE && \
            h->extra.st1 != 'Q'  && \
            h->extra.st2 != 'Z');
}

int qzGzipHeaderExt(const unsigned char *const ptr, QzGzH_T *hdr)
{
    QzGzH_T *h;

    h = (QzGzH_T *)ptr;
    if (h->std_hdr.id1          != 0x1f             || \
        h->std_hdr.id2          != 0x8b             || \
        h->extra.st1            != 'Q'              || \
        h->extra.st2            != 'Z'              || \
        h->std_hdr.cm           != QZ_DEFLATE       || \
        h->std_hdr.flag         != 0x04             || \
        h->std_hdr.xfl          != 0                || \
        h->std_hdr.os           != 255              || \
        h->x_len                != sizeof(h->extra) || \
        h->extra.x2_len         != sizeof(h->extra.qz_e)) {
        QZ_DEBUG("id1: %x, id2: %x, st1: %c, st2: %c, cm: %d, flag: %d,"
                 "xfl: %d, os: %d, x_len: %d, x2_len: %d\n",
                 h->std_hdr.id1, h->std_hdr.id2, h->extra.st1, h->extra.st2,
                 h->std_hdr.cm, h->std_hdr.flag, h->std_hdr.xfl, h->std_hdr.os,
                 h->x_len, h->extra.x2_len);
        return QZ_FAIL;
    }

    QZ_MEMCPY(hdr, ptr, sizeof(*hdr), sizeof(*hdr));
    return QZ_OK;
}

void qzGzipFooterGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(NULL != ptr);
    assert(NULL != res);
    StdGzF_T *ftr;

    ftr = (StdGzF_T *)ptr;
    ftr->crc32 = res->checksum;
    ftr->i_size = res->consumed;
}

inline void outputFooterGen(QzSess_T *qz_sess,
                            CpaDcRqResults *res,
                            QzDataFormat_T data_fmt)
{
    QZ_DEBUG("Generate footer\n");

    unsigned char *ptr = qz_sess->next_dest;
    switch (data_fmt) {
    case QZ_DEFLATE_RAW:
        break;
    case QZ_DEFLATE_GZIP_EXT:
    default:
        qzGzipFooterGen(ptr, res);
        break;
    }
}

void qzGzipFooterExt(const unsigned char *const ptr, StdGzF_T *ftr)
{
    QZ_MEMCPY(ftr, ptr, sizeof(*ftr), sizeof(*ftr));
}

#pragma pack(pop)
