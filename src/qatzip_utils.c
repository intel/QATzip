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

#include <stdlib.h>
#include <assert.h>
#include <qz_utils.h>

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#endif

#include <qatzip.h>
#include "qatzip_internal.h"

static QatThread_T g_qat_thread;
extern processData_T g_process;

#ifdef QATZIP_DEBUG
static void doInsertThread(unsigned int th_id,
                           ThreadList_T **thd_list,
                           unsigned int *num_thd,
                           pthread_mutex_t *lock,
                           Serv_T serv_type,
                           Engine_T engine_type)
{
    ThreadList_T *node;
    ThreadList_T *prev_node;

    if (0 != pthread_mutex_lock(lock)) {
        return;
    }

    for (prev_node = node = *thd_list; node; node = node->next) {
        if (node->thread_id == th_id) {
            break;
        }
        prev_node = node;
    }

    if (!node) {
        node = (ThreadList_T *)calloc(1, sizeof(*node));
        if (!node) {
            QZ_ERROR("[ERROR]: alloc memory failed in file(%s) line(%d)\n",
                     __FILE__, __LINE__);
            goto done;
        }

        node->thread_id = th_id;
        ++*num_thd;
        if (prev_node) {
            prev_node->next = node;
        } else {
            *thd_list = node;
        }
    }

    if (SW == engine_type) {
        if (COMPRESSION == serv_type) {
            ++node->comp_sw_count;
        } else if (DECOMPRESSION == serv_type) {
            ++node->decomp_sw_count;
        }
    } else if (HW == engine_type) {
        if (COMPRESSION == serv_type) {
            ++node->comp_hw_count;
        } else if (DECOMPRESSION == serv_type) {
            ++node->decomp_hw_count;
        }
    }

done:
    (void)pthread_mutex_unlock(lock);
}

void insertThread(unsigned int th_id,
                  Serv_T serv_type,
                  Engine_T engine_type)
{
    QatThread_T *th_list = &g_qat_thread;
    if (COMPRESSION == serv_type) {
        doInsertThread(th_id,
                       &th_list->comp_th_list,
                       &th_list->num_comp_th,
                       &th_list->comp_lock,
                       serv_type,
                       engine_type);
    } else if (DECOMPRESSION == serv_type) {
        doInsertThread(th_id,
                       &th_list->decomp_th_list,
                       &th_list->num_decomp_th,
                       &th_list->decomp_lock,
                       serv_type,
                       engine_type);
    }
}

static void doDumpThreadInfo(ThreadList_T *node,
                             unsigned int num_node,
                             Serv_T type)
{
    unsigned int i;
    char *serv_title;

    if (num_node > 0) {
        i = 0;
        if (COMPRESSION == type) {
            serv_title = "Compression";
        } else {
            serv_title = "Decompression";
        }

        QZ_PRINT("[INFO]: %s num_th %u\n",
                 serv_title, num_node);
        while (node) {
            QZ_PRINT("th_id: %u comp_hw_count: %u comp_sw_count: %u "
                     "decomp_hw_count: %u decomp_sw_count: %u\n",
                     node->thread_id,
                     node->comp_hw_count,
                     node->comp_sw_count,
                     node->decomp_hw_count,
                     node->decomp_sw_count);
            i++;
            node = node->next;
            if (i == num_node) {
                break;
            }
        }

        if (node) {
            QZ_ERROR("[ERROR]: there's node left in the list\n");
        }
        QZ_PRINT("\n");
    }
}

void dumpThreadInfo(void)
{
    QatThread_T *th_list = &g_qat_thread;
    doDumpThreadInfo(th_list->comp_th_list,
                     th_list->num_comp_th,
                     COMPRESSION);
    doDumpThreadInfo(th_list->decomp_th_list,
                     th_list->num_decomp_th,
                     DECOMPRESSION);
}
#endif

void initDebugLock(void)
{
    pthread_mutex_init(&g_qat_thread.comp_lock, NULL);
    pthread_mutex_init(&g_qat_thread.decomp_lock, NULL);
}

