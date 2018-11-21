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

#include "cpa.h"
#include "cpa_dc.h"
#include "icp_sal_poll.h"
#include "icp_sal_user.h"
#include "qae_mem.h"

#include <stdlib.h>
#include <qatzip.h>
#include <qz_utils.h>
#include <qatzip_internal.h>

static int initStream(QzSession_T *sess, QzStream_T *strm)
{
    int rc = QZ_FAIL;
    QzSess_T *qz_sess = NULL;
    QzStreamBuf_T *stream_buf = NULL;

    if (NULL != strm->opaque) {
        return QZ_DUPLICATE;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return rc;
        }
    }
    qz_sess = (QzSess_T *)(sess->internal);
    qz_sess->strm = strm;

    strm->opaque = malloc(sizeof(QzStreamBuf_T));
    stream_buf = (QzStreamBuf_T *) strm->opaque;
    if (NULL == stream_buf) {
        QZ_ERROR("Fail to allocate memory for QzStreamBuf");
        return QZ_FAIL;
    }

    stream_buf->out_offset = 0;
    stream_buf->buf_len = qz_sess->sess_params.strm_buff_sz;
    stream_buf->in_buf = qzMalloc(stream_buf->buf_len, NODE_0, PINNED_MEM);
    stream_buf->out_buf = qzMalloc(stream_buf->buf_len, NODE_0, PINNED_MEM);

    if (NULL == stream_buf->in_buf ||
        NULL == stream_buf->out_buf) {
        QZ_ERROR("Fail to allocate memory for QzStreamBuf");
        return QZ_FAIL;
    }
    QZ_DEBUG("Allocate stream buf %u\n", stream_buf->buf_len);

    strm->pending_in = 0;
    strm->pending_out = 0;
    strm->crc_32 = 0;
    strm->crc_64 = 0;
    return QZ_OK;
}

static unsigned int copyStreamInput(QzStream_T *strm, unsigned char *in)
{
    unsigned int cpy_cnt = 0;
    unsigned int avail_in = 0;
    QzStreamBuf_T *stream_buf = strm->opaque;

    avail_in = stream_buf->buf_len - strm->pending_in;
    cpy_cnt = (strm->in_sz > avail_in) ? avail_in : strm->in_sz;
    QZ_MEMCPY(stream_buf->in_buf + strm->pending_in, in, cpy_cnt, cpy_cnt);
    QZ_DEBUG("Copy to input from %p, to %p, count %u\n",
             in, stream_buf->in_buf + strm->pending_in, cpy_cnt);

    strm->pending_in += cpy_cnt;
    strm->in_sz -= cpy_cnt;
    return cpy_cnt;
}

static unsigned int copyStreamOutput(QzStream_T *strm, unsigned char *out)
{
    unsigned int cpy_cnt = 0;
    unsigned int avail_out = 0;
    QzStreamBuf_T *stream_buf = strm->opaque;

    avail_out = strm->out_sz;
    cpy_cnt = (strm->pending_out > avail_out) ? avail_out : strm->pending_out;
    QZ_MEMCPY(out, stream_buf->out_buf + stream_buf->out_offset, cpy_cnt, cpy_cnt);
    QZ_DEBUG("copy %ld to user output\n", cpy_cnt);

    strm->out_sz -= cpy_cnt;
    strm->pending_out -= cpy_cnt;
    stream_buf->out_offset += cpy_cnt;

    if (strm->pending_out == 0) {
        stream_buf->out_offset = 0;
    }

    return cpy_cnt;
}


