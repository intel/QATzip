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

        QZ_INFO("%s num_th %u\n", serv_title, num_node);
        while (node) {
            QZ_INFO("th_id: %u comp_hw_count: %u comp_sw_count: %u "
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
        QZ_INFO("\n");
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

QzLogLevel_T currentLogLevel = LOG_WARNING;

QzLogLevel_T qzSetLogLevel(QzLogLevel_T level)
{
    QzLogLevel_T oldLogLevel = currentLogLevel;
    currentLogLevel = level;
    return oldLogLevel;
}

const char *getLogLevelString(QzLogLevel_T level)
{
    switch (level) {
    case LOG_ERROR:
        return "Error";
    case LOG_WARNING:
        return "Warning";
    case LOG_INFO:
        return "Info";
    case LOG_DEBUG1:
        return "Debug";
    case LOG_DEBUG2:
        return "Test";
    case LOG_DEBUG3:
        return "Memory";
    default:
        return "UNKNOWN";
    }
}

void logMessage(QzLogLevel_T level, const char *file, int line,
                const char *format, ...)
{

    if (level > currentLogLevel) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    FILE *output = stderr;

    switch (level) {
    case LOG_NONE:
        output = stdout;
        break;
    case LOG_ERROR:
    case LOG_WARNING:
        output = stderr;
        fprintf(output, "[%s] [%s] (%s:%d): ", getLogLevelString(level), time_buffer,
                file, line);
        break;
    case LOG_INFO:
    case LOG_DEBUG1:
    case LOG_DEBUG2:
    case LOG_DEBUG3:
        output = stdout;
        fprintf(output, "[%s]: ", getLogLevelString(level));
        break;
    default:
        break;
    }

    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
}

void initDebugLock(void)
{
    pthread_mutex_init(&g_qat_thread.comp_lock, NULL);
    pthread_mutex_init(&g_qat_thread.decomp_lock, NULL);
}

/* LSM TODO:
 * expose global variabled (mention variable name) to API Level to tune
 * LSM Mode Performance lsm_sw_met_seed is the emprical mean of sw Latence
 */
unsigned int lsm_met_len_shift = 6;
unsigned int lsm_sw_met_seed = 1000;

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
    case DEFLATE_ZLIB:
        session_setup_data->compType = CPA_DC_DEFLATE;
        if (params->data_fmt == DEFLATE_ZLIB) {
            session_setup_data->checksum = CPA_DC_ADLER32;
        } else {
            session_setup_data->checksum = CPA_DC_CRC32;
        }
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

    /* LSM setup */
    if (qz_sess->sess_params.is_sensitive_mode == true) {
        metrixReset(&qz_sess->RRT);
        metrixReset(&qz_sess->PPT);
        metrixReset(&qz_sess->SWT);
        qz_sess->SWT.arr_avg = lsm_sw_met_seed;
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

int qzCheckParamsDeflateExt(QzSessionParamsDeflateExt_T *params)
{
    assert(params);
    if (qzCheckParamsCommon(&params->deflate_params.common_params) != QZ_OK) {
        return QZ_PARAMS;
    }

    if (params->deflate_params.common_params.comp_algorithm != QZ_DEFLATE) {
        QZ_ERROR("Invalid comp_algorithm value\n");
        return QZ_PARAMS;
    }

    if (params->deflate_params.huffman_hdr > QZ_STATIC_HDR) {
        QZ_ERROR("Invalid huffman_hdr value\n");
        return QZ_PARAMS;
    }

    if ((params->deflate_params.common_params.comp_lvl < QZ_LZS_COMP_LVL_MINIMUM) ||
        (params->deflate_params.common_params.comp_lvl >
         QZ_DEFLATE_COMP_LVL_MAXIMUM_Gen3)) {
        QZ_ERROR("Invalid comp_lvl value\n");
        return QZ_PARAMS;
    }
    QZ_DEBUG(" data_fmt value = %d , zlib_format = %d ",
             params->deflate_params.data_fmt, params->zlib_format);
    if ((params->zlib_format == 0) &&
        (params->deflate_params.data_fmt > QZ_DEFLATE_RAW)) {
        QZ_ERROR("Invalid data_fmt value\n");
        return QZ_PARAMS;
    }

    return QZ_OK;
    /*
        if (qzCheckParamsDeflate(&params->deflate_params) != QZ_OK) {
            return QZ_PARAMS;
        }

        return QZ_OK;
    */
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

    if (params->common_params.direction != QZ_DIR_COMPRESS) {
        QZ_ERROR("LZ4s only support compression direction\n");
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

/**
 * Set deflate ext session params into internal params
 */
void qzSetParamsDeflateExt(QzSessionParamsDeflateExt_T *params,
                           QzSessionParamsInternal_T *internal_params)
{
    assert(params);
    assert(internal_params);

    qzSetParamsDeflate(&params->deflate_params, internal_params);

    if (params->zlib_format == 1) {
        internal_params->data_fmt = DEFLATE_ZLIB;
        internal_params->zlib_format = 1;
    }
    internal_params->stop_decompression_stream_end =
        params->stop_decompression_stream_end;
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
 * Get deflateExt session params from internal params
 */
void qzGetParamsDeflateExt(QzSessionParamsInternal_T *internal_params,
                           QzSessionParamsDeflateExt_T *params)
{
    assert(params);
    assert(internal_params);

    qzGetParamsDeflate(internal_params, &params->deflate_params);
    if (internal_params->data_fmt == DEFLATE_ZLIB) {
        internal_params->zlib_format = 1;
    }

    params->stop_decompression_stream_end =
        internal_params->stop_decompression_stream_end;
    params->zlib_format = internal_params->zlib_format;
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
    /* LZ4s only support compression. */
    params->common_params.direction = QZ_DIR_COMPRESS;
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
    case DEFLATE_ZLIB:
        size = stdZlibFooterSz();
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
    case DEFLATE_ZLIB:
        size = stdZlibHeaderSz();
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
    case DEFLATE_ZLIB:
        stdZlibHeaderGen(ptr, res);
        break;
    case DEFLATE_GZIP_EXT:
    default:
        qzGzipHeaderGen(ptr, res);
        break;
    }
}

inline void outputFooterGen(unsigned char *ptr,
                            CpaDcRqResults *res,
                            DataFormatInternal_T data_fmt)
{
    switch (data_fmt) {
    case DEFLATE_RAW:
        break;
    case LZ4_FH:
        qzLZ4FooterGen(ptr, res);
        break;
    case LZ4S_BK:
        break;
    case DEFLATE_ZLIB:
        qzZlibFooterGen(ptr, res);
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
    case DEFLATE_ZLIB:
        rc = isQATDeflateProcessable(ptr, src_len, qz_sess);
        break;
    case LZ4_FH:
        rc = isQATLZ4Processable(ptr, src_len, qz_sess);
        break;
    case DEFLATE_RAW:
        if (*src_len < qz_sess->sess_params.input_sz_thrshold) {
            QZ_DEBUG("isQATProcessable: deflate_raw src_len is less than input threshhold\n");
            rc = 0;
        } else {
            rc = 1;
        }
        break;
    default:
        rc = 0;
        break;
    }
    return rc;
}

void RestoreDestCpastreamBuffer(int i, int j)
{
    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    }
}

void RestoreSrcCpastreamBuffer(int i, int j)
{
    if (g_process.qz_inst[i].stream[j].src_need_reset) {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_src;
        g_process.qz_inst[i].stream[j].src_need_reset = 0;
    }
}

void ResetCpastreamSink(int i, int j)
{
    g_process.qz_inst[i].stream[j].src1 = 0;
    g_process.qz_inst[i].stream[j].src2 = 0;
    g_process.qz_inst[i].stream[j].sink1 = 0;
    g_process.qz_inst[i].stream[j].sink2 = 0;
}

/*  This setup function will always match with buffer clean up function
*   during error offload flow.
*/
void compBufferSetup(int i, int j, QzSess_T *qz_sess,
                     unsigned char *src_ptr, unsigned int src_remaining,
                     unsigned int hw_buff_sz, unsigned int src_send_sz)
{
    unsigned int dest_receive_sz = 0;
    /* Get the qz_sess status of this request */
    CpaDcOpData *opData = NULL;
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    /* setup stream buffer */
    g_process.qz_inst[i].stream[j].seq = qz_sess->seq; /*update stream seq*/
    g_process.qz_inst[i].stream[j].res.checksum = 0;

    /* setup opData */
    opData = &g_process.qz_inst[i].stream[j].opData;
    opData->inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData->outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData->compressAndVerify = CPA_TRUE;
    if (IS_DEFLATE_RAW(data_fmt) && (1 != qz_sess->last ||
                                     src_remaining > hw_buff_sz)) {
        opData->flushFlag = CPA_DC_FLUSH_FULL;
    } else {
        opData->flushFlag = CPA_DC_FLUSH_FINAL;
    }

    QZ_DEBUG("sending seq number %d %d %ld, opData.flushFlag %d\n", i, j,
             qz_sess->seq, opData->flushFlag);
    /*Get feed src/dest buffer size*/
    dest_receive_sz = *qz_sess->dest_sz > DEST_SZ(src_send_sz) ?
                      DEST_SZ(src_send_sz) - outputHeaderSz(data_fmt) :
                      *qz_sess->dest_sz - outputHeaderSz(data_fmt);
    QZ_DEBUG("compBufferSetup dest_receive_sz = %d , src_send_sz = %d\n",
             dest_receive_sz, src_send_sz);
    g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
    g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
        dest_receive_sz;

    if (!need_cont_mem) {
        QZ_DEBUG("Compress SVM Enabled in doCompressIn\n");
    }

    /*Feed src/dest buffer*/
    if ((COMMON_MEM == qzMemFindAddr(src_ptr)) && need_cont_mem) {
        QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                  src_ptr,
                  src_send_sz,
                  src_remaining);
        g_process.qz_inst[i].stream[j].src_need_reset = 0;
    } else {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
        g_process.qz_inst[i].stream[j].src_need_reset = 1;
    }

    /*using zerocopy for the first request while dest buffer is pinned*/
    if (unlikely((0 == g_process.qz_inst[i].stream[j].seq) &&
                 (!need_cont_mem || qzMemFindAddr(qz_sess->next_dest)))) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            qz_sess->next_dest + outputHeaderSz(data_fmt);
        g_process.qz_inst[i].stream[j].dest_need_reset = 1;
    }
}

/*  when offload request failed after setup the buffer.
*   use this function to cleanup setup buffer.
*/
void compInBufferCleanUp(int i, int j)
{
    RestoreDestCpastreamBuffer(i, j);
    RestoreSrcCpastreamBuffer(i, j);
    g_process.qz_inst[i].stream[j].src1 -= 1;
    g_process.qz_inst[i].stream[j].src2 -= 1;
}

/*  when offload request successfully, Using below functions to process
*   clean up work during compressOut. There are clean up buffer function
*   in correct process flow or error process flow.
*/
void compOutSrcBufferCleanUp(int i, int j)
{
    RestoreSrcCpastreamBuffer(i, j);
}

void compOutErrorDestBufferCleanUp(int i, int j)
{
    RestoreDestCpastreamBuffer(i, j);
}

void compOutValidDestBufferCleanUp(int i, int j, QzSess_T *qz_sess,
                                   unsigned int dest_receive_sz)
{
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;

    if (!need_cont_mem) {
        QZ_DEBUG("Compress SVM Enabled in doCompressOut\n");
    }

    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    } else {
        QZ_MEMCPY(qz_sess->next_dest,
                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                  *qz_sess->dest_sz - qz_sess->qz_out_len,
                  dest_receive_sz);
    }
}

/*  Process respond means to reset or update sess status
*   And clean up buffer.
*/
void compOutProcessedRespond(int i, int j, QzSess_T *qz_sess)
{
    compOutSrcBufferCleanUp(i, j);
    /* Update the seq_in and process, clean buffer */
    assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
    g_process.qz_inst[i].stream[j].sink2++;
    qz_sess->processed++;
    qz_sess->seq_in++;
}

void compOutSkipErrorRespond(int i, int j, QzSess_T *qz_sess)
{
    compOutErrorDestBufferCleanUp(i, j);
    compOutProcessedRespond(i, j, qz_sess);
}

int compOutCheckDestLen(int i, int j, QzSession_T *sess,
                        long *dest_avail_len, long dest_receive_sz)
{
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    *dest_avail_len -= dest_receive_sz;
    if (unlikely(*dest_avail_len < 0)) {
        QZ_DEBUG("doCompressOut: inadequate output buffer length: %ld, outlen: %ld\n",
                 (long)(*qz_sess->dest_sz), qz_sess->qz_out_len);
        compOutSkipErrorRespond(i, j, qz_sess);
        qz_sess->stop_submitting = 1;
        sess->thd_sess_stat = QZ_BUF_ERROR;
        return sess->thd_sess_stat;
    }
    return QZ_OK;
}

/*To handle compression expansion*/
void swapDataBuffer(unsigned long i, int j)
{
    Cpa8U *p_tmp_data;

    p_tmp_data = g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;
    g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;
    g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = p_tmp_data;

    p_tmp_data = g_process.qz_inst[i].stream[j].orig_src;
    g_process.qz_inst[i].stream[j].orig_src =
        g_process.qz_inst[i].stream[j].orig_dest;
    g_process.qz_inst[i].stream[j].orig_dest = p_tmp_data;
}

/* Note:
* CheckHeader return value
* @retval QZ_OK                    Function executed successfully
* @retval QZ_FAIL                  Decompress stop, Fatal error
* @retval QZ_FORCE_SW              Decompress fallback sw
* @retval QZ_BUF_ERROR             Return to decompress API
* @retval QZ_DATA_ERROR            Return to decompress API
*/
int checkHeader(QzSess_T *qz_sess, unsigned char *src,
                unsigned int src_avail_len, unsigned int dest_avail_len,
                QzGzH_T *hdr)
{
    unsigned char *src_ptr = src;
    unsigned int compressed_sz = 0;
    unsigned int uncompressed_sz = 0;
    StdGzF_T *qzFooter = NULL;
    int isEndWithFooter = 0;
    int ret = 0;
    QzLZ4H_T *qzLZ4Header = NULL;
    QzLZ4F_T *lz4Footer = NULL;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;

    if ((src_avail_len <= 0) || (dest_avail_len <= 0)) {
        QZ_DEBUG("checkHeader: insufficient %s buffer length\n",
                 (dest_avail_len <= 0) ? "destation" : "source");
        return QZ_BUF_ERROR;
    }

    if (src_avail_len < outputHeaderSz(data_fmt)) {
        QZ_DEBUG("checkHeader: incomplete source buffer\n");
        return QZ_DATA_ERROR;
    }

    if (1 == qz_sess->force_sw) {
        return QZ_FORCE_SW;
    }

    switch (data_fmt) {
    case DEFLATE_GZIP:
        qzFooter = (StdGzF_T *)(findStdGzipFooter(src_ptr, src_avail_len));

        compressed_sz = (unsigned char *)qzFooter - src_ptr - stdGzipHeaderSz();
        uncompressed_sz = qzFooter->i_size;
        if ((unsigned char *)qzFooter == src_ptr + src_avail_len - stdGzipFooterSz()) {
            isEndWithFooter = 1;
        }
        QZ_DEBUG("checkHeader: DEFLATE_GZIP compressed_sz = %u , uncompressed_sz = %u \n",
                 compressed_sz, uncompressed_sz);
        break;
    case DEFLATE_GZIP_EXT:
        if (QZ_OK != qzGzipHeaderExt(src_ptr, hdr)) {
            return QZ_FAIL;
        }
        compressed_sz = (long)(hdr->extra.qz_e.dest_sz);
        uncompressed_sz = (long)(hdr->extra.qz_e.src_sz);
        QZ_DEBUG("checkHeader: DEFLATE_GZIP_EXT compressed_sz = %u , uncompressed_sz = %u \n",
                 compressed_sz, uncompressed_sz);

        break;
    case DEFLATE_4B:
        compressed_sz = *(int *)src_ptr;
        uncompressed_sz = (qz_sess->sess_params.hw_buff_sz > dest_avail_len) ?
                          dest_avail_len : qz_sess->sess_params.hw_buff_sz;
        break;
    case LZ4_FH:
        ret = qzVerifyLZ4FrameHeader(src_ptr, src_avail_len);
        if (ret != QZ_OK) {
            return ret;
        }

        lz4Footer = (QzLZ4F_T *)findLZ4Footer(src_ptr, src_avail_len);
        if (NULL == lz4Footer) {
            QZ_DEBUG("checkHeader: incomplete source buffer\n");
            return QZ_DATA_ERROR;
        }
        qzLZ4Header = (QzLZ4H_T *)src_ptr;
        compressed_sz = (unsigned char *)lz4Footer - src_ptr - qzLZ4HeaderSz();
        uncompressed_sz = qzLZ4Header->cnt_size;
        break;
    case DEFLATE_RAW:
        compressed_sz = *(qz_sess->src_sz);
        uncompressed_sz = *(qz_sess->dest_sz);
        QZ_DEBUG("checkHeader: DEFLATE_RAW HW Decompression enabled uncompressed_sz is %u \n",
                 uncompressed_sz);
        break;
    case DEFLATE_ZLIB:
        compressed_sz = src_avail_len - stdZlibFooterSz() - stdZlibHeaderSz();
        uncompressed_sz = (qz_sess->sess_params.hw_buff_sz > dest_avail_len) ?
                          dest_avail_len : qz_sess->sess_params.hw_buff_sz;
        QZ_DEBUG("checkHeader DEFLATE_ZLIB: compressed_sz %u  uncompressed_sz %u \n",
                 compressed_sz, uncompressed_sz);
        break;
    default:
        return QZ_FAIL;
    }

    if ((compressed_sz > DEST_SZ((long)(qz_sess->sess_params.hw_buff_sz))) ||
        (uncompressed_sz > qz_sess->sess_params.hw_buff_sz)) {
        if (1 == qz_sess->sess_params.sw_backup) {
            if (DEFLATE_GZIP == data_fmt &&
                1 == isEndWithFooter) {
                return QZ_FORCE_SW;
            }
            qz_sess->force_sw = 1;
            return QZ_FORCE_SW;
        } else {
            return QZ_FAIL;
        }
    }

    if (compressed_sz + outputHeaderSz(data_fmt) + outputFooterSz(data_fmt) >
        src_avail_len) {
        QZ_DEBUG("checkHeader: incomplete source buffer\n");
        return QZ_DATA_ERROR;
    } else if (uncompressed_sz > dest_avail_len) {
        QZ_DEBUG("checkHeader: insufficient destination buffer length\n");
        return QZ_BUF_ERROR;
    }
    hdr->extra.qz_e.dest_sz = compressed_sz;
    hdr->extra.qz_e.src_sz = uncompressed_sz;
    return QZ_OK;
}

/*  Below Buffer and respond process function is exact same means with
*   Compression
*/
void decompBufferSetup(int i, int j, QzSess_T *qz_sess,
                       unsigned char *src_ptr,
                       unsigned char *dest_ptr,
                       unsigned int src_avail_len,
                       QzGzH_T *hdr,
                       unsigned int *tmp_src_avail_len,
                       unsigned int *tmp_dest_avail_len)
{
    StdGzF_T *qzFooter = NULL;
    QzLZ4F_T *lz4Footer = NULL;
    StdZlibF_T *zlibFooter = NULL;
    unsigned int src_send_sz = 0;
    unsigned int dest_receive_sz = 0;
    int src_mem_type = qzMemFindAddr(src_ptr);
    int dest_mem_type = qzMemFindAddr(dest_ptr);
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;
    if (!need_cont_mem) {
        QZ_DEBUG("Decompress SVM Enabled in doDecompressIn\n");
    }

    g_process.qz_inst[i].stream[j].seq = qz_sess->seq;
    g_process.qz_inst[i].stream[j].res.checksum = 0;

    swapDataBuffer(i, j);
    src_ptr += outputHeaderSz(data_fmt);

    src_send_sz = hdr->extra.qz_e.dest_sz;
    dest_receive_sz = hdr->extra.qz_e.src_sz;
    g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
    g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
        dest_receive_sz;

    QZ_DEBUG("doDecompressIn: Sending %u bytes starting at 0x%lx\n",
             src_send_sz, (unsigned long)src_ptr);
    QZ_DEBUG("sending seq number %d %d %ld\n", i, j, qz_sess->seq);

    if (DEFLATE_GZIP_EXT == data_fmt ||
        DEFLATE_GZIP == data_fmt) {
        qzFooter = (StdGzF_T *)(src_ptr + src_send_sz);
        g_process.qz_inst[i].stream[j].checksum = qzFooter->crc32;
        g_process.qz_inst[i].stream[j].orgdatalen = qzFooter->i_size;
    } else if (LZ4_FH == data_fmt) {
        lz4Footer = (QzLZ4F_T *)(src_ptr + src_send_sz); //TODO
        g_process.qz_inst[i].stream[j].checksum = lz4Footer->cnt_cksum;
        g_process.qz_inst[i].stream[j].orgdatalen = hdr->extra.qz_e.src_sz;
    } else if (DEFLATE_ZLIB == data_fmt) {
        zlibFooter = (StdZlibF_T *)(src_ptr + src_send_sz);
        g_process.qz_inst[i].stream[j].checksum = zlibFooter->adler32;
        g_process.qz_inst[i].stream[j].orgdatalen =
            hdr->extra.qz_e.src_sz; //uncompressed size stored in checkheader
    }

    /*set up src dest buffers*/
    if ((COMMON_MEM == src_mem_type) && need_cont_mem) {
        QZ_DEBUG("memory copy in doDecompressIn\n");
        QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                  src_ptr,
                  src_avail_len,
                  src_send_sz);
        g_process.qz_inst[i].stream[j].src_need_reset = 0;
    } else {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
        g_process.qz_inst[i].stream[j].src_need_reset = 1;
    }

    if ((COMMON_MEM == dest_mem_type) && need_cont_mem) {
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    } else {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = dest_ptr;
        g_process.qz_inst[i].stream[j].dest_need_reset = 1;
    }

    *tmp_src_avail_len = (outputHeaderSz(data_fmt) + src_send_sz + outputFooterSz(
                              data_fmt));
    *tmp_dest_avail_len = dest_receive_sz;
}

