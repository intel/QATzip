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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <zlib.h>
#include <pthread.h>
#include <lz4frame.h>
#include <lz4.h>

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

#define GZIP_WRAPPER 16
extern processData_T g_process;

static const QzExtraField_T g_extra_field = {
    .st1 = 'Q',
    .st2 = 'Z',
    .x2_len = (uint16_t)sizeof(g_extra_field.qz_e),
    .qz_e.src_sz = 0,
    .qz_e.dest_sz = 0,
};

static void gen_qatzip_hdr(gz_header *hdr)
{
    qzMemSet(hdr, 0, sizeof(gz_header));
    hdr->extra = (Bytef *)&g_extra_field;
    hdr->extra_len = (uInt)sizeof(g_extra_field);
    hdr->os = 255;
}

int qzDeflateSWCompress(QzSession_T *sess, const unsigned char *src,
                        unsigned int *src_len, unsigned char *dest,
                        unsigned int *dest_len, unsigned int last)

{
    int ret;
    z_stream *stream = NULL;
    int flush_flag;
    int last_loop_in;
    int last_loop_out;
    int current_loop_in;
    int current_loop_out;
    gz_header hdr;
    unsigned int left_input_sz = *src_len;
    unsigned int left_output_sz = *dest_len;
    unsigned int send_sz;
    unsigned int total_in = 0, total_out = 0;
    QzSess_T *qz_sess = NULL;
    int windows_bits = 0;
    int comp_level = Z_DEFAULT_COMPRESSION;
    DataFormatInternal_T data_fmt = DATA_FORMAT_DEFAULT;
    unsigned int chunk_sz = QZ_HW_BUFF_SZ;
    QzGzH_T *qz_hdr = NULL;
    Qz4BH_T *qz4B_header = NULL;

    *src_len = 0;
    *dest_len = 0;

    assert(sess);
    assert(sess->internal);

    qz_sess = (QzSess_T *) sess->internal;
    qz_sess->force_sw = 1;
    comp_level = qz_sess->sess_params.comp_lvl;
    data_fmt = qz_sess->sess_params.data_fmt;
    chunk_sz = qz_sess->sess_params.hw_buff_sz;
    stream = qz_sess->deflate_strm;

    if (DeflateNull == qz_sess->deflate_stat) {
        if (NULL == stream) {
            stream = calloc(1, sizeof(z_stream));
            if (NULL == stream) {
                return QZ_FAIL;
            }

            qz_sess->deflate_strm = stream;
        }

        stream->zalloc = (alloc_func)0;
        stream->zfree = (free_func)0;
        stream->opaque = (voidpf)0;
        stream->total_in = 0;
        stream->total_out = 0;

        switch (data_fmt) {
        case DEFLATE_4B:
        case DEFLATE_RAW:
            windows_bits = -MAX_WBITS;
            break;
        case DEFLATE_GZIP:
        case DEFLATE_GZIP_EXT:
        default:
            windows_bits = MAX_WBITS + GZIP_WRAPPER;
            break;
        }

        /*Gzip header*/
        if (Z_OK != deflateInit2(stream,
                                 comp_level,
                                 Z_DEFLATED,
                                 windows_bits,
                                 MAX_MEM_LEVEL,
                                 Z_DEFAULT_STRATEGY)) {
            qz_sess->deflate_stat = DeflateNull;
            return QZ_FAIL;
        }
        qz_sess->deflate_stat = DeflateInited;

        if (DEFLATE_GZIP_EXT == data_fmt) {
            gen_qatzip_hdr(&hdr);
            if (Z_OK != deflateSetHeader(stream, &hdr)) {
                qz_sess->deflate_stat = DeflateNull;
                stream->total_in = 0;
                stream->total_out = 0;
                return QZ_FAIL;
            }
            qz_hdr = (QzGzH_T *)dest;
        } else if (DEFLATE_4B == data_fmt) {
            /* Need to reserve 4 bytes to fill the compressed length. */
            qz4B_header = (Qz4BH_T *)dest;
            dest = dest + sizeof(Qz4BH_T);
        }
    }

#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), COMPRESSION, SW);
#endif

    do {
        send_sz = left_input_sz > chunk_sz ? chunk_sz : left_input_sz;
        left_input_sz -= send_sz;

        if (0 == left_input_sz && 1 == last) {
            flush_flag = Z_FINISH;
        } else {
            flush_flag = Z_FULL_FLUSH;
        }

        stream->next_in   = (z_const Bytef *)src + total_in;
        stream->avail_in  = send_sz;
        stream->next_out  = (Bytef *)dest + total_out;
        stream->avail_out = left_output_sz;

        last_loop_in = GET_LOWER_32BITS(stream->total_in);
        last_loop_out = GET_LOWER_32BITS(stream->total_out);


        ret = deflate(stream, flush_flag);
        if ((Z_STREAM_END != ret && Z_FINISH == flush_flag) ||
            (Z_OK != ret  && Z_FULL_FLUSH == flush_flag)) {
            QZ_ERROR("ERR: deflate failed with return code: %d flush_flag: %d\n", ret,
                     flush_flag);
            stream->total_in = 0;
            stream->total_out = 0;
            qz_sess->deflate_stat = DeflateNull;
            return QZ_FAIL;
        }

        current_loop_in = GET_LOWER_32BITS(stream->total_in) - last_loop_in;
        current_loop_out = GET_LOWER_32BITS(stream->total_out) - last_loop_out;
        left_output_sz -= current_loop_out;

        total_out += current_loop_out;
        total_in += current_loop_in;
        *src_len = total_in;
        *dest_len = total_out;

        if (NULL != qz_sess->crc32) {
            if (DEFLATE_RAW == data_fmt) {
                *qz_sess->crc32 = crc32(*qz_sess->crc32,
                                        src + total_in - current_loop_in,
                                        current_loop_in);
            } else {
                if (0 == *qz_sess->crc32) {
                    *qz_sess->crc32 = stream->adler;
                } else {
                    *qz_sess->crc32 =
                        crc32_combine(*qz_sess->crc32, stream->adler, *src_len);
                }
            }
        }
    } while (left_input_sz);

    if (1 == last) {
        /*
         * When data_fmt is DEFLATE_GZIP_EXT,
         * we should fill src_sz & dest_sz in gzipext header field.
         */
        if (DEFLATE_GZIP_EXT == data_fmt && qz_hdr) {
            qz_hdr->extra.qz_e.src_sz  = stream->total_in;
            qz_hdr->extra.qz_e.dest_sz = stream->total_out -
                                         outputHeaderSz(data_fmt) - outputFooterSz(data_fmt);
        } else if (DEFLATE_4B == data_fmt && qz4B_header) {
            qz4B_header->blk_size = stream->total_out;
            *dest_len = *dest_len + sizeof(Qz4BH_T);
        }
        ret = deflateEnd(stream);
        stream->total_in = 0;
        stream->total_out = 0;
        qz_sess->deflate_stat = DeflateNull;
        if (Z_OK != ret) {
            return QZ_FAIL;
        }
    }

    return QZ_OK;
}

