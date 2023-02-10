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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#endif
#include "qatzip.h"
#include "qatzip_internal.h"
#include "qz_utils.h"

#pragma pack(push, 1)

inline unsigned long qzGzipHeaderSz(void)
{
    return sizeof(QzGzH_T);
}

inline unsigned long stdGzipHeaderSz(void)
{
    return sizeof(StdGzH_T);
}

inline unsigned long qz4BHeaderSz(void)
{
    return sizeof(Qz4BH_T);
}

inline unsigned long stdGzipFooterSz(void)
{
    return sizeof(StdGzF_T);
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

void stdGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    StdGzH_T *hdr;

    hdr = (StdGzH_T *)ptr;
    hdr->id1      = 0x1f;
    hdr->id2      = 0x8b;
    hdr->cm       = QZ_DEFLATE;
    hdr->flag     = 0x00;
    hdr->mtime[0] = (char)0;
    hdr->mtime[1] = (char)0;
    hdr->mtime[2] = (char)0;
    hdr->mtime[3] = (char)0;
    hdr->xfl      = 0;
    hdr->os       = 255;
}

void qz4BHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    Qz4BH_T *hdr;
    hdr = (Qz4BH_T *)ptr;
    hdr->blk_size = res->produced;
}

int isQATDeflateProcessable(const unsigned char *ptr,
                            const unsigned int *const src_len,
                            QzSess_T *const qz_sess)
{
    QzGzH_T *h = (QzGzH_T *)ptr;
    Qz4BH_T *h_4B;
    StdGzF_T *qzFooter = NULL;
    long buff_sz = (DEST_SZ(qz_sess->sess_params.hw_buff_sz) < *src_len ? DEST_SZ(
                        (long)(qz_sess->sess_params.hw_buff_sz)) : *src_len);

    if (qz_sess->sess_params.data_fmt == DEFLATE_4B) {
        h_4B = (Qz4BH_T *)ptr;
        if (h_4B->blk_size > DEST_SZ(qz_sess->sess_params.hw_buff_sz)) {
            return 0;
        }
        return 1;
    }

    /*check if HW can process*/
    if (h->std_hdr.id1 == 0x1f       && \
        h->std_hdr.id2 == 0x8b       && \
        h->std_hdr.cm  == QZ_DEFLATE && \
        h->std_hdr.flag == 0x00) {
        qzFooter = (StdGzF_T *)(findStdGzipFooter((const unsigned char *)h, buff_sz));
        if ((unsigned char *)qzFooter - ptr - stdGzipHeaderSz() > DEST_SZ(
                (unsigned long)(qz_sess->sess_params.hw_buff_sz)) ||
            qzFooter->i_size > qz_sess->sess_params.hw_buff_sz) {
            return 0;
        }
        qz_sess->sess_params.data_fmt  = DEFLATE_GZIP;
        return 1;
    }

    /* Besides standard GZIP header, only Gzip header with QZ extension can be processed by QAT */
    if (h->std_hdr.id1 != 0x1f       || \
        h->std_hdr.id2 != 0x8b       || \
        h->std_hdr.cm  != QZ_DEFLATE) {
        /* There are two possibilities when h is not a gzip header: */
        /* 1, wrong data */
        /* 2, It is the 2nd, 3rd... part of the file with the standard gzip header. */
        return -1;
    }

    return (h->extra.st1 == 'Q'  && \
            h->extra.st2 == 'Z');
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
        (h->std_hdr.xfl != 0 && h->std_hdr.xfl != 2 && \
         h->std_hdr.xfl         != 4)               || \
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

void qzGzipFooterExt(const unsigned char *const ptr, StdGzF_T *ftr)
{
    QZ_MEMCPY(ftr, ptr, sizeof(*ftr), sizeof(*ftr));
}

unsigned char *findStdGzipFooter(const unsigned char *src_ptr,
                                 long src_avail_len)
{
    StdGzH_T *gzHeader = NULL;
    unsigned int offset = stdGzipHeaderSz() + stdGzipFooterSz();

    while (src_avail_len >= offset + stdGzipHeaderSz()) {
        gzHeader = (StdGzH_T *)(src_ptr + offset);
        if (gzHeader->id1 == 0x1f       && \
            gzHeader->id2 == 0x8b       && \
            gzHeader->cm  == QZ_DEFLATE && \
            gzHeader->flag == 0x00) {
            return (void *)((unsigned char *)gzHeader - stdGzipFooterSz());
        }
        offset++;
    }
    return (void *)((unsigned char *)src_ptr + src_avail_len - stdGzipFooterSz());
}

#pragma pack(pop)