void decompInBufferCleanUp(int i, int j)
{
    /*clean stream buffer*/
    g_process.qz_inst[i].stream[j].src1 -= 1;
    g_process.qz_inst[i].stream[j].src2 -= 1;
    RestoreDestCpastreamBuffer(i, j);
    RestoreSrcCpastreamBuffer(i, j);
    swapDataBuffer(i, j);
}

void decompOutSrcBufferCleanUp(int i, int j)
{
    RestoreSrcCpastreamBuffer(i, j);
}

void decompOutErrorDestBufferCleanUp(int i, int j)
{
    RestoreDestCpastreamBuffer(i, j);
}

void decompOutValidDestBufferCleanUp(int i, int j, QzSess_T *qz_sess,
                                     CpaDcRqResults *resl,
                                     unsigned int dest_avail_len)
{
    if (!g_process.qz_inst[i].stream[j].dest_need_reset) {
        QZ_DEBUG("memory copy in doDecompressOut\n");
        QZ_MEMCPY(qz_sess->next_dest,
                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                  dest_avail_len,
                  resl->produced);
    } else {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    }
}


void decompOutProcessedRespond(int i, int j, QzSess_T *qz_sess)
{
    decompOutSrcBufferCleanUp(i, j);
    swapDataBuffer(i, j); /*swap pdata back after decompress*/
    assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
    g_process.qz_inst[i].stream[j].sink2++;
    qz_sess->seq_in++;
    qz_sess->processed++;
}