int qzCompressStream(QzSession_T *sess, QzStream_T *strm, unsigned int last)
{
    int rc = QZ_FAIL;
    unsigned long *strm_crc = NULL;
    unsigned int input_len = 0;
    unsigned int output_len = 0;
    unsigned int copied_output = 0;
    unsigned int copied_input = 0;
    unsigned int consumed = 0;
    unsigned int produced = 0;
    unsigned int strm_last = 0;
    QzStreamBuf_T *stream_buf = NULL;
    QzSess_T *qz_sess = NULL;
    QzDataFormat_T data_fmt = QZ_DEFLATE_GZIP_EXT;

    if (NULL == sess     || \
        NULL == strm     || \
        (last != 0 && last != 1)) {
        rc = QZ_PARAMS;
        goto end;
    }

    if (NULL == strm->in || \
        NULL == strm->out) {
        rc = QZ_PARAMS;
        goto end;
    }

    if (NULL == strm->opaque) {
        rc = initStream(sess, strm);
        if (QZ_FAIL == rc) {
            goto done;
        }
    }

    switch (strm->crc_type) {
    case QZ_CRC64:
        strm_crc = (unsigned long *)&strm->crc_64;
        break;
    case QZ_CRC32:
    case QZ_ADLER:
    default:
        strm_crc = (unsigned long *)&strm->crc_32;
        break;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return rc;
        }
    }
    qz_sess = (QzSess_T *)(sess->internal);
    data_fmt = qz_sess->sess_params.data_fmt;
    if (data_fmt != QZ_DEFLATE_RAW &&
        data_fmt != QZ_DEFLATE_GZIP_EXT) {
        QZ_ERROR("Invalid data format: %d\n", data_fmt);
        return QZ_PARAMS;
    }

    stream_buf = (QzStreamBuf_T *) strm->opaque;
    while (strm->pending_out > 0) {
        copied_output = copyStreamOutput(strm, strm->out + produced);
        produced += copied_output;
        if (0 == copied_output) {
            rc = QZ_OK;
            goto done;
        }
    }

    while (0 == strm->pending_out) {
        copied_input += copyStreamInput(strm, strm->in + consumed);

        if (strm->pending_in < stream_buf->buf_len &&
            last != 1) {
            rc = QZ_OK;
            goto done;
        }

        input_len = strm->pending_in;
        output_len = stream_buf->buf_len;
        copied_input -= strm->pending_in;
        strm_last = (0 == strm->in_sz && last) ? 1 : 0;
        QZ_DEBUG("Before Call qzCompressCrc input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);

        rc = qzCompressCrc(sess, stream_buf->in_buf, &input_len,
                           stream_buf->out_buf, &output_len, strm_last, strm_crc);

        strm->pending_in = 0;
        strm->pending_out = output_len;
        copied_output = copyStreamOutput(strm, strm->out + produced);
        consumed += input_len;
        produced += copied_output;

        QZ_DEBUG("After Call qzCompressCrc input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);

        if (QZ_BUF_ERROR == rc) {
            if (0 == input_len) {
                QZ_ERROR("ERROR in copy stream output, stream buf size = %u\n",
                         stream_buf->buf_len);
                rc = QZ_FAIL;
            } else {
                rc = QZ_OK;
            }
            goto done;
        }

        if (QZ_OK != rc) {
            rc = QZ_FAIL;
            goto done;
        }

        if (0 == strm->pending_in && 0 == strm->in_sz) {
            rc = QZ_OK;
            goto done;
        }
    }

done:
    strm->in_sz = copied_input + consumed;
    strm->out_sz = produced;
    QZ_DEBUG("Exit Compress Stream input_len %u output_len %u "
             "stream->pending_in %u stream->pending_out %u "
             "stream->in_sz %d stream->out_sz %d\n",
             input_len, output_len, strm->pending_in, strm->pending_out,
             strm->in_sz, strm->out_sz);
end:
    return rc;
}


int qzDecompressStream(QzSession_T *sess, QzStream_T *strm, unsigned int last)
{
    int rc = QZ_FAIL;
    unsigned int input_len = 0;
    unsigned int output_len = 0;
    unsigned int copied_output = 0;
    unsigned int copied_input = 0;
    unsigned int consumed = 0;
    unsigned int produced = 0;
    QzStreamBuf_T *stream_buf = NULL;

    if (NULL == sess     || \
        NULL == strm     || \
        (last != 0 && last != 1)) {
        rc = QZ_PARAMS;
        goto end;
    }

    if (NULL == strm->in || \
        NULL == strm->out) {
        rc = QZ_PARAMS;
        goto end;
    }

    if (NULL == strm->opaque) {
        rc = initStream(sess, strm);
        if (QZ_FAIL == rc) {
            goto done;
        }
    }
    stream_buf = (QzStreamBuf_T *) strm->opaque;
    QZ_DEBUG("Decompress Stream Start...\n");

    while (strm->pending_out > 0) {
        copied_output = copyStreamOutput(strm, strm->out + produced);
        produced += copied_output;
        if (0 == copied_output) {
            rc = QZ_OK;
            QZ_DEBUG("No space for pending output...\n");
            goto done;
        }
        QZ_DEBUG("Copy output %u bytes\n", copied_output);
    }

    while (0 == strm->pending_out) {
        if (0 == strm->in_sz) {
            rc = QZ_OK;
            QZ_DEBUG("No input...\n");
            goto done;
        }

        copied_input += copyStreamInput(strm, strm->in + consumed);

        if (strm->pending_in < stream_buf->buf_len &&
            last != 1) {
            rc = QZ_OK;
            QZ_DEBUG("Batch more input data...\n");
            goto done;
        }

        input_len = strm->pending_in;
        output_len = stream_buf->buf_len;
        copied_input -= strm->pending_in;
        QZ_DEBUG("Before Call qzDecompress input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);
        rc = qzDecompress(sess, stream_buf->in_buf, &input_len,
                          stream_buf->out_buf, &output_len);

        QZ_DEBUG("Return code = %d\n", rc);
        if (QZ_OK != rc && QZ_BUF_ERROR != rc) {
            return rc;
        }

        consumed += input_len;
        strm->pending_in -= input_len;
        strm->pending_out = output_len;
        copied_output = copyStreamOutput(strm, strm->out + produced);
        produced += copied_output;
        QZ_DEBUG("Copy output %u bytes\n", copied_output);

        QZ_DEBUG("After Call qzDecompress input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);
        if (QZ_BUF_ERROR == rc) {
            QZ_DEBUG("Recoverable buffer error occurs... set pending_in 0\n");
            strm->pending_in = 0;
            rc = QZ_OK;
            break;
        }
        if (0 == strm->pending_out) {
            QZ_DEBUG("Pending_out = 0, set pending_in 0\n");
            strm->pending_in = 0;
        }
    }

done:
    strm->in_sz = copied_input + consumed;
    strm->out_sz = produced;
    QZ_DEBUG("Exit Decompress Stream input_len %u output_len %u "
             "stream->pending_in %u stream->pending_out %u "
             "stream->in_sz %d stream->out_sz %d\n",
             input_len, output_len, strm->pending_in, strm->pending_out,
             strm->in_sz, strm->out_sz);
end:
    return rc;
}


int qzEndStream(QzSession_T *sess, QzStream_T *strm)
{
    int rc = QZ_FAIL;
    QzStreamBuf_T *stream_buf = NULL;

    if (NULL == sess || \
        NULL == strm) {
        rc = QZ_PARAMS;
        goto exit;
    }

    if (NULL == strm->opaque) {
        rc = QZ_OK;
        goto done;
    }

    stream_buf = (QzStreamBuf_T *)strm->opaque;
    qzFree(stream_buf->out_buf);
    qzFree(stream_buf->in_buf);
    free(stream_buf);
    strm->opaque = NULL;
    rc = QZ_OK;

done:
    strm->pending_in = 0;
    strm->pending_out = 0;
    strm->in_sz = 0;
    strm->out_sz = 0;
exit:
    return rc;
}
