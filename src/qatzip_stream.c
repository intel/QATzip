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

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#include <qat/icp_sal_poll.h>
#include <qat/icp_sal_user.h>
#include <qat/qae_mem.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#include <icp_sal_poll.h>
#include <icp_sal_user.h>
#include <qae_mem.h>
#endif

#include <stdlib.h>
#include <assert.h>
#include <qatzip.h>
#include <qz_utils.h>
#include <qatzip_internal.h>

#define STREAM_BUFF_LIST_SZ 8

typedef struct StreamBuffNode_S {
    void *buffer;
    size_t size;
    int pinned;
    struct StreamBuffNode_S *next;
    struct StreamBuffNode_S *prev;
} StreamBuffNode_T;

typedef struct StreamBuffNodeList_S {
    StreamBuffNode_T *head;
    StreamBuffNode_T *tail;
    unsigned int size;
    pthread_mutex_t mutex;
} StreamBuffNodeList_T;

StreamBuffNodeList_T g_strm_buff_list_free = {
    .head = NULL,
    .tail = NULL,
    .size = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

StreamBuffNodeList_T g_strm_buff_list_used = {
    .head = NULL,
    .tail = NULL,
    .size = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static inline int addNodeToList(StreamBuffNode_T *node,
                                StreamBuffNodeList_T *buff_list)
{
    buff_list->size += 1;
    node->next = NULL;
    if (NULL == buff_list->tail) {
        buff_list->head = node;
        node->prev = NULL;
    } else {
        node->prev = buff_list->tail;
        buff_list->tail->next = node;
    }
    buff_list->tail = node;

    return SUCCESS;
}

static inline int removeNodeFromList(StreamBuffNode_T *node,
                                     StreamBuffNodeList_T *buff_list)
{
    buff_list->size -= 1;
    if (NULL != node->prev) {
        node->prev->next = node->next;
        if (NULL != node->next) {
            node->next->prev = node->prev;
        } else {
            buff_list->tail = node->prev;
        }
    } else {
        if (NULL != node->next) {
            node->next->prev = NULL;
            buff_list->head = node->next;
        } else {
            buff_list->head = NULL;
            buff_list->tail = NULL;
        }
    }

    return SUCCESS;
}

void streamBufferCleanup(void)
{
    StreamBuffNode_T *node;
    StreamBuffNode_T *next;

    if (unlikely(0 != pthread_mutex_lock(&g_strm_buff_list_used.mutex))) {
        QZ_ERROR("Failed to get Mutex Lock.\n");
        return;
    }

    node = g_strm_buff_list_used.head;
    while (node != NULL) {
        next = node->next;
        removeNodeFromList(node, &g_strm_buff_list_used);
        qzFree(node->buffer);
        free(node);
        node = next;
    }

    if (unlikely(0 != pthread_mutex_unlock(&g_strm_buff_list_used.mutex))) {
        QZ_ERROR("Failed to release Mutex Lock.\n");
        return;
    }

    if (unlikely(0 != pthread_mutex_lock(&g_strm_buff_list_free.mutex))) {
        QZ_ERROR("Failed to get Mutex Lock.\n");
        return;
    }

    node = g_strm_buff_list_free.head;
    while (node != NULL) {
        next = node->next;
        removeNodeFromList(node, &g_strm_buff_list_free);
        qzFree(node->buffer);
        free(node);
        node = next;
    }

    if (unlikely(0 != pthread_mutex_unlock(&g_strm_buff_list_free.mutex))) {
        QZ_ERROR("Failed to release Mutex Lock.\n");
        return;
    }
}

static void *getNodeBuffFromFreeList(size_t sz, int pinned)
{
    pthread_mutex_lock(&g_strm_buff_list_free.mutex);
    pthread_mutex_lock(&g_strm_buff_list_used.mutex);

    void *retval  = NULL;
    StreamBuffNode_T *node;

    for (node = g_strm_buff_list_free.head; node != NULL; node = node->next) {
        if (pinned == node->pinned && sz <= node->size) {
            if (!removeNodeFromList(node, &g_strm_buff_list_free)) {
                retval = NULL;
                goto done;
            }
            if (!addNodeToList(node, &g_strm_buff_list_used)) {
                retval = NULL;
                goto done;
            }

            retval = node->buffer;
            goto done;
        }
    }

done:
    pthread_mutex_unlock(&g_strm_buff_list_used.mutex);
    pthread_mutex_unlock(&g_strm_buff_list_free.mutex);
    return retval;
}

static void allocSomeNodesForFreeList(size_t sz, int numa, int pinned)
{
    StreamBuffNode_T *node;
    int i;
    for (i = 0; i < STREAM_BUFF_LIST_SZ; ++i) {
        node = malloc(sizeof(StreamBuffNode_T));
        if (NULL == node) {
            break;
        }

        node->buffer = qzMalloc(sz, numa, pinned);
        if (NULL == node->buffer) {
            free(node);
            break;
        }
        node->pinned = pinned;
        node->size = sz;

        if (unlikely(0 != pthread_mutex_lock(&g_strm_buff_list_free.mutex))) {
            QZ_ERROR("Failed to get Mutex Lock.\n");
            free(node);
            return;
        }

        if (!addNodeToList(node, &g_strm_buff_list_free)) {
            free(node);
            if (unlikely(0 != pthread_mutex_unlock(&g_strm_buff_list_free.mutex))) {
                QZ_ERROR("Failed to release Mutex Lock.\n");
                return;
            }
        }

        if (unlikely(0 != pthread_mutex_unlock(&g_strm_buff_list_free.mutex))) {
            QZ_ERROR("Failed to release Mutex Lock.\n");
            free(node);
            return;
        }
    }
}

static void *streamBufferAlloc(size_t sz, int numa, int pinned)
{
    StreamBuffNode_T *node = getNodeBuffFromFreeList(sz, pinned);
    if (NULL == node) { //try to add some nodes to free list
        allocSomeNodesForFreeList(sz, numa, pinned);
        node = getNodeBuffFromFreeList(sz, pinned);
    }
    return node;
}

static void streamBufferFree(void *addr)
{
    StreamBuffNode_T *node;

    pthread_mutex_lock(&g_strm_buff_list_free.mutex);
    pthread_mutex_lock(&g_strm_buff_list_used.mutex);
    for (node = g_strm_buff_list_used.head; node != NULL; node = node->next) {
        if (addr == node->buffer) {
            if (removeNodeFromList(node, &g_strm_buff_list_used) == FAILURE) {
                QZ_ERROR("Fail to remove Node for streamBufferFree");
                goto done;
            }
            if (STREAM_BUFF_LIST_SZ <= g_strm_buff_list_free.size) {
                qzFree(node->buffer);
                free(node);
            } else {
                addNodeToList(node, &g_strm_buff_list_free);
            }
            addr = NULL;
            goto done;
        }
    }

done:
    pthread_mutex_unlock(&g_strm_buff_list_used.mutex);
    pthread_mutex_unlock(&g_strm_buff_list_free.mutex);
}

int initStream(QzSession_T *sess, QzStream_T *strm)
{
    int rc = QZ_FAIL;
    QzSess_T *qz_sess = NULL;
    QzStreamBuf_T *stream_buf = NULL;

    if (NULL != strm->opaque) {
        return QZ_DUPLICATE;
    }

    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        return QZ_FAIL;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSessionDeflate(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return QZ_FAIL;
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
    stream_buf->in_offset = 0;
    stream_buf->flush_more = 0;
    stream_buf->buf_len = qz_sess->sess_params.strm_buff_sz;
    stream_buf->in_buf =
        streamBufferAlloc(stream_buf->buf_len, NODE_0, PINNED_MEM);
    stream_buf->out_buf =
        streamBufferAlloc(stream_buf->buf_len, NODE_0, PINNED_MEM);

    if (NULL == stream_buf->in_buf) {
        QZ_DEBUG("stream_buf->in_buf : PINNED_MEM failed, try COMMON_MEM\n");
        stream_buf->in_buf =
            streamBufferAlloc(stream_buf->buf_len, NODE_0, COMMON_MEM);
    }
    if (NULL == stream_buf->out_buf) {
        QZ_DEBUG("stream_buf->out_buf : PINNED_MEM failed, try COMMON_MEM\n");
        stream_buf->out_buf =
            streamBufferAlloc(stream_buf->buf_len, NODE_0, COMMON_MEM);
    }

    if (NULL == stream_buf->in_buf ||
        NULL == stream_buf->out_buf) {
        goto clear;
    }
    QZ_DEBUG("Allocate stream buf %u\n", stream_buf->buf_len);

    strm->pending_in = 0;
    strm->pending_out = 0;
    strm->crc_32 = 0;
    return QZ_OK;

clear:
    if (NULL == stream_buf->in_buf) {
        QZ_ERROR("Fail to allocate memory for in_buf of QzStreamBuf");
    } else {
        qzFree(stream_buf->in_buf);
    }

    if (NULL == stream_buf->out_buf) {
        QZ_ERROR("Fail to allocate memory for out_buf of QzStreamBuf");
    } else {
        qzFree(stream_buf->out_buf);
    }
    free(stream_buf);
    stream_buf = NULL;
    strm->opaque = NULL;
    return QZ_FAIL;
}

static unsigned int copyStreamInput(QzStream_T *strm, unsigned char *in)
{
    unsigned int cpy_cnt = 0;
    unsigned int avail_in = 0;
    QzStreamBuf_T *stream_buf = strm->opaque;

    avail_in = stream_buf->buf_len - strm->pending_in;
    cpy_cnt = (strm->in_sz > avail_in) ? avail_in : strm->in_sz;
    QZ_MEMCPY(stream_buf->in_buf + strm->pending_in, in, cpy_cnt, strm->in_sz);
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
    QZ_MEMCPY(out, stream_buf->out_buf + stream_buf->out_offset, avail_out,
              cpy_cnt);
    QZ_DEBUG("copy %u to user output\n", cpy_cnt);

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
    unsigned int copied_input_last = 0;
    unsigned int copy_more = 1;
    unsigned int inbuf_offset = 0;
    unsigned int consumed = 0;
    unsigned int produced = 0;
    unsigned int strm_last = 0;
    QzStreamBuf_T *stream_buf = NULL;
    QzSess_T *qz_sess = NULL;
    DataFormatInternal_T data_fmt = DEFLATE_GZIP_EXT;

    if (NULL == sess     || \
        NULL == strm     || \
        (last != 0 && last != 1)) {
        rc = QZ_PARAMS;
        if (NULL != strm) {
            strm->in_sz = 0;
            strm->out_sz = 0;
        }
        goto end;
    }

    if (NULL == strm->out) {
        rc = QZ_PARAMS;
        strm->in_sz = 0;
        strm->out_sz = 0;
        goto end;
    }

    if (NULL == strm->in && \
        strm->in_sz > 0) {
        rc = QZ_PARAMS;
        strm->in_sz = 0;
        strm->out_sz = 0;
        goto end;
    }

    if (NULL == strm->opaque) {
        rc = initStream(sess, strm);
        if (QZ_OK != rc) {
            goto done;
        }
    }
    switch (strm->crc_type) {
    case QZ_CRC32:
    case QZ_ADLER:
    default:
        strm_crc = (unsigned long *)&strm->crc_32;
        break;
    }

    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        return QZ_FAIL;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSessionDeflate(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            strm->in_sz = 0;
            strm->out_sz = 0;
            return QZ_FAIL;
        }
    }
    qz_sess = (QzSess_T *)(sess->internal);
    data_fmt = qz_sess->sess_params.data_fmt;
    if (data_fmt != DEFLATE_RAW &&
        data_fmt != DEFLATE_GZIP_EXT) {
        QZ_ERROR("Invalid data format: %d\n", data_fmt);
        strm->in_sz = 0;
        strm->out_sz = 0;
        return QZ_PARAMS;
    }

    stream_buf = (QzStreamBuf_T *) strm->opaque;
    while (strm->pending_out > 0) {
        copied_output = copyStreamOutput(strm, strm->out + produced);
        produced += copied_output;
        if (0 == copied_output ||
            (0 == strm->pending_out &&
             0 == strm->pending_in &&
             0 == strm->in_sz)) {
            rc = QZ_OK;
            /* When pending_out and pending_in are all greater than zero, we
             * set the flush_more flag to indicate that we should not copy more
             * input and should do the process(compression or decompression) */
            if (strm->pending_in > 0) {
                stream_buf->flush_more = 1;
            }
            goto done;
        }
    }

    while (0 == strm->pending_out) {

        if (copy_more == 1 && stream_buf->flush_more != 1) {
            copied_input_last = copied_input;
            // Note, strm->in == NULL and strm->in_sz == 0, will not cause
            // copyStreamInput failed, but it's Dangerous behavior.
            if (NULL != strm->in) {
                copied_input += copyStreamInput(strm, strm->in + consumed);
            }

            if (strm->pending_in < stream_buf->buf_len &&
                last != 1) {
                rc = QZ_OK;
                goto done;
            } else {
                copy_more = 0;
            }
        }

        input_len = strm->pending_in;
        output_len = stream_buf->buf_len;
        if (stream_buf->flush_more == 1) {
            inbuf_offset = stream_buf->in_offset;
            stream_buf->flush_more = 0;
        }

        strm_last = (0 == strm->in_sz && last) ? 1 : 0;
        QZ_DEBUG("Before Call qzCompressCrc input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);

        rc = qzCompressCrc(sess, stream_buf->in_buf + inbuf_offset, &input_len,
                           stream_buf->out_buf, &output_len, strm_last, strm_crc);

        strm->pending_in -= input_len;
        strm->pending_out = output_len;
        copied_output = copyStreamOutput(strm, strm->out + produced);
        consumed = copied_input;
        produced += copied_output;
        inbuf_offset += input_len;
        stream_buf->in_offset = inbuf_offset;

        QZ_DEBUG("After Call qzCompressCrc input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);

        if (QZ_BUF_ERROR == rc) {
            if (0 == input_len) {
                QZ_ERROR("ERROR in copy stream output, stream buf size = %u\n",
                         stream_buf->buf_len);
                copied_input = copied_input_last;
                rc = QZ_FAIL;
                goto done;
            } else {
                rc = QZ_OK;
            }
        }

        if (QZ_OK != rc) {
            copied_input = copied_input_last;
            rc = QZ_FAIL;
            goto done;
        }

        if (0 == strm->pending_in) {
            copy_more = 1;
            inbuf_offset = 0;
        }


        if (0 == strm->pending_in && 0 == strm->in_sz) {
            rc = QZ_OK;
            goto done;
        }
    }

done:

    strm->in_sz = copied_input;
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
    unsigned int copied_input_last = 0;
    unsigned int consumed = 0;
    unsigned int produced = 0;
    unsigned int copy_more = 1;
    unsigned int inbuf_offset = 0;
    QzStreamBuf_T *stream_buf = NULL;

    if (NULL == sess     || \
        NULL == strm     || \
        (last != 0 && last != 1)) {
        rc = QZ_PARAMS;
        if (NULL != strm) {
            strm->in_sz = 0;
            strm->out_sz = 0;
        }
        goto end;
    }

    if (NULL == strm->in || \
        NULL == strm->out) {
        rc = QZ_PARAMS;
        strm->in_sz = 0;
        strm->out_sz = 0;
        goto end;
    }

    if (NULL == strm->opaque) {
        rc = initStream(sess, strm);
        if (QZ_OK != rc) {
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
            if (strm->pending_in > 0) {
                /* We need to handle all the input that has left pending in the input buffer next time we are called.
                 * Otherwise we'd append additional bits to the pending data, violate the buffer's boundary and
                 * corrupt the memory behind the boundary. */
                stream_buf->flush_more = 1;
            }
            QZ_DEBUG("No space for pending output...\n");
            goto done;
        }
        QZ_DEBUG("Copy output %u bytes\n", copied_output);
    }

    while (0 == strm->pending_out) {

        if (1 == copy_more && stream_buf->flush_more != 1) {
            copied_input_last = copied_input;
            copied_input += copyStreamInput(strm, strm->in + consumed);

            if (strm->pending_in < stream_buf->buf_len &&
                last != 1) {
                rc = QZ_OK;
                QZ_DEBUG("Batch more input data...\n");
                goto done;
            } else {
                copy_more = 0;
            }
        }

        if (stream_buf->flush_more == 1) {
            /* We need to flush all the input that has left pending in the input buffer since the previous call to this function.
             * Otherwise we'd append additional bits to the pending data, violate the buffer's boundary and
             * corrupt the memory behind the boundary. */
            stream_buf->flush_more = 0;
            inbuf_offset = stream_buf->in_offset;
        }

        input_len = strm->pending_in;
        output_len = stream_buf->buf_len;

        QZ_DEBUG("Before Call qzDecompress input_len %u output_len %u "
                 "stream->pending_in %u stream->pending_out %u "
                 "stream->in_sz %d stream->out_sz %d\n",
                 input_len, output_len, strm->pending_in, strm->pending_out,
                 strm->in_sz, strm->out_sz);
        rc = qzDecompress(sess, stream_buf->in_buf + inbuf_offset, &input_len,
                          stream_buf->out_buf, &output_len);

        QZ_DEBUG("Return code = %d\n", rc);
        if (QZ_OK != rc && QZ_BUF_ERROR != rc) {
            copied_input = copied_input_last;
            goto done;
        }

        inbuf_offset += input_len;
        stream_buf->in_offset = inbuf_offset;
        consumed = copied_input;
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
            if (0 == input_len) {
                QZ_ERROR("Error in qzDecompressStream, stream buf size = %u\n",
                         stream_buf->buf_len);
                copied_input = copied_input_last;
                rc = QZ_FAIL;
                goto done;
            } else {
                QZ_DEBUG("Recoverable buffer error occurs... \n");
                rc = QZ_OK;
            }
        }

        if (0 == strm->pending_in) {
            copy_more = 1;
            inbuf_offset = 0;
        }
        if (0 == strm->pending_in && 0 == strm->in_sz) {
            rc = QZ_OK;
            goto done;
        }
    }

done:

    strm->in_sz = copied_input;
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
    streamBufferFree(stream_buf->out_buf);
    streamBufferFree(stream_buf->in_buf);
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
