/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2022 Intel Corporation. All rights reserved.
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
#ifdef HAVE_QAT_HEADERS
#include <qat/cpa_dev.h>
#else
#include <cpa_dev.h>
#endif
#include <stdbool.h>
#include <stdatomic.h>
#include <zlib.h>
#include <lz4frame.h>

/**
 *  define release version
 */
#define QATZIP_VERSION "1.1.0"

#define SUCCESS              1
#define FAILURE              0

#define NODE_0               0
#define NUM_BUFF             (32)
/**
 * For Gen4, 8K hardware buffer size need more inflight buffers
 * to reach peak performance
 */
#define NUM_BUFF_8K_GEN4     (128)
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

#define QAT_MAX_DEVICES     (32 * 32)
#define STORED_BLK_MAX_LEN  65535
#define STORED_BLK_HDR_SZ   5

#define QZ_INIT_FAIL(rc)          (QZ_OK != rc     &&  \
                                   QZ_DUPLICATE != rc)

#define QZ_SETUP_SESSION_FAIL(rc) (QZ_PARAMS == rc     || \
                                   QZ_NOSW_NO_HW == rc || \
                                   QZ_NOSW_LOW_MEM == rc)

#define likely(x)   __builtin_expect (!!(x), 1)
#define unlikely(x) __builtin_expect (!!(x), 0)
#define DEST_SZ(src_sz)           (((9U * (src_sz)) / 8U) + 1024U)

/* The minimal supported CAP_DC API version */
#ifndef CPA_DC_API_VERSION_AT_LEAST
#define CPA_DC_API_VERSION_AT_LEAST(major, minor)   \
        (CPA_DC_API_VERSION_NUM_MAJOR > major ||    \
        (CPA_DC_API_VERSION_NUM_MAJOR == major &&   \
        CPA_DC_API_VERSION_NUM_MINOR >= minor))
#endif

#define QZ_4XXX_PCI_DEVICE_A_ID 0x4940
#define QZ_4XXXIOV_PCI_DEVICE_A_ID 0x4941
#define QZ_4XXX_PCI_DEVICE_B_ID 0x4942
#define QZ_4XXXIOV_PCI_DEVICE_B_ID 0x4943
#define QZ_4XXX_PCI_DEVICE_C_ID 0x4944
#define QZ_4XXXIOV_PCI_DEVICE_C_ID 0x4945
#define QZ_4XXX_PCI_DEVICE_D_ID 0x4946
#define QZ_4XXXIOV_PCI_DEVICE_D_ID 0x4947
#define QZ_4XXX_PCI_DEVICE_E_ID 0x4948
#define QZ_4XXXIOV_PCI_DEVICE_E_ID 0x4949

#define IS_QAT_GEN4(device_id)                       \
    (device_id == QZ_4XXX_PCI_DEVICE_A_ID ||         \
     device_id == QZ_4XXXIOV_PCI_DEVICE_A_ID ||      \
     device_id == QZ_4XXX_PCI_DEVICE_B_ID ||         \
     device_id == QZ_4XXXIOV_PCI_DEVICE_B_ID ||      \
     device_id == QZ_4XXX_PCI_DEVICE_C_ID ||         \
     device_id == QZ_4XXXIOV_PCI_DEVICE_C_ID ||      \
     device_id == QZ_4XXX_PCI_DEVICE_D_ID ||         \
     device_id == QZ_4XXXIOV_PCI_DEVICE_D_ID ||      \
     device_id == QZ_4XXX_PCI_DEVICE_E_ID ||         \
     device_id == QZ_4XXXIOV_PCI_DEVICE_E_ID)

#define QZ_CEIL_DIV(x, y) (((x) + (y)-1U) / (y))
#define QZ_DEFLATE_SKID_PAD_GEN4_STATIC (1029)
#define QZ_DEFLATE_SKID_PAD_GEN4_DYN (512)
#define QZ_LZ4_SKID_PAD_GEN4 (1024)
#define QZ_LZ4S_SKID_PAD_GEN4 (1024)

/* macros for lz4 */
#define QZ_LZ4_MAGIC         0x184D2204U
#define QZ_LZ4_MAGIC_SKIPPABLE 0x184D2A50U
#define QZ_LZ4_VERSION       0x1
#define QZ_LZ4_BLK_INDEP     0x0
#define QZ_LZ4_BLK_CKS_FLAG  0x0
#define QZ_LZ4_DICT_ID_FLAG  0x0
#define QZ_LZ4_CNT_SIZE_FLAG 0x1
#define QZ_LZ4_CNT_CKS_FLAG  0x1
#define QZ_LZ4_ENDMARK       0x0
#define QZ_LZ4_MAX_BLK_SIZE  0x4

