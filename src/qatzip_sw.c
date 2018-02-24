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
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <zlib.h>
#include <pthread.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "qatzip.h"
#include "qatzipP.h"
#include "qz_utils.h"

#define GZIP_WRAPPER 16

static const QzExtraField_T g_extra_field = {
    .st1 = 'Q',
    .st2 = 'Z',
    .x2_len = (uint16_t)sizeof(g_extra_field.qz_e),
    .qz_e.src_sz = 0,
    .qz_e.dest_sz = 0,
};

static void gen_qatzip_hdr(gz_header *hdr)
{
    memset(hdr, 0, sizeof(gz_header));
    hdr->extra = (Bytef *)&g_extra_field;
    hdr->extra_len = (uInt)sizeof(g_extra_field);
    hdr->os = 255;
}

/* The software failover function for compression request */
int qzSWCompress(QzSession_T *sess, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len, unsigned int last)

{
    int ret;
    z_stream stream;
    gz_header hdr;
    CpaDcRqResults res;
    unsigned int left_input_sz = *src_len;
    unsigned int left_output_sz = *dest_len;
    unsigned int send_sz;
    unsigned int cur_hdr_pos = 0;
    unsigned int total_in = 0, total_out = 0;
    QzSess_T *qz_sess = NULL;
    int windows_bits = 0;
    int comp_level = Z_DEFAULT_COMPRESSION;
    QzDataFormat_T data_fmt = QZ_DATA_FORMAT_DEFAULT;
    unsigned int chunk_sz = QZ_HW_BUFF_SZ;

    /*check if setupSession called*/
    if (NULL == sess->internal) {
        ret = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(ret)) {
            return ret;
        }
    }

    qz_sess = (QzSess_T *) sess->internal;
    qz_sess->force_sw = 1;
    comp_level = (qz_sess->sess_params.comp_lvl == Z_BEST_COMPRESSION) ? \
                 Z_BEST_COMPRESSION : Z_DEFAULT_COMPRESSION;
    data_fmt = qz_sess->sess_params.data_fmt;
    chunk_sz = qz_sess->sess_params.hw_buff_sz;

    switch (data_fmt) {
    case QZ_DEFLATE_RAW:
        windows_bits = -MAX_WBITS;
        break;
    case QZ_DEFLATE_GZIP:
    case QZ_DEFLATE_GZIP_EXT:
    default:
        windows_bits = MAX_WBITS + GZIP_WRAPPER;
        break;
    }

    stream.zalloc = (alloc_func)0;
    stream.zfree = (free_func)0;
    stream.opaque = (voidpf)0;
#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), COMPRESSION, SW);
#endif

    while (left_input_sz) {
        /*Gzip header*/
        if (Z_OK != deflateInit2(&stream,
                                 comp_level,
                                 Z_DEFLATED,
                                 windows_bits,
                                 MAX_MEM_LEVEL,
                                 Z_DEFAULT_STRATEGY)) {
            return QZ_FAIL;
        }

        if (QZ_DEFLATE_GZIP != data_fmt &&
            QZ_DEFLATE_RAW != data_fmt) {
            gen_qatzip_hdr(&hdr);
            if (Z_OK != deflateSetHeader(&stream, &hdr)) {
                return QZ_FAIL;
            }
        }

        send_sz = left_input_sz > chunk_sz ? chunk_sz : left_input_sz;
        left_input_sz -= send_sz;

        stream.next_in   = (z_const Bytef *)src + total_in;
        stream.avail_in  = send_sz;
        stream.next_out  = (Bytef *)dest + total_out;
        stream.avail_out = left_output_sz;

        if (Z_STREAM_END != (ret = deflate(&stream, Z_FINISH))) {
            QZ_ERROR("ERR: deflate failed with return code: %d\n", ret);
            return QZ_FAIL;
        }

        left_output_sz -= GET_LOWER_32BITS(stream.total_out);
        res.consumed = (Cpa32U) GET_LOWER_32BITS(stream.total_in);
        res.produced = (Cpa32U) GET_LOWER_32BITS((stream.total_out -
                       outputHeaderSz(data_fmt) -
                       outputFooterSz(data_fmt)));
        outputHeaderGen(dest + cur_hdr_pos, &res, data_fmt);
        cur_hdr_pos += GET_LOWER_32BITS(stream.total_out);

        total_out += GET_LOWER_32BITS(stream.total_out);
        total_in += GET_LOWER_32BITS(stream.total_in);
        *src_len = total_in;
        *dest_len = total_out;

        if (NULL != qz_sess->crc32) {
            if (QZ_DEFLATE_RAW == data_fmt) {
                *qz_sess->crc32 = crc32(*qz_sess->crc32, src, *src_len);
            } else {
                if (0 == *qz_sess->crc32) {
                    *qz_sess->crc32 = stream.adler;
                } else {
                    *qz_sess->crc32 =
                        crc32_combine(*qz_sess->crc32, stream.adler, *src_len);
                }
            }
        }

        if (Z_OK != deflateEnd(&stream)) {
            return QZ_FAIL;
        }
    }

    return QZ_OK;
}