static int qzSetupDcSessionData(CpaDcSessionSetupData *session_setup_data,
                                QzSessionParamsInternal_T *params)
{
    assert(session_setup_data);
    assert(params);

    session_setup_data->compLevel = params->comp_lvl;

    switch (params->data_fmt) {
    case DEFLATE_4B:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_RAW:
        session_setup_data->compType = CPA_DC_DEFLATE;
        session_setup_data->checksum = CPA_DC_CRC32;
        session_setup_data->autoSelectBestHuffmanTree =
            CPA_DC_ASB_UNCOMP_STATIC_DYNAMIC_WITH_STORED_HDRS;
        if (params->huffman_hdr == QZ_DYNAMIC_HDR) {
            session_setup_data->huffType = CPA_DC_HT_FULL_DYNAMIC;
        } else {
            session_setup_data->huffType = CPA_DC_HT_STATIC;
        }
        break;
    case LZ4_FH:
#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
        session_setup_data->compType = CPA_DC_LZ4;
        session_setup_data->checksum = CPA_DC_XXHASH32;
        session_setup_data->lz4BlockChecksum = 0;
        session_setup_data->lz4BlockMaxSize = CPA_DC_LZ4_MAX_BLOCK_SIZE_64K;
        break;
#else
        QZ_ERROR("QAT driver does not support lz4 algorithm\n");
        return QZ_UNSUPPORTED_FMT;
#endif
    case LZ4S_BK:
#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
        session_setup_data->compType = CPA_DC_LZ4S;
        session_setup_data->checksum = CPA_DC_XXHASH32;
        if (params->lz4s_mini_match == 4) {
            session_setup_data->minMatch = CPA_DC_MIN_4_BYTE_MATCH;
        } else {
            session_setup_data->minMatch = CPA_DC_MIN_3_BYTE_MATCH;
        }
        break;
#else
        QZ_ERROR("QAT driver does not support lz4s algorithm\n");
        return QZ_UNSUPPORTED_FMT;
#endif
    default:
        return QZ_UNSUPPORTED_FMT;
    }

    switch (params->direction) {
    case QZ_DIR_COMPRESS:
        session_setup_data->sessDirection = CPA_DC_DIR_COMPRESS;
        break;
    case QZ_DIR_DECOMPRESS:
        session_setup_data->sessDirection = CPA_DC_DIR_DECOMPRESS;
        break;
    default:
        session_setup_data->sessDirection = CPA_DC_DIR_COMBINED;
    }

    session_setup_data->sessState = CPA_DC_STATELESS;
#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
    session_setup_data->windowSize = (Cpa32U)7;
#else
    session_setup_data->deflateWindowSize = (Cpa32U)7;
    session_setup_data->fileType = CPA_DC_FT_ASCII;
#endif

    return QZ_OK;
}

int qzSetupSessionInternal(QzSession_T *sess)
{
    int rc;
    QzSess_T *qz_sess;

    assert(sess);
    assert(sess->internal);

    qz_sess = (QzSess_T *)sess->internal;

    rc = qzSetupDcSessionData(&qz_sess->session_setup_data,
                              &qz_sess->sess_params);
    if (rc != QZ_OK) {
        return rc;
    }

    qz_sess->inst_hint = -1;
    qz_sess->seq = 0;
    qz_sess->seq_in = 0;
    qz_sess->polling_idx = 0;
    qz_sess->force_sw = 0;
    qz_sess->inflate_strm = NULL;
    qz_sess->inflate_stat = InflateNull;
    qz_sess->deflate_strm = NULL;
    qz_sess->deflate_stat = DeflateNull;
    qz_sess->cctx = NULL;
    qz_sess->dctx = NULL;

    if (g_process.qz_init_status != QZ_OK) {
        /*hw not present*/
        if (qz_sess->sess_params.sw_backup == 1) {
            sess->hw_session_stat = QZ_NO_HW;
            rc = QZ_OK;
        } else {
            sess->hw_session_stat = QZ_NOSW_NO_HW;
            rc = QZ_NOSW_NO_HW;
        }
    } else {
        sess->hw_session_stat = QZ_OK;
        rc = QZ_OK;
    }

    return rc;
}