void decompOutSkipErrorRespond(int i, int j, QzSess_T *qz_sess)
{
    decompOutErrorDestBufferCleanUp(i, j);
    decompOutProcessedRespond(i, j, qz_sess);
}

int decompOutCheckSum(int i, int j, QzSession_T *sess,
                      CpaDcRqResults *resl)
{
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    //alder32_swap to read the checksum in proper order.
    if (qz_sess->sess_params.data_fmt == DEFLATE_ZLIB) {
        if (resl->consumed <
            g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes) {
            // we need to refetch trailer from the end of consumed length which is the stream end
            unsigned char *src_ptr = qz_sess->src + qz_sess->qz_in_len + outputHeaderSz(
                                         qz_sess->sess_params.data_fmt);
            StdZlibF_T *zlibFooter = (StdZlibF_T *)(src_ptr + resl->consumed);
            g_process.qz_inst[i].stream[j].checksum = zlibFooter->adler32;
            if (qz_sess->single_thread &&
                (qz_sess->sess_params.stop_decompression_stream_end == 0)) {
                qz_sess->last_submitted = 0;
            }
            QZ_DEBUG("src_ptr  %p  qz_sess->qz_in_len %lu data checksum %x\n",
                     src_ptr, qz_sess->qz_in_len,
                     g_process.qz_inst[i].stream[j].checksum);
        } else {
            qz_sess->last_submitted = 1;
        }
        g_process.qz_inst[i].stream[j].checksum = ntohl(
                    g_process.qz_inst[i].stream[j].checksum);
        QZ_DEBUG("After resp checksum: %x data checksum %x\n", resl->checksum,
                 g_process.qz_inst[i].stream[j].checksum);
    }
    if ((qz_sess->sess_params.data_fmt != DEFLATE_4B) &&
        (qz_sess->sess_params.data_fmt != DEFLATE_RAW) &&
        unlikely(resl->checksum !=
                 g_process.qz_inst[i].stream[j].checksum ||
                 (resl->produced != g_process.qz_inst[i].stream[j].orgdatalen &&
                  qz_sess->sess_params.data_fmt != DEFLATE_ZLIB))) {
        QZ_ERROR("Error in check footer, inst %d, stream %d\n", i, j);
        QZ_DEBUG("resp checksum: %x data checksum %x data_fmt %d\n",
                 resl->checksum,
                 g_process.qz_inst[i].stream[j].checksum,
                 qz_sess->sess_params.data_fmt);
        QZ_DEBUG("resp produced :%d data produced: %d\n",
                 resl->produced,
                 g_process.qz_inst[i].stream[j].orgdatalen);

        decompOutProcessedRespond(i, j, qz_sess);
        qz_sess->stop_submitting = 1;
        sess->thd_sess_stat = QZ_DATA_ERROR;
        return sess->thd_sess_stat;
    }
    return QZ_OK;
}