#define QZ_LZ4_MAGIC_SIZE      4                     //lz4 magic number length
#define QZ_LZ4_FD_SIZE         11                    //lz4 frame descriptor length
#define QZ_LZ4_HEADER_SIZE     (QZ_LZ4_MAGIC_SIZE + \
                                QZ_LZ4_FD_SIZE)      //lz4 frame header length
#define QZ_LZ4_CHECKSUM_SIZE   4                     //lz4 checksum length
#define QZ_LZ4_ENDMARK_SIZE    4                     //lz4 endmark length
#define QZ_LZ4_FOOTER_SIZE     (QZ_LZ4_CHECKSUM_SIZE + \
                                QZ_LZ4_ENDMARK_SIZE) //lz4 frame footer length
#define QZ_LZ4_BLK_HEADER_SIZE 4                     //lz4 block header length
#define QZ_LZ4_STOREDBLOCK_FLAG 0x80000000U
#define QZ_LZ4_STORED_HEADER_SIZE 4

#define DATA_FORMAT_DEFAULT     DEFLATE_GZIP_EXT

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
    int src_need_reset;
    int dest_need_reset;
    unsigned int checksum;
    unsigned int orgdatalen;
    CpaDcOpData opData;
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
    CpaDcSessionSetupData session_setup_data;
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
    atomic_char qat_available;
    CpaDeviceInfo device_info;
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

// Include all support Dataforamt
typedef enum DataFormatInternal_E {
    DEFLATE_4B = 0,
    /**< Data is in raw deflate format with 4 byte header */
    DEFLATE_GZIP,
    /**< Data is in deflate wrapped by GZip header and footer */
    DEFLATE_GZIP_EXT,
    /**< Data is in deflate wrapped by GZip extended header and footer */
    DEFLATE_RAW,
    /**< Data is in raw deflate format */
    LZ4_FH,
    /**< Data is in LZ4 format with frame headers */
    LZ4S_BK,
    /**< Data is in LZ4s sequences with block header, it's only for post processed */
} DataFormatInternal_T;

// Include all support session parameters
typedef struct QzSessionParamsInternal_S {
    QzHuffmanHdr_T huffman_hdr;
    /**< Dynamic or Static Huffman headers */
    QzDirection_T direction;
    /**< Compress or decompress */
    DataFormatInternal_T data_fmt;
    /**< Deflate, deflate with GZip or deflate with GZip ext */
    unsigned int comp_lvl;
    /**< Compression level 1 to 12 for QAT CPM2.0. */
    /**< If the comp_algorithm is deflate, values > max will be set to max */
    unsigned char comp_algorithm;
    /**< Compress/decompression algorithms */
    unsigned int max_forks;
    /**< Maximum forks permitted in the current thread */
    /**< 0 means no forking permitted */
    unsigned char sw_backup;
    /**< bit field defining SW configuration (see QZ_SW_* definitions) */
    unsigned int hw_buff_sz;
    /**< Default buffer size, must be a power of 2k */
    /**< 4K,8K,16K,32K,64K,128K */
    unsigned int strm_buff_sz;
    /**< Stream buffer size between [1K .. 2M - 5K] */
    /**< Default strm_buf_sz equals to hw_buff_sz */
    unsigned int input_sz_thrshold;
    /**< Default threshold of compression service's input size */
    /**< for sw failover, if the size of input request is less */
    /**< than the threshold, QATzip will route the request */
    /**< to software */
    unsigned int req_cnt_thrshold;
    /**< Set between 1 and NUM_BUFF, default NUM_BUFF */
    /**< NUM_BUFF is defined in qatzip_internal.h */
    unsigned int wait_cnt_thrshold;
    /**< When previous try failed, wait for specific number of calls */
    /**< before retrying to open device. Default threshold is 8 */
    qzLZ4SCallbackFn qzCallback;
    /**< post processing callback for zstd compression*/
    void *qzCallback_external;
    /**< An opaque pointer provided by the user to be passed */
    /**< into qzCallback during post processing*/
    QzPollingMode_T polling_mode;
    /**< 0 means no busy polling, 1 means busy polling */
    unsigned int is_sensitive_mode;
    /**< 0 means disable sensitive mode, 1 means enable sensitive mode*/
    unsigned int lz4s_mini_match;
    /**< Set lz4s dictionary mini match, which would be 3 or 4 */
} QzSessionParamsInternal_T;

typedef struct QzSess_S {
    int inst_hint;   /*which instance we last used*/
    QzSessionParamsInternal_T sess_params;
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
    LZ4F_cctx *cctx;
    LZ4F_dctx *dctx;
} QzSess_T;