int qzDeflateSWDecompress(QzSession_T *sess, const unsigned char *src,
                          unsigned int *src_len, unsigned char *dest,
                          unsigned int *dest_len)

{
    z_stream *stream = NULL;
    int ret = QZ_OK;
    int zlib_ret = Z_OK;
    DataFormatInternal_T data_fmt;
    int windows_bits = 0;
    unsigned int total_in;
    unsigned int total_out;
    unsigned int qz4B_header_len = 0;

    QzSess_T *qz_sess = (QzSess_T *) sess->internal;
    qz_sess->force_sw = 1;
    stream = qz_sess->inflate_strm;
    data_fmt = qz_sess->sess_params.data_fmt;

    if (NULL == stream) {
        stream = calloc(1, sizeof(z_stream));
        if (NULL == stream) {
            *src_len = 0;
            *dest_len = 0;
            return QZ_FAIL;
        }

        stream->zalloc = (alloc_func)0;
        stream->zfree  = (free_func)0;
        stream->opaque = (voidpf)0;
        stream->total_in    = 0;
        stream->total_out   = 0;
        qz_sess->inflate_strm = stream;
    }

    stream->next_in   = (z_const Bytef *)src;
    stream->avail_in  = *src_len;
    stream->next_out  = (Bytef *)dest;
    stream->avail_out = *dest_len;
    *src_len = 0;
    *dest_len = 0;

    QZ_DEBUG("decomp_sw data_fmt: %d\n", data_fmt);
    switch (data_fmt) {
    case DEFLATE_4B:
    case DEFLATE_RAW:
        windows_bits = -MAX_WBITS;
        break;
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    default:
        windows_bits = MAX_WBITS + GZIP_WRAPPER;
        break;
    }

    if (InflateNull == qz_sess->inflate_stat) {
        if (DEFLATE_4B == data_fmt) {
            /* For DEFLATE_4B, we need to skip the header. */
            stream->next_in = (z_const Bytef *)stream->next_in + sizeof(Qz4BH_T);
            stream->avail_in = stream->avail_in - sizeof(Qz4BH_T);
            qz4B_header_len = sizeof(Qz4BH_T);
        }
        ret = inflateInit2(stream, windows_bits);
        if (Z_OK != ret) {
            ret = QZ_FAIL;
            goto done;
        }
        QZ_DEBUG("\n****** inflate init done with win_bits: %d *****\n", windows_bits);
        qz_sess->inflate_stat = InflateInited;
        stream->total_in = 0;
        total_in = 0;
        stream->total_out = 0;
        total_out = 0;
    } else {
        total_in = GET_LOWER_32BITS(stream->total_in);
        total_out = GET_LOWER_32BITS(stream->total_out);
    }

    zlib_ret = inflate(stream, Z_SYNC_FLUSH);
    switch (zlib_ret) {
    case Z_OK:
        if (QZ_LOW_DEST_MEM == sess->thd_sess_stat) {
            QZ_DEBUG("ERR: inflate failed with Z_DATA_ERROR\n");
            ret = QZ_DATA_ERROR;
            qz_sess->inflate_stat = InflateError;
            goto done;
        }
        ret = QZ_OK;
        qz_sess->inflate_stat = InflateOK;
        break;
    case Z_STREAM_END:
        ret = QZ_OK;
        qz_sess->inflate_stat = InflateEnd;
        break;
    case Z_DATA_ERROR:
        QZ_DEBUG("ERR: inflate failed with Z_DATA_ERROR\n");
        ret = QZ_DATA_ERROR;
        qz_sess->inflate_stat = InflateError;
        goto done;
    default:
        QZ_DEBUG("ERR: inflate failed with error code %d\n", ret);
        ret = QZ_FAIL;
        qz_sess->inflate_stat = InflateError;
        goto done;
    }

    *dest_len = GET_LOWER_32BITS(stream->total_out - total_out);
    /* for Deflate_4B, we need to add the length of Deflate 4B header. */
    *src_len = GET_LOWER_32BITS(stream->total_in - total_in + qz4B_header_len);

done:
    QZ_DEBUG("Exit qzSWDecompress total_in: %lu total_out: %lu "
             "avail_in: %u avail_out: %u msg: %s "
             "src_len: %u dest_len: %u\n",
             stream->total_in, stream->total_out,
             stream->avail_in, stream->avail_out,
             stream->msg,
             *src_len,
             *dest_len);
    if (zlib_ret == Z_STREAM_END || QZ_LOW_DEST_MEM == sess->thd_sess_stat) {
        if (Z_OK != inflateEnd(stream)) {
            QZ_DEBUG("inflateEnd failed.\n");
            ret = QZ_FAIL;
        }
        qz_sess->inflate_stat = InflateNull;
        QZ_DEBUG("\n****** inflate end done *****\n");
    }

    return ret;
}