void setDeflateEndOfStream(QzSess_T *qz_sess, unsigned char val)
{
    QzDeflateExtCustomData_T  *data = (QzDeflateExtCustomData_T *)
                                      qz_sess->qzdeflateExtData;
    if (data != NULL) {
        data->end_of_stream = val;
        QZ_DEBUG("\tHW setDeflateEndOfStream: setting end_of_stream to %d \n", val);
    }
}

unsigned char getDeflateEndOfStream(QzSess_T *qz_sess)
{
    QzDeflateExtCustomData_T  *data = (QzDeflateExtCustomData_T *)
                                      qz_sess->qzdeflateExtData;
    if (data != NULL) {
        QZ_DEBUG("\tHW getDeflateEndOfStream:  end_of_stream  %d \n",
                 data->end_of_stream);
        return data->end_of_stream;
    }
    return 0;
}

inline void metrixReset(LatencyMetrix_T *m)
{
    if (m == NULL) {
        return;
    }
    if (m->latency_array == NULL) {
        m->latency_array = calloc(LSM_MET_DEPTH, sizeof(*(m->latency_array)));
    }

    for (int i = 0; i < LSM_MET_DEPTH; i++) {
        m->latency_array[i] = 0;
    }
    m->arr_total = 0;
    m->arr_avg = 0;
    m->arr_idx = 0;
#ifdef QATZIP_DEBUG
    m->invoke_counter = 0;
    m->sess_lat_total = 0;
    m->sess_lat_avg = 0;
#endif
}