typedef struct QzStreamBuf_S {
    unsigned int buf_len;
    unsigned char *in_buf;
    unsigned char *out_buf;
    unsigned int out_offset;
    unsigned int in_offset;
    unsigned int flush_more;
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

typedef struct Qz4BH_S {
    uint32_t blk_size;
} Qz4BH_T;

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

#pragma pack(push, 1)
/* lz4 frame header */
typedef struct QzLZ4H_S {
    uint32_t magic; /* LZ4 magic number */
    uint8_t flag_desc;
    uint8_t block_desc;
    uint64_t cnt_size;
    uint8_t hdr_cksum; /* header checksum */
} QzLZ4H_T;

/* lz4 frame footer */
typedef struct QzLZ4F_S {
    uint32_t end_mark;  /* LZ4 end mark */
    uint32_t cnt_cksum; /* content checksum */
} QzLZ4F_T;
#pragma pack(pop)


void dumpAllCounters(void);
int qzSetupHW(QzSession_T *sess, int i);
unsigned long qzGzipHeaderSz(void);
unsigned long qz4BHeaderSz(void);
unsigned long stdGzipHeaderSz(void);
unsigned long stdGzipFooterSz(void);
unsigned long outputHeaderSz(DataFormatInternal_T data_fmt);
unsigned long outputFooterSz(DataFormatInternal_T data_fmt);
void qzGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res);
void qz4BHeaderGen(unsigned char *ptr, CpaDcRqResults *res);
void stdGzipHeaderGen(unsigned char *ptr, CpaDcRqResults *res);
int qzGzipHeaderExt(const unsigned char *const ptr, QzGzH_T *hdr);
void outputHeaderGen(unsigned char *ptr,
                     CpaDcRqResults *res,
                     DataFormatInternal_T data_fmt);
void qzGzipFooterGen(unsigned char *ptr, CpaDcRqResults *res);
void outputFooterGen(QzSess_T *qz_sess,
                     CpaDcRqResults *res,
                     DataFormatInternal_T data_fmt);
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

int qzSWDecompressMulti(QzSession_T *sess, const unsigned char *src,
                        unsigned int *uncompressed_buf_len, unsigned char *dest,
                        unsigned int *compressed_buffer_len);

unsigned char getSwBackup(QzSession_T *sess);

#ifdef ADF_PCI_API
extern CpaStatus icp_adf_get_numDevices(Cpa32U *);
#endif

int initStream(QzSession_T *sess, QzStream_T *strm);

void *qzMemSet(void *ptr, unsigned char filler, unsigned int count);

unsigned char *findStdGzipFooter(const unsigned char *src_ptr,
                                 long src_avail_len);

void removeSession(int i);

void cleanUpInstMem(int i);

void qzMemDestory(void);

void streamBufferCleanup();

//lz4 functions
unsigned long qzLZ4HeaderSz(void);
unsigned long qzLZ4FooterSz(void);
void qzLZ4HeaderGen(unsigned char *ptr, CpaDcRqResults *res);
void qzLZ4FooterGen(unsigned char *ptr, CpaDcRqResults *res);
unsigned char *findLZ4Footer(const unsigned char *src_ptr,
                             long src_avail_len);
int qzVerifyLZ4FrameHeader(const unsigned char *const ptr, uint32_t len);
int isQATLZ4Processable(const unsigned char *ptr,
                        const unsigned int *const src_len,
                        QzSess_T *const qz_sess);
int isQATDeflateProcessable(const unsigned char *ptr,
                            const unsigned int *const src_len,
                            QzSess_T *const qz_sess);

unsigned long qzLZ4SBlockHeaderSz(void);
void qzLZ4SBlockHeaderGen(unsigned char *ptr, CpaDcRqResults *res);

int qzSetupSessionInternal(QzSession_T *sess);

int qzCheckParams(QzSessionParams_T *params);
int qzCheckParamsDeflate(QzSessionParamsDeflate_T *params);
int qzCheckParamsLZ4(QzSessionParamsLZ4_T *params);
int qzCheckParamsLZ4S(QzSessionParamsLZ4S_T *params);

void qzGetParams(QzSessionParamsInternal_T *internal_params,
                 QzSessionParams_T *params);
void qzGetParamsDeflate(QzSessionParamsInternal_T *internal_params,
                        QzSessionParamsDeflate_T *params);
void qzGetParamsLZ4(QzSessionParamsInternal_T *internal_params,
                    QzSessionParamsLZ4_T *params);
void qzGetParamsLZ4S(QzSessionParamsInternal_T *internal_params,
                     QzSessionParamsLZ4S_T *params);

void qzSetParamsLZ4S(QzSessionParamsLZ4S_T *params,
                     QzSessionParamsInternal_T *internal_params);
void qzSetParamsLZ4(QzSessionParamsLZ4_T *params,
                    QzSessionParamsInternal_T *internal_params);
void qzSetParamsDeflate(QzSessionParamsDeflate_T *params,
                        QzSessionParamsInternal_T *internal_params);
void qzSetParams(QzSessionParams_T *params,
                 QzSessionParamsInternal_T *internal_params);
#endif //_QATZIPP_H
