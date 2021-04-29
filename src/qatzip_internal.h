/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2021 Intel Corporation. All rights reserved.
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

#ifndef _QATZIPP_H
#define _QATZIPP_H

#ifdef __cplusplus
extern"C" {
#endif

#include <zlib.h>

/**
 *  define lib version
 */
#define QATZIP_VERSION "1.0.4"

#define SUCCESS              1
#define FAILURE              0

#define NODE_0               0
#define NUM_BUFF             (32)
#define MAX_NUM_RETRY        ((int)500)
#define MAX_BUFFERS          ((int)100)
#define MAX_THREAD_TMR       ((int)100)

#define GET_LOWER_32BITS(v)  ((v) & 0xFFFFFFFF)
#define GET_LOWER_16BITS(v)  ((v) & 0xFFFF)
#define GET_LOWER_8BITS(v)   ((v) & 0xFF)

/*For Sync mode, request counts must less than NUM_BUFF.
 *If then the request can't get adequate unused buffer and will be hanged
 * */
#if QZ_REQ_THRESHOLD_MAXIMUM > NUM_BUFF
#error QZ_REQ_THRESHOLD_MAXIMUM should not be larger than NUM_BUFF
#endif

#define QAT_MAX_DEVICES     32
#define STORED_BLK_MAX_LEN  65535
#define STORED_BLK_HDR_SZ   5

#define QZ_SETUP_SESSION_FAIL(rc) (QZ_FAIL == rc       || \
                                   QZ_PARAMS == rc     || \
                                   QZ_NOSW_NO_HW == rc || \
                                   QZ_NOSW_LOW_MEM == rc)

#define likely(x)   __builtin_expect (!!(x), 1)
#define unlikely(x) __builtin_expect (!!(x), 0)
#define DEST_SZ(src_sz)           (((9 * (src_sz)) / 8) + 1024)

typedef struct QzCpaStream_S {
    signed long seq;
    signed long src1;
    signed long src2;
    signed long sink1;
    signed long sink2;
    CpaDcRqResults  res;
    CpaStatus job_status;
    unsigned char *orig_src;
    unsigned char *orig_dest;
    int src_pinned;
    int dest_pinned;
    unsigned int gzip_footer_checksum;
    unsigned int gzip_footer_orgdatalen;
} QzCpaStream_T;

typedef struct QzInstance_S {
    CpaInstanceInfo2 instance_info;
    CpaDcInstanceCapabilities instance_cap;
    CpaBufferList **intermediate_buffers;
    Cpa32U buff_meta_size;

    /* Tracks memory where the intermediate buffers reside. */
    Cpa16U intermediate_cnt;
    CpaBufferList **src_buffers;
    CpaBufferList **dest_buffers;
    Cpa16U src_count;
    Cpa16U dest_count;
    QzCpaStream_T *stream;

    unsigned int lock;
    time_t heartbeat;
    unsigned char mem_setup;
    unsigned char cpa_sess_setup;
    CpaStatus inst_start_status;
    unsigned int num_retries;
    CpaDcSessionHandle cpaSess;
} QzInstance_T;

typedef struct QzInstanceList_S {
    QzInstance_T instance;
    CpaInstanceHandle dc_inst_handle;
    struct QzInstanceList_S *next;
} QzInstanceList_T;

typedef struct QzHardware_S {
    QzInstanceList_T devices[QAT_MAX_DEVICES];
    unsigned int dev_num;
    unsigned int max_dev_id;
} QzHardware_T;

typedef struct ProccesData_S {
    char qz_init_status;
    unsigned char sw_backup;
    CpaInstanceHandle *dc_inst_handle;
    QzInstance_T *qz_inst;
    Cpa16U num_instances;
    char qat_available;
} processData_T;

typedef enum {
    InflateError = -1,
    InflateNull = 0,
    InflateInited,
    InflateOK,
    InflateEnd
} InflateState_T;

typedef enum {
    DeflateNull = 0,
    DeflateInited
} DeflateState_T;