inline void metrixUpdate(LatencyMetrix_T *m, unsigned long val)
{
    if (m == NULL) {
        return;
    }

    m->arr_total -= m->latency_array[m->arr_idx];
    m->latency_array[m->arr_idx] = val;
    m->arr_total += m->latency_array[m->arr_idx];
    m->arr_avg = m->arr_total >> lsm_met_len_shift;

    if (++m->arr_idx >= LSM_MET_DEPTH) {
        m->arr_idx = 0;
    }

    if (val != 0) {
        m->invoke_counter++;
    }
#ifdef QATZIP_DEBUG
    m->sess_lat_total += val;

    if (m->invoke_counter != 0) {
        m->sess_lat_avg = (m->sess_lat_total) / (m->invoke_counter);
    }

    if (val < m->min_latency) {
        m->min_latency = val;
        m->min_cnt = m->invoke_counter;
    }
    if (val > m->min_latency) {
        m->max_latency = val;
        m->max_cnt = m->invoke_counter;
    }
#endif

    QZ_INFO("The latency for request %lu is %lu, avg is %lu\n", m->arr_idx, val,
            m->arr_avg);
    return;
}

int AsyncCompOutCheckDestLen(int i, int j, QzSession_T *sess,
                             long dest_receive_sz)
{
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    QzAsyncReq_T *req = g_process.qz_inst[i].stream[j].req;
    long remaining = req->qzResults->dest_len - req->req_out_len - dest_receive_sz;
    if (unlikely(remaining < 0)) {
        compOutSkipErrorRespond(i, j, qz_sess);
        // qz_sess->stop_submitting = 1;
        sess->thd_sess_stat = QZ_BUF_ERROR;
        return sess->thd_sess_stat;
    }
    return QZ_OK;
}