/* The software failover function for decompression request */
int qzSWDecompress(QzSession_T *sess, const unsigned char *src,
                   unsigned int *uncompressed_buf_len, unsigned char *dest,
                   unsigned int *compressed_buffer_len)
{
    z_stream *stream = NULL;
    int ret = QZ_OK;

    QzSess_T *qz_sess = (QzSess_T *) sess->internal;
    qz_sess->force_sw = 1;
    stream = qz_sess->inflate_strm;
    if (NULL == stream) {
        stream = malloc(sizeof(z_stream));
        if (NULL == stream) {
            return QZ_FAIL;
        }

        stream->zalloc = (alloc_func)0;
        stream->zfree  = (free_func)0;
        stream->opaque = (voidpf)0;
        ((QzSess_T *) sess->internal)->inflate_strm = stream;
    }

    stream->next_in   = (z_const Bytef *)src;
    stream->avail_in  = *uncompressed_buf_len;
    stream->next_out  = (Bytef *)dest;
    stream->avail_out = *compressed_buffer_len;

    /*gunzip*/
    ret = inflateInit2(stream, MAX_WBITS + GZIP_WRAPPER);
    if (Z_OK != ret) {
        ret = QZ_FAIL;
        goto done;
    }

    ret = inflate(stream, Z_FINISH);
    if ((ret == Z_OK) || (ret == Z_STREAM_END)) {
        ret = QZ_OK;
    } else if (Z_DATA_ERROR == ret) {
        ret = QZ_DATA_ERROR;
        goto done;
    } else {
        QZ_ERROR("ERR: inflate failed with error code %d\n", ret);
        ret = QZ_FAIL;
        goto done;
    }

    *compressed_buffer_len = GET_LOWER_32BITS(stream->total_out);
    *uncompressed_buf_len = GET_LOWER_32BITS(stream->total_in);

done:
    if (NULL != stream) {
        if (Z_OK != inflateEnd(stream)) {
            ret = QZ_FAIL;
        }
    }

    return ret;
}

int qzSWDecompressMultiGzip(QzSession_T *sess, const unsigned char *src,
                            unsigned int *uncompressed_buf_len, unsigned char *dest,
                            unsigned int *compressed_buffer_len)
{
    int ret = QZ_OK;
    unsigned int total_in = 0;
    unsigned int total_out = 0;
    const unsigned int input_len = *uncompressed_buf_len;
    const unsigned int output_len = *compressed_buffer_len;
    unsigned int cur_input_len = input_len;
    unsigned int cur_output_len = output_len;
#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), DECOMPRESSION, SW);
#endif
    while (total_in < input_len) {
        ret = qzSWDecompress(sess,
                             src + total_in,
                             &cur_input_len,
                             dest + total_out,
                             &cur_output_len);
        if (ret != QZ_OK) {
            goto out;
        }

        total_in  += cur_input_len;
        total_out += cur_output_len;
        cur_input_len  = input_len - total_in;
        cur_output_len = output_len - total_out;
        *uncompressed_buf_len  = total_in;
        *compressed_buffer_len = total_out;
    }

out:
    return ret;
}