typedef struct QzSess_S {
    int inst_hint;   /*which instance we last used*/
    QzSessionParams_T sess_params;
    CpaDcSessionSetupData session_setup_data;
    Cpa32U session_size;
    Cpa32U ctx_size;
    CpaStatus sess_status;
    int submitted;
    int processed;
    int last_submitted;
    int last_processed;
    int stop_submitting;
    signed long seq;
    signed long seq_in;
    pthread_t c_th_i;
    pthread_t c_th_o;

    unsigned char *src;
    unsigned int *src_sz;

    unsigned int *dest_sz;
    unsigned char *next_dest;

    int force_sw;
    InflateState_T inflate_stat;
    void *strm;
    z_stream *inflate_strm;
    unsigned long qz_in_len;
    unsigned long qz_out_len;
    unsigned long *crc32;
    unsigned int last;
    unsigned int single_thread;
    unsigned int polling_idx;

    z_stream *deflate_strm;
    DeflateState_T deflate_stat;
} QzSess_T;

typedef struct QzStreamBuf_S {
    unsigned int buf_len;
    unsigned char *in_buf;
    unsigned char *out_buf;
    unsigned int out_offset;
} QzStreamBuf_T;

typedef struct ThreadData_S {
    pid_t ppid;
    pid_t pid;
    struct timeval timer[MAX_THREAD_TMR];
} ThreadData_T;

typedef struct QzExtraHeader_S {
    uint32_t src_sz;
    uint32_t dest_sz;
} QzExtraHeader_T;

typedef struct QzExtraField_S {
    unsigned char st1;
    unsigned char st2;
    uint16_t x2_len;
    QzExtraHeader_T qz_e;
} QzExtraField_T;

typedef struct StdGzH_S {
    unsigned char id1;
    unsigned char id2;
    unsigned char cm;
    unsigned char flag;
    unsigned char mtime[4];
    unsigned char xfl;
    unsigned char os;
} StdGzH_T;

typedef struct QzGzH_S {
    StdGzH_T std_hdr;
    uint16_t x_len;
    QzExtraField_T extra;
} QzGzH_T;

typedef struct StdGzF_S {
    uint32_t crc32;
    uint32_t i_size;
} StdGzF_T;

typedef struct QzMem_S {
    int flag;
    unsigned char *addr;
    int sz;
    int numa;
} QzMem_T;

void dumpAllCounters(void);
int qzSetupHW(QzSession_T *sess, int i);
unsigned long qzGzipHeaderSz(void);
unsigned long stdGzipHeaderSz(void);
unsigned long stdGzipFooterSz(void);
unsigned long outputHeaderSz(QzDataFormat_T data_fmt);
unsigned long outputFooterSz(QzDataFormat_T data_fmt);
void qzGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res);
void stdGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res);
int qzGzipHeaderExt(const unsigned char *const ptr, QzGzH_T *hdr);
void outputHeaderGen(unsigned char *ptr,
                     CpaDcRqResults *res,
                     QzDataFormat_T data_fmt);
void qzGzipFooterGen(unsigned char *ptr, CpaDcRqResults *res);
void outputFooterGen(QzSess_T *qz_sess,
                     CpaDcRqResults *res,
                     QzDataFormat_T data_fmt);
void qzGzipFooterExt(const unsigned char *const ptr, StdGzF_T *ftr);

int isQATProcessable(const unsigned char *ptr,
                     const unsigned int *const src_len,
                     QzSess_T *const qz_sess);

int qzSWCompress(QzSession_T *sess, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len, unsigned int last);

int qzSWDecompress(QzSession_T *sess, const unsigned char *src,
                   unsigned int *uncompressed_buf_len, unsigned char *dest,
                   unsigned int *compressed_buffer_len);

int qzSWDecompressMultiGzip(QzSession_T *sess, const unsigned char *src,
                            unsigned int *uncompressed_buf_len, unsigned char *dest,
                            unsigned int *compressed_buffer_len);

int qz_sessParamsCheck(QzSessionParams_T *params);

unsigned char getSwBackup(QzSession_T *sess);

#ifdef ADF_PCI_API
extern CpaStatus icp_adf_get_numDevices(Cpa32U *);
#endif

int initStream(QzSession_T *sess, QzStream_T *strm);

void *qzMemSet(void *ptr, unsigned char filler, unsigned int count);

unsigned char *findStdGzipFooter(const unsigned char *src_ptr,
                                 long src_avail_len);

void streamBufferCleanup();
#endif //_QATZIPP_H