void AsyncCompOutValidDestBufferCleanUp(int i, int j,
                                        unsigned int dest_receive_sz)
{
    QzAsyncReq_T *req = g_process.qz_inst[i].stream[j].req;
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;

    if (!need_cont_mem) {
        QZ_DEBUG("Compress SVM Enabled in doCompressOut\n");
    }

    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    } else {
        QZ_MEMCPY(req->dest,
                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                  req->qzResults->dest_len - req->req_out_len,
                  dest_receive_sz);
    }
}

void AsyncDecompOutValidDestBufferCleanUp(int i, int j, QzSess_T *qz_sess,
        CpaDcRqResults *resl,
        QzAsyncReq_T *req)
{
    if (!g_process.qz_inst[i].stream[j].dest_need_reset) {
        QZ_DEBUG("memory copy in doDecompressOut\n");
        QZ_MEMCPY(req->dest,
                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                  req->qzResults->dest_len,
                  resl->produced);
    } else {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    }
}

static int QzRingMoveProdHead(QzRing_T *ring, uint32_t *old_head,
                              uint32_t *new_head, int is_single_producer)
{
    const uint32_t capacity = ring->capacity;
    int success = 0;
    uint32_t free_entries = 0;

    do {
        *old_head = ring->prod.head;
        *new_head = *old_head + 1;

        free_entries = (capacity + ring->cons.tail - *old_head);
        if (1 > free_entries)
            return QZ_FAIL;

        if (is_single_producer) {
            ring->prod.head = *new_head;
            success = 1;
        } else {
            success = __sync_bool_compare_and_swap((uint32_t *)(uintptr_t)&ring->prod.head,
                                                   *old_head, *new_head);
        }
    } while (success == 0);
    return QZ_OK;
}