int qzCheckParams(QzSessionParams_T *params)
{
    assert(params);

    if (params->huffman_hdr > QZ_STATIC_HDR) {
        QZ_ERROR("Invalid huffman_hdr value\n");
        return QZ_PARAMS;
    }

    if ((params->comp_lvl < QZ_DEFLATE_COMP_LVL_MINIMUM) ||
        (params->comp_lvl > QZ_DEFLATE_COMP_LVL_MAXIMUM)) {
        QZ_ERROR("Invalid comp_lvl value\n");
        return QZ_PARAMS;
    }

    if (params->direction > QZ_DIR_BOTH) {
        QZ_ERROR("Invalid direction value\n");
        return QZ_PARAMS;
    }

    if (params->comp_algorithm != QZ_DEFLATE) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if (params->sw_backup > 1) {
        QZ_ERROR("Invalid sw_backup value\n");
        return QZ_PARAMS;
    }

    if ((params->hw_buff_sz < QZ_HW_BUFF_MIN_SZ) ||
        (params->hw_buff_sz > QZ_HW_BUFF_MAX_SZ)) {
        QZ_ERROR("Invalid hw_buff_sz value\n");
        return QZ_PARAMS;
    }

    if ((params->strm_buff_sz < QZ_STRM_BUFF_MIN_SZ) ||
        (params->strm_buff_sz > QZ_STRM_BUFF_MAX_SZ)) {
        QZ_ERROR("Invalid strm_buff_sz value\n");
        return QZ_PARAMS;
    }

    if (params->input_sz_thrshold < QZ_COMP_THRESHOLD_MINIMUM) {
        QZ_ERROR("Invalid input_sz_thrshold value\n");
        return QZ_PARAMS;
    }

    if ((params->req_cnt_thrshold  < QZ_REQ_THRESHOLD_MINIMUM) ||
        (params->req_cnt_thrshold  > QZ_REQ_THRESHOLD_MAXIMUM)) {
        QZ_ERROR("Invalid req_cnt_thrshold value\n");
        return QZ_PARAMS;
    }

    if (params->hw_buff_sz & (params->hw_buff_sz - 1)) {
        QZ_ERROR("Invalid hw_buff_sz value, must be a power of 2k\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
}

static int qzCheckParamsCommon(QzSessionParamsCommon_T *params)
{
    assert(params);

    if (params->direction > QZ_DIR_BOTH) {
        QZ_ERROR("Invalid direction value\n");
        return QZ_PARAMS;
    }

    if ((params->comp_algorithm != QZ_DEFLATE) &&
        (params->comp_algorithm != QZ_LZ4) &&
        (params->comp_algorithm != QZ_LZ4s) &&
        (params->comp_algorithm != QZ_ZSTD)) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if (params->sw_backup > 1) {
        QZ_ERROR("Invalid sw_backup value\n");
        return QZ_PARAMS;
    }

    if ((params->hw_buff_sz < QZ_HW_BUFF_MIN_SZ) ||
        (params->hw_buff_sz > QZ_HW_BUFF_MAX_SZ)) {
        QZ_ERROR("Invalid hw_buff_sz value\n");
        return QZ_PARAMS;
    }

    if ((params->strm_buff_sz < QZ_STRM_BUFF_MIN_SZ) ||
        (params->strm_buff_sz > QZ_STRM_BUFF_MAX_SZ)) {
        QZ_ERROR("Invalid strm_buff_sz value\n");
        return QZ_PARAMS;
    }

    if (params->input_sz_thrshold < QZ_COMP_THRESHOLD_MINIMUM) {
        QZ_ERROR("Invalid input_sz_thrshold value\n");
        return QZ_PARAMS;
    }

    if ((params->req_cnt_thrshold  < QZ_REQ_THRESHOLD_MINIMUM) ||
        (params->req_cnt_thrshold  > QZ_REQ_THRESHOLD_MAXIMUM)) {
        QZ_ERROR("Invalid req_cnt_thrshold value\n");
        return QZ_PARAMS;
    }

    if (params->hw_buff_sz & (params->hw_buff_sz - 1)) {
        QZ_ERROR("Invalid hw_buff_sz value, must be a power of 2k\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
}

int qzCheckParamsDeflate(QzSessionParamsDeflate_T *params)
{
    assert(params);

    if (qzCheckParamsCommon(&params->common_params) != QZ_OK) {
        return QZ_PARAMS;
    }

    if (params->common_params.comp_algorithm != QZ_DEFLATE) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if (params->huffman_hdr > QZ_STATIC_HDR) {
        QZ_ERROR("Invalid huffman_hdr value\n");
        return QZ_PARAMS;
    }

    if ((params->common_params.comp_lvl < QZ_LZS_COMP_LVL_MINIMUM) ||
        (params->common_params.comp_lvl > QZ_DEFLATE_COMP_LVL_MAXIMUM_Gen3)) {
        QZ_ERROR("Invalid comp_lvl value\n");
        return QZ_PARAMS;
    }

    if (params->data_fmt > QZ_DEFLATE_RAW) {
        QZ_ERROR("Invalid data_fmt value\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
}

int qzCheckParamsLZ4(QzSessionParamsLZ4_T *params)
{
    assert(params);

    if (qzCheckParamsCommon(&params->common_params) != QZ_OK) {
        return QZ_PARAMS;
    }

    if (params->common_params.comp_algorithm != QZ_LZ4) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if ((params->common_params.comp_lvl < QZ_LZS_COMP_LVL_MINIMUM) ||
        (params->common_params.comp_lvl > QZ_LZS_COMP_LVL_MAXIMUM)) {
        QZ_ERROR("Invalid comp_lvl value\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
}

int qzCheckParamsLZ4S(QzSessionParamsLZ4S_T *params)
{
    assert(params);

    if (qzCheckParamsCommon(&params->common_params) != QZ_OK) {
        return QZ_PARAMS;
    }

    if (params->common_params.comp_algorithm != QZ_LZ4s) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if ((params->common_params.comp_lvl < QZ_LZS_COMP_LVL_MINIMUM) ||
        (params->common_params.comp_lvl > QZ_LZS_COMP_LVL_MAXIMUM)) {
        QZ_ERROR("Invalid comp_lvl value\n");
        return QZ_PARAMS;
    }

    if ((params->lz4s_mini_match < 3) ||
        (params->lz4s_mini_match > 4)) {
        QZ_ERROR("Invalid lz4s_mini_match value\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
}

static void qzSetParamsCommon(QzSessionParamsCommon_T *params,
                              QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    internal_params->direction = params->direction;
    internal_params->comp_lvl = params->comp_lvl;
    internal_params->comp_algorithm = params->comp_algorithm;
    internal_params->max_forks = params->max_forks;
    internal_params->sw_backup = params->sw_backup;
    internal_params->hw_buff_sz = params->hw_buff_sz;
    internal_params->strm_buff_sz = params->strm_buff_sz;
    internal_params->input_sz_thrshold = params->input_sz_thrshold;
    internal_params->req_cnt_thrshold = params->req_cnt_thrshold;
    internal_params->wait_cnt_thrshold = params->wait_cnt_thrshold;
    internal_params->polling_mode = params->polling_mode;
    internal_params->is_sensitive_mode = params->is_sensitive_mode;
}

/**
 * Set session params into internal params
 */
void qzSetParams(QzSessionParams_T *params,
                 QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    internal_params->direction = params->direction;
    internal_params->comp_lvl = params->comp_lvl;
    internal_params->comp_algorithm = params->comp_algorithm;
    internal_params->max_forks = params->max_forks;
    internal_params->sw_backup = params->sw_backup;
    internal_params->hw_buff_sz = params->hw_buff_sz;
    internal_params->strm_buff_sz = params->strm_buff_sz;
    internal_params->input_sz_thrshold = params->input_sz_thrshold;
    internal_params->req_cnt_thrshold = params->req_cnt_thrshold;
    internal_params->wait_cnt_thrshold = params->wait_cnt_thrshold;

    if (params->data_fmt == QZ_DEFLATE_4B) {
        internal_params->data_fmt = DEFLATE_4B;
    } else if (params->data_fmt == QZ_DEFLATE_GZIP) {
        internal_params->data_fmt = DEFLATE_GZIP;
    } else if (params->data_fmt == QZ_DEFLATE_RAW) {
        internal_params->data_fmt = DEFLATE_RAW;
    } else {
        internal_params->data_fmt = DEFLATE_GZIP_EXT;
    }

    internal_params->huffman_hdr = params->huffman_hdr;
}

/**
 * Set deflate session params into internal params
 */
void qzSetParamsDeflate(QzSessionParamsDeflate_T *params,
                        QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    qzSetParamsCommon(&params->common_params, internal_params);

    if (params->data_fmt == QZ_DEFLATE_4B) {
        internal_params->data_fmt = DEFLATE_4B;
    } else if (params->data_fmt == QZ_DEFLATE_GZIP) {
        internal_params->data_fmt = DEFLATE_GZIP;
    } else if (params->data_fmt == QZ_DEFLATE_RAW) {
        internal_params->data_fmt = DEFLATE_RAW;
    } else {
        internal_params->data_fmt = DEFLATE_GZIP_EXT;
    }

    internal_params->huffman_hdr = params->huffman_hdr;
}

void qzSetParamsLZ4(QzSessionParamsLZ4_T *params,
                    QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    qzSetParamsCommon(&params->common_params, internal_params);
    internal_params->data_fmt = LZ4_FH;
}

/**
 * Set LZ4S session params into internal params
 */
void qzSetParamsLZ4S(QzSessionParamsLZ4S_T *params,
                     QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    qzSetParamsCommon(&params->common_params, internal_params);
    internal_params->data_fmt = LZ4S_BK;
    internal_params->qzCallback = params->qzCallback;
    internal_params->qzCallback_external = params->qzCallback_external;
    internal_params->lz4s_mini_match = params->lz4s_mini_match;
}

/**
 * Get session params from internal params
 */
void qzGetParams(QzSessionParamsInternal_T *internal_params,
                 QzSessionParams_T *params)
{
    assert(params);
    assert(internal_params);

    params->direction = internal_params->direction;
    params->comp_lvl = internal_params->comp_lvl;
    params->comp_algorithm = internal_params->comp_algorithm;
    params->max_forks = internal_params->max_forks;
    params->sw_backup = internal_params->sw_backup;
    params->hw_buff_sz = internal_params->hw_buff_sz;
    params->strm_buff_sz = internal_params->strm_buff_sz;
    params->input_sz_thrshold = internal_params->input_sz_thrshold;
    params->req_cnt_thrshold = internal_params->req_cnt_thrshold;
    params->wait_cnt_thrshold = internal_params->wait_cnt_thrshold;

    if (internal_params->data_fmt == DEFLATE_4B) {
        params->data_fmt = QZ_DEFLATE_4B;
    } else if (internal_params->data_fmt == DEFLATE_GZIP) {
        params->data_fmt = QZ_DEFLATE_GZIP;
    } else if (internal_params->data_fmt == DEFLATE_RAW) {
        params->data_fmt = QZ_DEFLATE_RAW;
    } else {
        params->data_fmt = QZ_DEFLATE_GZIP_EXT;
    }

    params->huffman_hdr = internal_params->huffman_hdr;
}

static void qzGetParamsCommon(QzSessionParamsInternal_T *internal_params,
                              QzSessionParamsCommon_T *params)
{
    assert(params);
    assert(internal_params);

    params->direction = internal_params->direction;
    params->comp_lvl = internal_params->comp_lvl;
    params->comp_algorithm = internal_params->comp_algorithm;
    params->max_forks = internal_params->max_forks;
    params->sw_backup = internal_params->sw_backup;
    params->hw_buff_sz = internal_params->hw_buff_sz;
    params->strm_buff_sz = internal_params->strm_buff_sz;
    params->input_sz_thrshold = internal_params->input_sz_thrshold;
    params->req_cnt_thrshold = internal_params->req_cnt_thrshold;
    params->wait_cnt_thrshold = internal_params->wait_cnt_thrshold;
    params->polling_mode = internal_params->polling_mode;
    params->is_sensitive_mode = internal_params->is_sensitive_mode;
}

/**
 * Get deflate session params from internal params
 */
void qzGetParamsDeflate(QzSessionParamsInternal_T *internal_params,
                        QzSessionParamsDeflate_T *params)
{
    assert(params);
    assert(internal_params);

    qzGetParamsCommon(internal_params, &params->common_params);

    if (internal_params->data_fmt == DEFLATE_4B) {
        params->data_fmt = QZ_DEFLATE_4B;
    } else if (internal_params->data_fmt == DEFLATE_GZIP) {
        params->data_fmt = QZ_DEFLATE_GZIP;
    } else if (internal_params->data_fmt == DEFLATE_RAW) {
        params->data_fmt = QZ_DEFLATE_RAW;
    } else {
        params->data_fmt = QZ_DEFLATE_GZIP_EXT;
    }

    params->huffman_hdr = internal_params->huffman_hdr;
    params->common_params.comp_algorithm = QZ_DEFLATE;
}

/**
 * Get LZ4 session params from internal params
 */
void qzGetParamsLZ4(QzSessionParamsInternal_T *internal_params,
                    QzSessionParamsLZ4_T *params)
{
    assert(params);
    assert(internal_params);

    qzGetParamsCommon(internal_params, &params->common_params);
    params->common_params.comp_algorithm = QZ_LZ4;
}

/**
 * Get LZ4S session params from internal params
 */
void qzGetParamsLZ4S(QzSessionParamsInternal_T *internal_params,
                     QzSessionParamsLZ4S_T *params)
{
    assert(params);
    assert(internal_params);

    qzGetParamsCommon(internal_params, &params->common_params);

    params->common_params.comp_algorithm = QZ_LZ4s;
    params->qzCallback = internal_params->qzCallback;
    params->qzCallback_external = internal_params->qzCallback_external;
    params->lz4s_mini_match = internal_params->lz4s_mini_match;
}

inline unsigned long outputFooterSz(DataFormatInternal_T data_fmt)
{
    unsigned long size = 0;
    switch (data_fmt) {
    case DEFLATE_4B:
    /* fall through */
    case DEFLATE_RAW:
        size = 0;
        break;
    case LZ4_FH:
        size = qzLZ4FooterSz();
        break;
    case LZ4S_BK:
        size = 0;
        break;
    case DEFLATE_GZIP_EXT:
    default:
        size = stdGzipFooterSz();
        break;
    }

    return size;
}

unsigned long outputHeaderSz(DataFormatInternal_T data_fmt)
{
    unsigned long size = 0;

    switch (data_fmt) {
    case DEFLATE_4B:
        size = qz4BHeaderSz();
        break;
    case DEFLATE_RAW:
        break;
    case DEFLATE_GZIP:
        size = stdGzipHeaderSz();
        break;
    case LZ4_FH:
        size = qzLZ4HeaderSz();
        break;
    case LZ4S_BK:
        size = qzLZ4SBlockHeaderSz();
        break;
    case DEFLATE_GZIP_EXT:
    default:
        size = qzGzipHeaderSz();
        break;
    }

    return size;
}

void outputHeaderGen(unsigned char *ptr,
                     CpaDcRqResults *res,
                     DataFormatInternal_T data_fmt)
{
    QZ_DEBUG("Generate header\n");

    switch (data_fmt) {
    case DEFLATE_4B:
        qz4BHeaderGen(ptr, res);
        break;
    case DEFLATE_RAW:
        break;
    case DEFLATE_GZIP:
        stdGzipHeaderGen(ptr, res);
        break;
    case LZ4_FH:
        qzLZ4HeaderGen(ptr, res);
        break;
    case LZ4S_BK:
        qzLZ4SBlockHeaderGen(ptr, res);
        break;
    case DEFLATE_GZIP_EXT:
    default:
        qzGzipHeaderGen(ptr, res);
        break;
    }
}

inline void outputFooterGen(QzSess_T *qz_sess,
                            CpaDcRqResults *res,
                            DataFormatInternal_T data_fmt)
{
    unsigned char *ptr = qz_sess->next_dest;
    switch (data_fmt) {
    case DEFLATE_RAW:
        break;
    case LZ4_FH:
        qzLZ4FooterGen(ptr, res);
        break;
    case LZ4S_BK:
        break;
    case DEFLATE_GZIP_EXT:
    default:
        qzGzipFooterGen(ptr, res);
        break;
    }
}

int isQATProcessable(const unsigned char *ptr,
                     const unsigned int *const src_len,
                     QzSess_T *const qz_sess)
{
    uint32_t rc = 0;
    DataFormatInternal_T data_fmt;
    assert(ptr != NULL);
    assert(src_len != NULL);
    assert(qz_sess != NULL);


    data_fmt = qz_sess->sess_params.data_fmt;
    switch (data_fmt) {
    case DEFLATE_4B:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
        rc = isQATDeflateProcessable(ptr, src_len, qz_sess);
        break;
    case LZ4_FH:
        rc = isQATLZ4Processable(ptr, src_len, qz_sess);
        break;
    default:
        rc = 0;
        break;
    }
    return rc;
}