int qzSWDecompressMultiGzip(QzSession_T *sess, const unsigned char *src,
                            unsigned int *src_len, unsigned char *dest,
                            unsigned int *dest_len)
{
    int ret = QZ_OK;
    unsigned int total_in = 0;
    unsigned int total_out = 0;
    const unsigned int input_len = *src_len;
    const unsigned int output_len = *dest_len;
    unsigned int cur_input_len = input_len;
    unsigned int cur_output_len = output_len;
#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), DECOMPRESSION, SW);
#endif
    QZ_DEBUG("Start qzSWDecompressMultiGzip: src_len %u dest_len %u\n",
             *src_len, *dest_len);

    *src_len = 0;
    *dest_len = 0;

    while (total_in < input_len && total_out < output_len) {
        ret = qzDeflateSWDecompress(sess,
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
        *src_len  = total_in;
        *dest_len = total_out;
    }

out:
    QZ_DEBUG("Exit qzSWDecompressMultiGzip: src_len %u dest_len %u\n",
             *src_len, *dest_len);
    return ret;
}

int qzLZ4SWCompress(QzSession_T *sess, const unsigned char *src,
                    unsigned int *src_len, unsigned char *dest,
                    unsigned int *dest_len, unsigned int last)
{
    size_t total_out = 0;
    assert(sess);
    assert(sess->internal);
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    LZ4F_preferences_t preferences = {0};
    preferences.frameInfo.contentChecksumFlag = 1;
    preferences.frameInfo.contentSize = *src_len;
    preferences.autoFlush = 1;
    preferences.compressionLevel = qz_sess->sess_params.comp_lvl;
    total_out = LZ4F_compressFrame(dest, *dest_len, src, *src_len, &preferences);
    if (LZ4F_isError(total_out)) {
        QZ_ERROR("LZ4F_compressUpdate error: %s\n", LZ4F_getErrorName(total_out));
        goto lz4_compress_fail;
    }
    *dest_len = total_out;
    QZ_DEBUG("Exit qzLZ4SWCompress: src_len %u dest_len %u\n",
             *src_len, *dest_len);

    return QZ_OK;

lz4_compress_fail:
    *src_len = 0;
    *dest_len = 0;
    return QZ_FAIL;
}

int qzLZ4SWDecompress(QzSession_T *sess, const unsigned char *src,
                      unsigned int *src_len, unsigned char *dest,
                      unsigned int *dest_len)
{
    size_t in_sz = 0;
    size_t out_sz = 0;
    size_t ret = QZ_OK;
    QzSess_T *qz_sess = NULL;
    QZ_DEBUG("Enter qzLZ4SWDecompress: src_len %u dest_len %u\n",
             *src_len, *dest_len);

    qz_sess = (QzSess_T *) sess->internal;
    if (qz_sess->dctx == NULL) {
        ret = LZ4F_createDecompressionContext(&(qz_sess->dctx), LZ4F_VERSION);
        if (LZ4F_isError(ret)) {
            QZ_ERROR("LZ4F_createDecompressionContext error: %s\n",
                     LZ4F_getErrorName(ret));
            goto lz4_decompress_fail;
        }
    }

    in_sz = *src_len;
    out_sz = *dest_len;
    ret = LZ4F_decompress(qz_sess->dctx, dest, (size_t *)&out_sz,
                          src, (size_t *)&in_sz, NULL);
    if (LZ4F_isError(ret)) {
        QZ_ERROR("LZ4F_decompress error: %s\n", LZ4F_getErrorName(ret));
        goto lz4_decompress_fail;
    } else if (ret == 0) {
        /*
         * when ret == 0, it means that a frame be fully decompressed,
         * we need to free the dctx, not reuse it to decompress another frame.
         */
        LZ4F_freeDecompressionContext(qz_sess->dctx);
        qz_sess->dctx = NULL;
    } else {
        /*
         * when ret > 0, it means that the compressed data is not a fully frame,
         * it needs more compressed data to decompress. The remaining compressed data
         * was stored in dctx.
         */
        QZ_DEBUG("LZ4F_decompress: incomplete compressed data, need more data\n");
    }

    *src_len = in_sz;
    *dest_len = out_sz;
    QZ_DEBUG("Exit qzLZ4SWDecompress: src_len %u dest_len %u\n",
             *src_len, *dest_len);

    return QZ_OK;

lz4_decompress_fail:
    if (qz_sess->dctx != NULL) {
        LZ4F_freeDecompressionContext(qz_sess->dctx);
        qz_sess->dctx = NULL;
    }

    *src_len = 0;
    *dest_len  = 0;
    return QZ_FAIL;
}

int qzSWDecompressMultiLZ4(QzSession_T *sess, const unsigned char *src,
                           unsigned int *src_len, unsigned char *dest,
                           unsigned int *dest_len)
{
    int ret = QZ_OK;
    unsigned int total_in = 0;
    unsigned int total_out = 0;
    const unsigned int input_len = *src_len;
    const unsigned int output_len = *dest_len;
    unsigned int cur_input_len = input_len;
    unsigned int cur_output_len = output_len;
#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), DECOMPRESSION, SW);
#endif
    QZ_DEBUG("Start qzSWDecompressMultiLz4: src_len %u dest_len %u\n",
             *src_len, *dest_len);

    *src_len = 0;
    *dest_len = 0;

    while (total_in < input_len && total_out < output_len) {
        ret = qzLZ4SWDecompress(sess,
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
        *src_len  = total_in;
        *dest_len = total_out;
    }

out:
    QZ_DEBUG("Exit qzSWDecompressMultiLz4: src_len %u dest_len %u\n",
             *src_len, *dest_len);
    return ret;
}


/* The software failover function for compression request */
int qzSWCompress(QzSession_T *sess, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len, unsigned int last)
{
#ifdef ENABLE_TESTLOG
    QZ_TESTLOG(2, 0, "SW_FALLBACK", "sw fallback happend!");
#endif
    int ret = QZ_FAIL;
    DataFormatInternal_T data_fmt;
    QzSess_T *qz_sess = NULL;

    assert(sess);
    assert(sess->internal);

    qz_sess = (QzSess_T *) sess->internal;
    data_fmt = qz_sess->sess_params.data_fmt;

    switch (data_fmt) {
    case DEFLATE_RAW:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_4B:
        ret = qzDeflateSWCompress(sess, src, src_len, dest, dest_len, last);
        break;
    case LZ4_FH:
        ret = qzLZ4SWCompress(sess, src, src_len, dest, dest_len, last);
        break;
    default:
        QZ_ERROR("Unknown/unsupported data format: %d\n", data_fmt);
        *src_len = 0;
        *dest_len = 0;
        ret = QZ_FAIL;
        break;
    }
    return ret;
}

/* The software failover function for decompression request */
int qzSWDecompress(QzSession_T *sess, const unsigned char *src,
                   unsigned int *src_len, unsigned char *dest,
                   unsigned int *dest_len)
{
#ifdef ENABLE_TESTLOG
    QZ_TESTLOG(2, 0, "SW_FALLBACK", "sw fallback happend!");
#endif
    int ret = QZ_FAIL;
    DataFormatInternal_T data_fmt;
    QzSess_T *qz_sess = NULL;

    assert(sess);
    assert(sess->internal);

    qz_sess = (QzSess_T *) sess->internal;
    data_fmt = qz_sess->sess_params.data_fmt;

    switch (data_fmt) {
    case DEFLATE_RAW:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_4B:
        ret = qzDeflateSWDecompress(sess, src, src_len, dest, dest_len);
        break;
    case LZ4_FH:
        ret = qzLZ4SWDecompress(sess, src, src_len, dest, dest_len);
        break;
    default:
        QZ_ERROR("Unknown/unsupported data format: %d\n", data_fmt);
        *src_len = 0;
        *dest_len = 0;
        ret = QZ_FAIL;
        break;
    }
    return ret;
}


int qzSWDecompressMulti(QzSession_T *sess, const unsigned char *src,
                        unsigned int *src_len, unsigned char *dest,
                        unsigned int *dest_len)
{
#ifdef ENABLE_TESTLOG
    QZ_TESTLOG(2, 0, "SW_FALLBACK", "sw fallback happend!");
#endif
    int ret = QZ_FAIL;
    DataFormatInternal_T data_fmt;
    QzSess_T *qz_sess = NULL;

    assert(sess);
    assert(sess->internal);

    qz_sess = (QzSess_T *) sess->internal;
    data_fmt = qz_sess->sess_params.data_fmt;

    switch (data_fmt) {
    case DEFLATE_RAW:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_4B:
        ret = qzSWDecompressMultiGzip(sess, src, src_len, dest, dest_len);
        break;
    case LZ4_FH:
        ret = qzSWDecompressMultiLZ4(sess, src, src_len, dest, dest_len);
        break;
    default:
        QZ_ERROR("Unknown/unsupported data format: %d\n", data_fmt);
        *src_len = 0;
        *dest_len = 0;
        ret = QZ_FAIL;
        break;
    }
    return ret;
}

int compInSWFallback(int i, int j, QzSession_T *sess,
                     unsigned char *src_ptr, unsigned int src_send_sz)
{
    int rc;
    unsigned int dest_receive_sz = DEST_SZ(src_send_sz);
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;

    if (!qz_sess->sess_params.sw_backup) {
        QZ_DEBUG("The instance %d heartbeat is failure, Don't enable sw fallback, compressIn fatal ERROR!\n",
                 i);
        return QZ_FAIL;
    }

    if (qz_sess->single_thread) {
        QZ_DEBUG("The instance %d failure, single_thread, compressIn fatal ERROR!\n",
                 i);
        return QZ_FAIL;
    }

    if (qz_sess->stop_submitting) {
        QZ_DEBUG("compInSWFallback stop submit\n");
        return QZ_FAIL;
    }

    if (qz_sess->seq != qz_sess->seq_in) {
        return QZ_WAIT_SW_PENDING;
    }

    QZ_DEBUG("SW CompIn Sending %u bytes, seq = %ld, instance = %d\n", src_send_sz,
             qz_sess->seq, i);
    /* sw fallback here */
    rc = qzSWCompress(sess, src_ptr, &src_send_sz, qz_sess->next_dest,
                      &dest_receive_sz,
                      qz_sess->last);

    QZ_DEBUG("SW CompIn src_ptr %p, dst_ptr %p, Sending %u bytes, seq = %ld\n",
             src_ptr, qz_sess->next_dest, src_send_sz, qz_sess->seq);

    if (unlikely(QZ_OK != rc)) {
        QZ_ERROR("SW CompIn fallback failure! compress fatal ERROR!\n");
        return QZ_FAIL;
    }

    /* For SW compress, have to update ComputeOut status first */
    qz_sess->next_dest += dest_receive_sz;
    qz_sess->qz_in_len += src_send_sz;
    qz_sess->qz_out_len += dest_receive_sz;
    qz_sess->seq_in++;
    qz_sess->processed++;
    return QZ_OK;
}

int compOutSWFallback(int i, int j, QzSession_T *sess,
                      long *dest_avail_len)
{
    int rc;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    unsigned int src_send_sz =
        g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes;
    /* sw dest buffer is align with the inst hw buffer size */
    unsigned int dest_receive_sz = outputHeaderSz(qz_sess->sess_params.data_fmt) +
                                   g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes;
    unsigned char *src_ptr = g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;
    unsigned char *dest_ptr = g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;

    if (!qz_sess->sess_params.sw_backup) {
        QZ_DEBUG("The instance %d heartbeat is failure, Don't enable sw fallback, compressOut fatal ERROR!\n",
                 i);
        return QZ_FAIL;
    }

    QZ_DEBUG("The request get dummy emty respond, offload to software!\n");
    QZ_DEBUG("SW CompOut src_ptr %p, dst_ptr %p, Sending %u bytes, seq = %ld\n",
             src_ptr, dest_ptr, src_send_sz, g_process.qz_inst[i].stream[j].seq);

    rc = qzSWCompress(sess, src_ptr, &src_send_sz, dest_ptr, &dest_receive_sz,
                      qz_sess->last);
    if (QZ_OK != rc) {
        QZ_ERROR("SW CompOut fallback failure! compress fatal ERROR!\n");
        return QZ_FAIL;
    }

    rc = compOutCheckDestLen(i, j, sess, dest_avail_len, dest_receive_sz);
    if (QZ_OK != rc) {
        return rc;
    }

    /* Update qz_sess info and clean dest buffer */
    compOutValidDestBufferCleanUp(i, j, qz_sess, dest_receive_sz);
    qz_sess->next_dest += dest_receive_sz;
    qz_sess->qz_in_len += src_send_sz;
    qz_sess->qz_out_len += dest_receive_sz;
    return rc;
}

int decompInSWFallback(int i, int j, QzSession_T *sess,
                       unsigned char *src_ptr,
                       unsigned char *dest_ptr,
                       unsigned int *tmp_src_avail_len,
                       unsigned int *tmp_dest_avail_len)
{
    int rc;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;

    if (!qz_sess->sess_params.sw_backup) {
        QZ_DEBUG("The instance %d heartbeat is failure, Don't enable sw fallback, decompressIn fatal ERROR!\n",
                 i);
        sess->thd_sess_stat = QZ_FAIL;
        return QZ_FAIL;
    }

    if (qz_sess->single_thread) {
        QZ_DEBUG("The instance %d failure, single thread, decompressIn fatal ERROR!\n",
                 i);
        sess->thd_sess_stat = QZ_FAIL;
        return QZ_FAIL;
    }

    if (qz_sess->stop_submitting) {
        QZ_DEBUG("decompInSWFallback stop submit\n");
        return QZ_FAIL;
    }

    if (qz_sess->seq != qz_sess->seq_in) {
        return QZ_WAIT_SW_PENDING;
    }

    QZ_DEBUG("SW DecompIn src_ptr %p, dest_ptr %p, Sending %u bytes, seq = %ld, instance = %d\n",
             src_ptr, dest_ptr, tmp_src_avail_len, qz_sess->seq, i);
    rc = qzSWDecompress(sess,
                        src_ptr,
                        tmp_src_avail_len,
                        dest_ptr,
                        tmp_dest_avail_len);
    if (unlikely(QZ_OK != rc)) {
        QZ_ERROR("SW deCompIn fallback failure! decompress fatal ERROR!\n");
        sess->thd_sess_stat = QZ_FAIL;
        return QZ_FAIL;
    }

    /* For compressIn fallback, need to update seq_in first */
    qz_sess->seq_in++;
    qz_sess->processed++;
    qz_sess->qz_in_len  += *tmp_src_avail_len;
    qz_sess->qz_out_len += *tmp_dest_avail_len;
    qz_sess->next_dest += *tmp_dest_avail_len;
    return QZ_OK;
}

int decompOutSWFallback(int i, int j, QzSession_T *sess,
                        unsigned int *dest_avail_len)
{
    int rc;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    unsigned int src_send_sz =
        g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes;
    unsigned int dest_receive_sz =
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes;
    /*  pinned buffer ptr already strip header, need retrieve.
    *   use original src_ptr and consumed len to get src_ptr for this request.
    */
    unsigned char *src_ptr = qz_sess->src + qz_sess->qz_in_len;
    src_send_sz += (outputHeaderSz(data_fmt) + outputFooterSz(data_fmt)) ;

    if (!qz_sess->sess_params.sw_backup) {
        QZ_DEBUG("The instance %d heartbeat is failure, Don't enable sw fallback, decompressOut fatal ERROR!\n",
                 i);
        return QZ_FAIL;
    }

    QZ_DEBUG("The request get dummy empty respond, offload to software!\n");
    QZ_DEBUG("SW DecompOut src_ptr %p, dest_ptr %p, Sending %u bytes, receive %u bytes, seq = %ld\n",
             src_ptr, qz_sess->next_dest, src_send_sz, dest_receive_sz,
             g_process.qz_inst[i].stream[j].seq);
    rc = qzSWDecompress(sess,
                        src_ptr,
                        &src_send_sz,
                        qz_sess->next_dest,
                        &dest_receive_sz);
    if (QZ_OK != rc) {
        QZ_ERROR("SW deCompOut fallback failure! compress fatal ERROR!\n");
        return QZ_FAIL;
    }

    /* update qz_sess info, Don't need clean dest buffer */
    decompOutErrorDestBufferCleanUp(i, j);
    qz_sess->next_dest += dest_receive_sz;
    qz_sess->qz_in_len += src_send_sz;
    qz_sess->qz_out_len += dest_receive_sz;
    *dest_avail_len -= dest_receive_sz;
    return QZ_OK;
}