static void QzRingEnqueueElem(QzRing_T *ring, uint32_t prod_head, void *obj)
{
    prod_head = prod_head % ring->capacity;
    ring->elems[prod_head] = obj;
}

static void QzRingUpdatTail(QzRingHeadTail_T *ht, uint32_t old_val,
                            uint32_t new_val, uint32_t single)
{
    if (!single)
        __sync_bool_compare_and_swap((volatile uint32_t *)(uintptr_t)&ht->tail, old_val,
                                     new_val);
    else
        ht->tail = new_val;
}

static int QzRingMoveConsHead(QzRing_T *ring,
                              uint32_t *old_head, uint32_t *new_head, int is_single_consumer)
{
    int success;
    int entries = 0;

    do {
        *old_head = ring->cons.head;

        entries = (ring->prod.tail - *old_head);

        if (1 > entries)
            return QZ_FAIL;

        *new_head = *old_head + 1;
        if (is_single_consumer) {
            ring->cons.head = *new_head;
            success = 1;
        } else {
            success = __sync_bool_compare_and_swap((uint32_t *)(uintptr_t)&ring->cons.head,
                                                   *old_head, *new_head);
        }
    } while (success == 0);
    return QZ_OK;
}

static void *QzRingDequeueElem(QzRing_T *ring, uint32_t cons_head)
{
    void *obj;
    cons_head = cons_head % ring->capacity;
    obj = ring->elems[cons_head];
    ring->elems[cons_head] = NULL;
    return obj;
}

