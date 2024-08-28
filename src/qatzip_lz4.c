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

#include <assert.h>
#define XXH_NAMESPACE QATZIP_
#include "xxhash.h"


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

inline unsigned long qzLZ4HeaderSz(void)
{
    return QZ_LZ4_HEADER_SIZE;
}

inline unsigned long qzLZ4FooterSz(void)
{
    return QZ_LZ4_FOOTER_SIZE;
}

int qzVerifyLZ4FrameHeader(const unsigned char *const ptr, uint32_t len)
{
    QzLZ4H_T *hdr = NULL;

    assert(ptr != NULL);
    assert(len >= QZ_LZ4_HEADER_SIZE);
    QZ_DEBUG("qzVerifyLZ4FrameHeader\n");

    hdr = (QzLZ4H_T *)ptr;

    //Skippable frames
    if ((hdr->magic & 0xFFFFFFF0U) == QZ_LZ4_MAGIC_SKIPPABLE) {
        /*for skippalbe frames, fallback to software decompression */
        QZ_DEBUG("qzVerifyLZ4FrameHeader: skip frames, switch to software.\n");
        return  QZ_FORCE_SW;
    }

    //Unknown magic number
    if (hdr->magic != QZ_LZ4_MAGIC) {
        QZ_DEBUG("qzVerifyLZ4FrameHeader: unknown lz4 frame magic number %x.\n",
                 hdr->magic);
        return QZ_FAIL;
    }

    //No support for unknown lz4 version
    if ((hdr->flag_desc >> 6 & 0x3) != QZ_LZ4_VERSION) {
        QZ_DEBUG("qzVerifyLZ4FrameHeader: unknown lz4 frame version number.\n");
        return QZ_FAIL;
    }

    if ((hdr->flag_desc & 0x1)      != QZ_LZ4_DICT_ID_FLAG ||
        (hdr->flag_desc >> 4 & 0x1) != QZ_LZ4_BLK_CKS_FLAG ||
        (hdr->flag_desc >> 2 & 0x1) != QZ_LZ4_CNT_CKS_FLAG ||
        (hdr->flag_desc >> 3 & 0x1) != QZ_LZ4_CNT_SIZE_FLAG) {
        QZ_DEBUG("qzVerifyLZ4FrameHeader: unsupported lz4 frame header \
                 switch to software.\n");
        return QZ_FORCE_SW;
    }

    return QZ_OK;
}

void qzLZ4HeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    QzLZ4H_T *hdr = NULL;
    unsigned char *hc_start = NULL;

    assert(ptr != NULL);
    assert(res != NULL);

    hdr = (QzLZ4H_T *)ptr;
    hdr->magic = QZ_LZ4_MAGIC;
    //flag descriptor
    hdr->flag_desc = (unsigned char)(((QZ_LZ4_VERSION  & 0x03) << 6) +
                                     ((QZ_LZ4_BLK_INDEP & 0x01) << 5) +
                                     ((QZ_LZ4_BLK_CKS_FLAG & 0x01) << 4) +
                                     ((QZ_LZ4_CNT_SIZE_FLAG & 0x01) << 3) +
                                     ((QZ_LZ4_CNT_CKS_FLAG & 0x01) << 2) +
                                     (QZ_LZ4_DICT_ID_FLAG & 0x01));

    //block descriptor
    hdr->block_desc = (unsigned char)((QZ_LZ4_MAX_BLK_SIZE & 0x07) << 4);

    //content size
    hdr->cnt_size = res->consumed;

    //header checksum
    hc_start = ptr + QZ_LZ4_MAGIC_SIZE;
    hdr->hdr_cksum = (unsigned char)((XXH32(hc_start, QZ_LZ4_FD_SIZE - 1,
                                            0) >> 8) & 0xff);
}

void qzLZ4FooterGen(unsigned char *ptr, CpaDcRqResults *res)
{
    QzLZ4F_T *footer = NULL;

    assert(ptr != NULL);

    footer = (QzLZ4F_T *)ptr;
    footer->end_mark = QZ_LZ4_ENDMARK;
    footer->cnt_cksum = res->checksum;
}

unsigned char *findLZ4Footer(const unsigned char *src_ptr,
                             long src_avail_len)
{
    const unsigned char *lz4_footer = NULL;
    unsigned int block_sz = 0;
    unsigned int frame_sz = 0;

    assert(src_ptr != NULL);

    //the src_avail_len should be equal or greater than minimum of frame size
    if (src_avail_len < (QZ_LZ4_HEADER_SIZE +
                         QZ_LZ4_BLK_HEADER_SIZE + QZ_LZ4_FOOTER_SIZE)) {
        return NULL;
    }

    block_sz = (*(unsigned int *)(src_ptr + QZ_LZ4_HEADER_SIZE)) & 0x7fffffff;
    frame_sz = block_sz + QZ_LZ4_HEADER_SIZE +
               QZ_LZ4_BLK_HEADER_SIZE + QZ_LZ4_FOOTER_SIZE;

    while (src_avail_len >= frame_sz) {
        lz4_footer = (src_ptr + frame_sz - QZ_LZ4_FOOTER_SIZE);
        if (*(unsigned int *)lz4_footer == (unsigned int)0x0) {
            return (unsigned char *)lz4_footer;
        }
        block_sz = (*(unsigned int *)lz4_footer) & 0x7fffffff;
        frame_sz += block_sz + QZ_LZ4_BLK_HEADER_SIZE;
    }
    return NULL;
}

int isQATLZ4Processable(const unsigned char *ptr,
                        const unsigned int *const src_len,
                        QzSess_T *const qz_sess)
{
    QzLZ4H_T *hdr = (QzLZ4H_T *)ptr;
    QzLZ4F_T *footer = NULL;

    if (*src_len < QZ_LZ4_HEADER_SIZE) {
        return 0;
    }

    //Skippable frames
    if ((hdr->magic & 0xFFFFFFF0U) == QZ_LZ4_MAGIC_SKIPPABLE) {
        /*for skippalbe frames, fallback to software decompression */
        return  0;
    }

    //Unknown magic number
    if (hdr->magic != QZ_LZ4_MAGIC) {
        return 0;
    }

    //No support for unknown lz4 version
    if ((hdr->flag_desc >> 6 & 0x3) != QZ_LZ4_VERSION) {
        return 0;
    }

    if ((hdr->flag_desc & 0x1)      != QZ_LZ4_DICT_ID_FLAG ||
        (hdr->flag_desc >> 4 & 0x1) != QZ_LZ4_BLK_CKS_FLAG ||
        (hdr->flag_desc >> 2 & 0x1) != QZ_LZ4_CNT_CKS_FLAG ||
        (hdr->flag_desc >> 3 & 0x1) != QZ_LZ4_CNT_SIZE_FLAG) {
        return 0;
    }

    footer = (QzLZ4F_T *)findLZ4Footer(ptr, *src_len);
    if (footer == NULL ||
        ((unsigned int)((unsigned char *)footer - ptr - QZ_LZ4_HEADER_SIZE)) > DEST_SZ(
            qz_sess->sess_params.hw_buff_sz)) {
        return 0;
    }

    return 1;
}

inline unsigned long qzLZ4SBlockHeaderSz(void)
{
    return QZ_LZ4_BLK_HEADER_SIZE;
}

void qzLZ4SBlockHeaderGen(unsigned char *ptr, CpaDcRqResults *res)
{
    assert(ptr != NULL);
    assert(res != NULL);
    //block header contains block size
    unsigned int *blk_size = (unsigned int *)(ptr);
    *blk_size = (unsigned int)res->produced;
}