QzRing_T *QzRingCreate(int size)
{
    if (size < 1) {
        QZ_ERROR("Create ring size is incorrect\n");
        return NULL;
    }
    QzRing_T *ring;
    ring = (QzRing_T *)calloc(1, sizeof(QzRing_T));
    ring->elems = (void *)calloc(size, sizeof(void *));
    ring->size = size;
    ring->mask = size - 1;
    ring->capacity = size;
    return ring;
}

void QzClearRing(QzRing_T *ring)
{
    for (int i = 0; i < ring->size; i++) {
        if (NULL != ring->elems[i]) {
            free(ring->elems[i]);
            ring->elems[i] = NULL;
        }
    }
    ring->mask = ring->size - 1;
    ring->capacity = ring->size;
    ring->cons.head = 0;
    ring->cons.tail = 0;
    ring->prod.head = 0;
    ring->prod.tail = 0;
}

void QzRingFree(QzRing_T *ring)
{
    if (ring != NULL) {
        if (ring->elems != NULL) {
            free(ring->elems);
        }
        free(ring);
    }
}

int QzRingProduceEnQueue(QzRing_T *ring, void *obj, int is_single_producer)
{
    uint32_t prod_head, prod_next;
    int rc = QZ_OK;

    rc = QzRingMoveProdHead(ring, &prod_head, &prod_next, is_single_producer);
    if (QZ_FAIL == rc)
        return rc;

    QzRingEnqueueElem(ring, prod_head, obj);

    QzRingUpdatTail(&ring->prod, prod_head, prod_next, is_single_producer);
    return rc;
}

void *QzRingConsumeDequeue(QzRing_T *ring, int is_single_consumer)
{
    uint32_t cons_head = 0;
    uint32_t cons_next = 0;
    // uint32_t entries;
    int rc = 0;
    void *obj = NULL;

    rc = QzRingMoveConsHead(ring, &cons_head, &cons_next, is_single_consumer);
    if (QZ_FAIL == rc)
        return NULL;

    obj = QzRingDequeueElem(ring, cons_head);

    QzRingUpdatTail(&ring->cons, cons_head, cons_next, is_single_consumer);

    return obj;
}
