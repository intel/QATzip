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

/**
 *****************************************************************************
 * @file qatzip.h
 *
 * @defgroup qatZip Data Compression API
 *
 * @description
 *      These functions specify the API for data compression operations.
 *
 * @remarks
 *
 *
 *****************************************************************************/

#ifndef _QATZIP_H
#define _QATZIP_H

#ifdef __cplusplus
extern"C" {
#endif

#include <string.h>
#include <stdint.h>

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Major Version Number
 * @description
 *      The QATzip API major version number. This number will be incremented
 *    when significant changes to the API have occurred.
 *    The combination of the major and minor number definitions represent
 *    the complete version number for this interface.
 *
 *****************************************************************************/
#define QATZIP_API_VERSION_NUM_MAJOR (2)

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Minor Version Number
 * @description
 *      The QATzip API minor version number. This number will be incremented
 *    when minor changes to the API have occurred. The combination of the major
 *    and minor number definitions represent the complete version number for
 *    this interface.
 *****************************************************************************/
#define QATZIP_API_VERSION_NUM_MINOR (3)

/* Define a macro as an integer to test */
#define QATZIP_API_VERSION    (QATZIP_API_VERSION_NUM_MAJOR * 10000 +      \
                               QATZIP_API_VERSION_NUM_MINOR * 100)

/**
 *  These macros define how the project will be built
 *  QATZIP_LINK_DLL must be defined if linking the DLL
 *  QATZIP_BUILD_DLL must be defined when building a DLL
 *  No definition required if building the project as static library
 */
#if defined QATZIP_LINK_DLL
#define QATZIP_API __declspec(dllimport)
#elif defined QATZIP_BUILD_DLL
#define QATZIP_API __declspec(dllexport)
#else
#define QATZIP_API
#endif

/**
 *****************************************************************************
 *
 *  This API provides access to underlying compression functions in QAT
 *  hardware. The API supports an implementation that provides compression
 *  service in software if all of the required resources are not available
 *  to execute the compression service in hardware.
 *
 *  The API supports threaded applications.
 *  Applications can create threads and each of these threads can invoke the
 *  API defined herein.
 *
 *  For simplicity, initializations and setup function calls are not
 *  required to obtain compression services. If the initialization and setup
 *  functions are not called before compression or decompression requests, then
 *  they will be called with default arguments from within the compression or
 *  decompression functions. This results in several legal calling scenarios,
 *  described below.
 *
 *  Scenario 1 - All functions explicitly invoked by caller, with all arguments provided.
 *
 *  qzInit(&sess, sw_backup);
 *  qzSetupSession(&sess, &params);
 *  qzCompress(&sess, src, &src_len, dest, &dest_len, 1);
 *  qzDecompress(&sess, src, &src_len, dest, &dest_len);
 *  qzTeardownSession(&sess);
 *  qzClose(&sess);
 *
 *
 * Scenario 2 - Initialization function called, setup function not invoked by caller.
 *              This scenario can be used to specify the sw_backup argument to
 *              qzInit.
 *
 *  qzInit(&sess, sw_backup);
 *  qzCompress(&sess, src, &src_len, dest, &dest_len, 1);
 *      calls qzSetupSession(sess, NULL);
 *  qzTeardownSession(&sess);
 *  qzClose(&sess);
 *
 *
 * Scenario 3 - Calling application simply invokes the actual qzCompress functions.
 *
 *  qzCompress(&sess, src, &src_len, dest, &dest_len, 0);
 *      calls qzInit(sess, 1);
 *      calls qzSetupSession(sess, NULL);
 *  qzCompress(&sess, src, &src_len, dest, &dest_len, 1);
 *
 *  Notes: Invoking qzSetupSession with NULL for params sets up a session with
 *  default session attributed, detailed in the function description below.
 *
 *  If an application terminates without invoking tear down and close
 *  functions, process termination will invoke memory and hardware instance
 *  cleanup.
 *
 *  If a thread terminates without invoking tear down and close functions,
 *  memory and hardware are not cleaned up until the application exits.
 *
 *  Additions for QAT 2.0 and beyond platforms though Extending
 *  QzSessionParamsGen3_T, QzDataFormatGen3_T and Using qzSetupSessionGen3
 *  to setup session.
 *  1. Addition of LZ4 and LZ4s
 *  2. Addition of post processing functions for out of LZ4s
 *  3. Compression level up to 12 for LZ4 and LZ4s
 *  4. Support for gzip header with additional compression algorithms
 *
 *****************************************************************************/
/**
 *****************************************************************************
 * @ingroup qatZip
 *      Supported Huffman Headers
 *
 * @description
 *      This enumerated list identifies the Huffman header types
 *    supported by QATzip.
 *
 *****************************************************************************/
typedef enum QzHuffmanHdr_E {
    QZ_DYNAMIC_HDR = 0,
    /**< Full Dynamic Huffman Trees */
    QZ_STATIC_HDR
    /**< Static Huffman Trees */
} QzHuffmanHdr_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Supported memory types
 *
 * @description
 *      This enumerated list identifies memory types supported
 *    by QATzip.
 *
 *****************************************************************************/
typedef enum PinMem_E {
    COMMON_MEM = 0,
    /**< Allocate non-contiguous memory */
    PINNED_MEM
    /**< Allocate contiguous memory */
} PinMem_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Compress or decompress setting
 *
 * @description
 *      This enumerated list identifies the session directions
 *    supported by QATzip. A session can be compress, decompress
 *    or both.
 *
 *****************************************************************************/
typedef enum QzDirection_E {
    QZ_DIR_COMPRESS = 0,
    /**< Session will be used for compression */
    QZ_DIR_DECOMPRESS,
    /**< Session will be used for decompression */
    QZ_DIR_BOTH
    /**< Session will be used for both compression and decompression */
} QzDirection_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Streaming API input and output format
 *
 * @description
 *      This enumerated list identifies the data format supported by
 *    QATzip streaming API. A format can be raw deflate data block, deflate
 *    block wrapped by GZip header and footer, or deflate data block wrapped
 *    by GZip extension header and footer.
 *
 *****************************************************************************/
typedef enum QzDataFormat_E {
    QZ_DEFLATE_4B = 0,
    /**< Data is in raw deflate format with 4 byte header */
    QZ_DEFLATE_GZIP,
    /**< Data is in deflate wrapped by GZip header and footer */
    QZ_DEFLATE_GZIP_EXT,
    /**< Data is in deflate wrapped by GZip extended header and footer */
    QZ_DEFLATE_RAW,
    /**< Data is in raw deflate format */
    QZ_FMT_NUM
} QzDataFormat_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Supported polling mode
 *
 * @description
 *      Specifies whether the instance must be busy polling,
 *    or be periodical polling.
 *
 *****************************************************************************/
typedef enum QzPollingMode_E {
    QZ_PERIODICAL_POLLING = 0,
    /**< No busy polling */
    QZ_BUSY_POLLING,
    /**< busy polling */
} QzPollingMode_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Supported checksum type
 *
 * @description
 *      This enumerated list identifies the checksum type for input/output
 *    data. The format can be CRC32, Adler or none.
 *
 *****************************************************************************/
typedef enum QzCrcType_E {
    QZ_CRC32 = 0,
    /**< CRC32 checksum */
    QZ_ADLER,
    /**< Adler checksum */
    NONE
    /**< No checksum */
} QzCrcType_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Software Component type
 *
 * @description
 *      This enumerated list specifies the type of software that is being
 *    described.
 *
 *****************************************************************************/
typedef enum QzSoftwareComponentType_E {
    QZ_COMPONENT_FIRMWARE = 0,
    QZ_COMPONENT_KERNEL_DRIVER,
    QZ_COMPONENT_USER_DRIVER,
    QZ_COMPONENT_QATZIP_API,
    QZ_COMPONENT_SOFTWARE_PROVIDER
} QzSoftwareComponentType_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Session Status definitions and function return codes
 *
 * @description
 *      This list identifies valid values for session status and function
 *    return codes.
 *
 *****************************************************************************/
#define QZ_OK                   (0)
/**< Success */
#define QZ_DUPLICATE            (1)
/**< Can not process function again. No failure */
#define QZ_FORCE_SW             (2)
/**< Using SW: Switch to software because of previous block */
#define QZ_PARAMS              (-1)
/**< Invalid parameter in function call */
#define QZ_FAIL                (-2)
/**< Unspecified error */
#define QZ_BUF_ERROR            (-3)
/**< Insufficient buffer error */
#define QZ_DATA_ERROR           (-4)
/**< Input data was corrupted */
#define QZ_TIMEOUT              (-5)
/**< Operation timed out */
#define QZ_INTEG                (-100)
/**< Integrity checked failed */
#define QZ_NO_HW                (11)
/**< Using SW: No QAT HW detected */
#define QZ_NO_MDRV              (12)
/**< Using SW: No memory driver detected */
#define QZ_NO_INST_ATTACH       (13)
/**< Using SW: Could not attach to an instance */
#define QZ_LOW_MEM              (14)
/**< Using SW: Not enough pinned memory */
#define QZ_LOW_DEST_MEM         (15)
/**< Using SW: Not enough pinned memory for dest buffer */
#define QZ_UNSUPPORTED_FMT      (16)
/**< Using SW: QAT device does not support data format */
#define QZ_NONE                 (100)
/**< Device uninitialized */
#define QZ_NOSW_NO_HW           (-101)
/**< Not using SW: No QAT HW detected */
#define QZ_NOSW_NO_MDRV         (-102)
/**< Not using SW: No memory driver detected */
#define QZ_NOSW_NO_INST_ATTACH  (-103)
/**< Not using SW: Could not attach to instance */
#define QZ_NOSW_LOW_MEM         (-104)
/**< Not using SW: not enough pinned memory */
#define QZ_NO_SW_AVAIL          (-105)
/**<Session may require software, but no software is available */
#define QZ_NOSW_UNSUPPORTED_FMT (-116)
/**< Not using SW: QAT device does not support data format */
#define QZ_POST_PROCESS_ERROR   (-117)
/**< Using post process: post process callback encountered an error */
#define QZ_METADATA_OVERFLOW    (-118)
/**< Insufficent memory allocated for metadata */
#define QZ_OUT_OF_RANGE         (-119)
/**< Metadata block_num specified is out of range */
#define QZ_NOT_SUPPORTED        (-200)
/**< Request not supported */

#define QZ_MAX_ALGORITHMS  ((int)255)
#define QZ_DEFLATE         ((unsigned char)8)
/**< used in gzip header to indicate deflate blocks */
/**< and in session params */
#define QZ_LZ4             ((unsigned char)'4')
#define QZ_LZ4s            ((unsigned char)'s')
#define QZ_ZSTD            ((unsigned char)'Z')

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifdef __linux__
#define QZ_MEMCPY(dest, src, dest_sz, src_sz) \
        memcpy((void *)(dest), (void *) (src), (size_t)MIN(dest_sz, src_sz))
#endif
#ifdef _WIN64
#define QZ_MEMCPY(dest, src, dest_sz, src_sz) \
        memcpy_s((void *)(dest), dest_sz, (void *) (src), MIN(dest_sz, src_sz))
#endif

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Post processing callback after LZ4s compression
 *
 * @description
 *        This function will be called in qzCompressCrc for post processing
 *      of lz4s payloads. Function implementation should be provided by user
 *      and comply with this prototype's rules. The input paramter 'dest'
 *      will contain the compressed lz4s format data.
 *
 *      The user callback function should be provided through the
 *      QzSessionParams_T. And set data format of compression to
 *      'QZ_LZ4S_FH', then post-processing will be trigger.
 *
 *      qzCallback's first parameter 'external' can be a customized
 *      compression context which can be setup before QAT qzSetupSession.
 *      It can be provided to QATZip though the 'qzCallback_external'
 *      variable in the QzSessionParams_T structure.
 *
 *      ExtStatus will be embedded into extended return codes when
 *      qzLZ4SCallbackFn return `QZ_POST_PROCESS_ERROR`. See extended return
 *      code section and *Ext API for details.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       external    User context provided through the
 *                              'qzCallback_external' pointer in the
 *                              QzSessionParams_T structure.
 * @param[in]       src         Point to source buffer
 * @param[in,out]   src_len     Length of source buffer. Modified to number
 *                              of bytes consumed
 * @param[in]       dest        Point to destination buffer
 * @param[in,out]   dest_len    Length of destination buffer. Modified
 *                              to length of compressed data when
 *                              function returns
 * @param[in,out]   ExtStatus   'qzCallback' customized error code.
 *
 * @retval QZ_OK                    Function executed successfully
 * @retval QZ_FAIL                  Function did not succeed
 * @retval QZ_PARAMS                params are invalid
 * @retval QZ_POST_PROCESS_ERROR    post processing error
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 * @see
 *      None
 *
 *****************************************************************************/
typedef int (*qzLZ4SCallbackFn)(void *external, const unsigned char *src,
                                unsigned int *src_len, unsigned char *dest,
                                unsigned int *dest_len, int *ExtStatus);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Session Initialization parameters
 *
 * @description
 *      This structure contains data for initializing a session.
 *
 *****************************************************************************/
typedef struct QzSessionParams_S {
    QzHuffmanHdr_T huffman_hdr;
    /**< Dynamic or Static Huffman headers */
    QzDirection_T direction;
    /**< Compress or decompress */
    QzDataFormat_T data_fmt;
    /**< Deflate, deflate with GZip or deflate with GZip ext */
    unsigned int comp_lvl;
    /**< Compression level 1 to 9 */
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
#ifdef ERR_INJECTION
    void *fbError;
    void *fbErrorCurr;
    /* Linked list for simulated errors from HW */
#endif
} QzSessionParams_T;

typedef struct QzSessionParamsCommon_S {
    QzDirection_T direction;
    /**< Compress or decompress */
    unsigned int comp_lvl;
    /**< Compression level 1 to 9 */
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
    QzPollingMode_T polling_mode;
    /**< 0 means no busy polling, 1 means busy polling */
    unsigned int is_sensitive_mode;
    /**< 0 means disable sensitive mode, 1 means enable sensitive mode*/
#ifdef ERR_INJECTION
    void *fbError;
    void *fbErrorCurr;
    /* Linked list for simulated errors from HW */
#endif
} QzSessionParamsCommon_T;

typedef struct QzSessionParamsDeflate_S {
    QzSessionParamsCommon_T common_params;
    QzHuffmanHdr_T huffman_hdr;
    /**< Dynamic or Static Huffman headers */
    QzDataFormat_T data_fmt;
    /**< Deflate, deflate with GZip or deflate with GZip ext */
} QzSessionParamsDeflate_T;

typedef struct QzSessionParamsLZ4_S {
    QzSessionParamsCommon_T common_params;
} QzSessionParamsLZ4_T;

typedef struct QzSessionParamsLZ4S_S {
    QzSessionParamsCommon_T common_params;
    qzLZ4SCallbackFn qzCallback;
    /**< post processing callback for zstd compression*/
    void *qzCallback_external;
    /**< An opaque pointer provided by the user to be passed */
    /**< into qzCallback during post processing*/
    unsigned int lz4s_mini_match;
    /**< Set lz4s dictionary mini match, which would be 3 or 4 */
} QzSessionParamsLZ4S_T;

#define QZ_HUFF_HDR_DEFAULT          QZ_DYNAMIC_HDR
#define QZ_DIRECTION_DEFAULT         QZ_DIR_BOTH
#define QZ_DATA_FORMAT_DEFAULT       QZ_DEFLATE_GZIP_EXT
#define QZ_COMP_LEVEL_DEFAULT        1
#define QZ_COMP_ALGOL_DEFAULT        QZ_DEFLATE
#define QZ_POLL_SLEEP_DEFAULT        10
#define QZ_MAX_FORK_DEFAULT          3
#define QZ_SW_BACKUP_DEFAULT         1
#define QZ_HW_BUFF_SZ                (64*1024)
#define QZ_HW_BUFF_SZ_Gen3           (1*1024*1024)
#define QZ_HW_BUFF_MIN_SZ            (1*1024)
#define QZ_HW_BUFF_MAX_SZ            (512*1024)
#define QZ_HW_BUFF_MAX_SZ_Gen3       (2*1024*1024*1024U)
#define QZ_STRM_BUFF_SZ_DEFAULT      QZ_HW_BUFF_SZ
#define QZ_STRM_BUFF_MIN_SZ          (1*1024)
#define QZ_STRM_BUFF_MAX_SZ          (2*1024*1024 - 5*1024)
#define QZ_COMP_THRESHOLD_DEFAULT    1024
#define QZ_COMP_THRESHOLD_MINIMUM    128
#define QZ_REQ_THRESHOLD_MINIMUM     1
#define QZ_REQ_THRESHOLD_MAXIMUM     NUM_BUFF
#define QZ_REQ_THRESHOLD_DEFAULT     QZ_REQ_THRESHOLD_MAXIMUM
#define QZ_WAIT_CNT_THRESHOLD_DEFAULT 8
#define QZ_DEFLATE_COMP_LVL_MINIMUM      (1)
#define QZ_DEFLATE_COMP_LVL_MAXIMUM      (9)
#define QZ_DEFLATE_COMP_LVL_MAXIMUM_Gen3 (12)
#define QZ_LZS_COMP_LVL_MINIMUM          (1)
#define QZ_LZS_COMP_LVL_MAXIMUM          (12)

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Session software configuration settings
 *
 * @description
 *      The following definitions can be used with the sw_backup variable in
 *      structs and functions to configure the session
 *
 *      QZ_ENABLE_SOFTWARE_BACKUP          Congifure session with software
 *                                         fallback
 *
 *      QZ_ENABLE_SOFTWARE_ONLY_EXECUTION  Configure session to only use
 *                                         software
 *****************************************************************************/
#define QZ_SW_BACKUP_BIT_POSITION   (0)
#define QZ_SW_FORCESW_BIT_POSITION  (1)

#define QZ_ENABLE_SOFTWARE_BACKUP(_BackupVariable) \
        (_BackupVariable |= (1 << QZ_SW_BACKUP_BIT_POSITION))
/**< SW backup/fallback enabled */
#define QZ_ENABLE_SOFTWARE_ONLY_EXECUTION(_BackupVariable) \
        (_BackupVariable |= (1 << QZ_SW_FORCESW_BIT_POSITION))
/**< Force SW to perform all compression/decompression operations */

#define QZ_DISABLE_SOFTWARE_BACKUP(_BackupVariable) \
        (_BackupVariable &= ~(1 << QZ_SW_BACKUP_BIT_POSITION))
/**< SW backup/fallback disabled */
#define QZ_DISABLE_SOFTWARE_ONLY_EXECUTION(_BackupVariable) \
        (_BackupVariable &= ~(1 << QZ_SW_FORCESW_BIT_POSITION))
/**< Disable SW only compression/decompression operations*/

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Extended return information
 *
 * @description
 *      The following definitions can be used with the extended return
 *      values.
 *
 *      QZ_SW_EXECUTION indicates if a request for services was performed in
 *      software.
 *
 *      QZ_HW_TIMEOUT indicates if a request to hardware was timed out.
 *
 *      If set in the extended return value, QZ_POST_PROCESS_FAIL indicates
 *      post processing of the LZ4s compressed data has failed.
 *****************************************************************************/
#define QZ_SW_EXECUTION_BIT           (4)
#define QZ_SW_EXECUTION_MASK         (1 << QZ_SW_EXECUTION_BIT)
#define QZ_SW_EXECUTION(ret, ext_rc) \
     (!ret && (ext_rc & QZ_SW_EXECUTION_MASK))

#define QZ_TIMEOUT_BIT                (8)
#define QZ_TIMEOUT_MASK              (1 << QZ_TIMEOUT_BIT)
#define QZ_HW_TIMEOUT(ret, ext_rc)   \
     (!ret && (ext_rc & QZ_TIMEOUT_MASK))

#define QZ_POST_PROCESS_FAIL_BIT      (10)
#define QZ_POST_PROCESS_FAIL_MASK    (1 << QZ_POST_PROCESS_FAIL_BIT)
#define QZ_POST_PROCESS_FAIL(ret, ext_rc)   \
     (ret && (ext_rc & QZ_POST_PROCESS_FAIL_MASK))

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Session opaque data storage
 *
 * @description
 *      This structure contains a pointer to a structure with
 *    session state.
 *
 *****************************************************************************/
typedef struct QzSession_S {
    signed long int hw_session_stat;
    /**< Filled in during initialization, session startup and decompression */
    int thd_sess_stat;
    /**< Note process compression and decompression thread state */
    void *internal;
    /**< Session data is opaque to outside world */
    unsigned long total_in;
    /**< Total processed input data length in this session */
    unsigned long total_out;
    /**< Total output data length in this session */
} QzSession_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip status structure
 *
 * @description
 *      This structure contains data relating to the status of QAT on the
 *    platform.
 *
 *****************************************************************************/
typedef struct QzStatus_S {
    unsigned short int qat_hw_count;
    /**< From PCI scan */
    unsigned char qat_service_init;
    /**< Check if the available services have been initialized */
    unsigned char qat_mem_drvr;
    /**< 1 if /dev/qat_mem exists */
    /**< 2 if /dev/qat_mem has been opened */
    /**< 0 otherwise */
    unsigned char qat_instance_attach;
    /**< Is this thread/g_process properly attached to an Instance? */
    unsigned long int memory_alloced;
    /**< Amount of memory allocated by this thread/process */
    unsigned char using_huge_pages;
    /**< Are memory slabs coming from huge pages? */
    signed long int hw_session_status;
    /**< One of QATzip Session Status */
    unsigned char algo_sw[QZ_MAX_ALGORITHMS];
    /**< Support software algorithms */
    unsigned char algo_hw[QZ_MAX_ALGORITHMS];
    /**< Count of hardware devices supporting algorithms */
} QzStatus_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip software version structure
 *
 * @description
 *      This structure contains data relating to the versions of a QATZip or a
 *    subcomponent of this library platform.
 *
 *****************************************************************************/
#define QZ_MAX_STRING_LENGTH 64
typedef struct QzSoftwareVersionInfo_S {
    QzSoftwareComponentType_T component_type;
    unsigned char component_name[QZ_MAX_STRING_LENGTH];
    unsigned int major_version;
    unsigned int minor_version;
    unsigned int patch_version;
    unsigned int build_number;
    unsigned char reserved[52];
} QzSoftwareVersionInfo_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip CRC64 configuration structure
 *
 * @description
 *      This structure contains data relating to configuration of the sessions
 *   CRC64 functionality.Session defaults to using ECMA-182 Normal on creation.
 *
 *****************************************************************************/
typedef struct QzCrc64Config_S {
    uint64_t polynomial;
    /**< Polynomial used for CRC64 calculation. Default 0x42F0E1EBA9EA3693 */
    uint64_t initial_value;
    /**< Defaults to 0x0000000000000000 */
    uint32_t reflect_in;
    /**< Reflect bit order before CRC calculation. Default 0 */
    uint32_t reflect_out;
    /**< Reflect bit order after CRC calculation.Default 0 */
    uint64_t xor_out;
    /**< Defaults to 0x0000000000000000 */
} QzCrc64Config_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip pointer to opaque metadata.
 *
 * @description
 *      The opaque pointer to metadata.
 *
 *****************************************************************************/
typedef void *QzMetadataBlob_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Initialize QAT hardware
 *
 * @description
 *      This function initializes the QAT hardware.
 *    This function is optional in the function calling sequence. If
 *    desired, this call can be made to avoid latency impact during the
 *    first call to qzDecompress or qzCompress, or to set the sw_backup
 *    parameter explicitly.
 *    The input parameter sw_backup specifies the behavior of the function
 *    and that of the functions called with the same session in the event
 *    there are insufficient resources to establish a QAT based compression
 *    or decompression session.
 *
 *    The required resources include access to the QAT hardware, contiguous
 *    pinned memory for mapping the hardware rings, and contiguous
 *    pinned memory for buffers.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      This function will:
 *        1) start the user space driver if necessary
 *        2) allocate all hardware instances available
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess           Session handle
 *                                 (pointer to opaque instance and
 *                                 session data.)
 * @param[in]       sw_backup      see QZ_SW_* definitions for expected behavior
 *
 * @retval QZ_OK                   Function executed successfully. A hardware
 *                                 or software instance has been allocated to
 *                                 the calling process/thread
 * @retval QZ_DUPLICATE            This process/thread already has a hardware
 *                                 instance
 * @retval QZ_PARAMS               *sess is NULL
 * @retval QZ_NOSW_NO_HW           No hardware and no software session being
 *                                 established
 * @retval QZ_NOSW_NO_MDRV         No memory driver. No software session
 *                                 established
 * @retval QZ_NOSW_NO_INST_ATTACH  No instance available
 *                                 No software session established
 * @retval QZ_NOSW_LOW_MEM         Not enough pinned memory available
 *                                 No software session established
 * @retval QZ_UNSUPPORTED_FMT           No support for requested algorithm;
 *                                      using software
 * @retval QZ_NOSW_UNSUPPORTED_FMT      No support for requested algorithm;
 *                                      No software session established
 * @retval QZ_NO_SW_AVAIL          No software is available. This will be
 *                                 returned when sw_backup is set but the
 *                                 session does not support software operations
 *                                 or software fallback is unavailable
 *                                 to the application.
 *
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzInit(QzSession_T *sess,  unsigned char sw_backup);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Initialize a QATzip session
 *
 * @description
 *      This function establishes a QAT session. This involves associating
 *    a hardware instance to the session, allocating buffers. If all of
 *    these activities can not be completed successfully, then this function
 *    will set up a software based session of param->sw_backup that is set to 1.
 *
 *    Before this function is called, the hardware must have been
 *    successfully started via qzInit.
 *
 *    If *sess includes an existing hardware or software session, then
 *    QZ_DUPLICATE will be returned without modifying the existing session.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess           Session handle
 *                                 (pointer to opaque instance and session data)
 * @param[in]       params         Parameters for session
 *
 *
 * @retval QZ_OK                   Function executed successfully. A hardware
 *                                 or software based compression session has been
 *                                 created
 * @retval QZ_DUPLICATE            *sess includes an existing hardware or
 *                                 software session
 * @retval QZ_PARAMS               *sess is NULL or member of params is invalid
 * @retval QZ_NOSW_NO_HW           No hardware and no sw session being
 *                                 established
 * @retval QZ_NOSW_NO_MDRV         No memory driver. No software session
 *                                 established
 * @retval QZ_NOSW_NO_INST_ATTACH  No instance available
 *                                 No software session established
 * @retval QZ_NO_LOW_MEM           Not enough pinned memory available
 *                                 No software session established
 * @retval QZ_UNSUPPORTED_FMT           No support for requested algorithm;
 *                                      using software
 * @retval QZ_NOSW_UNSUPPORTED_FMT      No support for requested algorithm;
 *                                      No software session established
 * @retval QZ_NO_SW_AVAIL          No software is available. This may returned
 *                                 when sw_backup is set to 1 but the session
 *                                 does not support software backup or software
 *                                 backup is unavailable to the application.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzSetupSession(QzSession_T *sess,  QzSessionParams_T *params);

QATZIP_API int qzSetupSessionDeflate(QzSession_T *sess,
                                     QzSessionParamsDeflate_T *params);

QATZIP_API int qzSetupSessionLZ4(QzSession_T *sess,
                                 QzSessionParamsLZ4_T *params);

QATZIP_API int qzSetupSessionLZ4S(QzSession_T *sess,
                                  QzSessionParamsLZ4S_T *params);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Compress a buffer
 *
 * @description
 *      This function will compress a buffer if either a hardware based
 *    session or a software based session is available. If no session has
 *    been established - as indicated by the contents of *sess - then this
 *    function will attempt to set up a session using qzInit and qzSetupSession.
 *
 *    The resulting compressed block of data will be composed of one or more
 *    gzip blocks, as per RFC 1952.
 *
 *    This function will place completed compression blocks in the output
 *    buffer.
 *
 *    The caller must check the updated src_len. This value will be the
 *    number of consumed bytes on exit. The calling API may have to
 *    process the destination buffer and call again.
 *
 *    The parameter dest_len will be set to the number of bytes produced in
 *    the destination buffer. This value may be zero if no data was produced
 *    which may occur if the consumed data is retained internally. A
 *    possible reason for this may be small amounts of data in the src
 *    buffer.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess     Session handle
 *                           (pointer to opaque instance and session data)
 * @param[in]       src      Point to source buffer
 * @param[in,out]   src_len  Length of source buffer. Modified to number
 *                           of bytes consumed
 * @param[in]       dest     Point to destination buffer
 * @param[in,out]   dest_len Length of destination buffer. Modified
 *                           to length of compressed data when
 *                           function returns
 * @param[in]       last     1 for 'No more data to be compressed'
 *                           0 for 'More data to be compressed'
 * @param[in,out]   ext_rc   qzCompressExt only.
 *                           If not NULL, ext_rc point to a location where
 *                           extended return codes may be returned. See
 *                           extended return code section for details.
 *                           if NULL, no extended information will be
 *                           provided.
 *
 * @retval QZ_OK             Function executed successfully
 * @retval QZ_FAIL           Function did not succeed
 * @retval QZ_PARAMS         *sess is NULL or member of params is invalid
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzCompress(QzSession_T *sess, const unsigned char *src,
                          unsigned int *src_len, unsigned char *dest,
                          unsigned int *dest_len, unsigned int last);

QATZIP_API int qzCompressExt(QzSession_T *sess, const unsigned char *src,
                             unsigned int *src_len, unsigned char *dest,
                             unsigned int *dest_len, unsigned int last,
                             uint64_t *ext_rc);


/**
 *****************************************************************************
 * @ingroup qatZip
 *      Compress a buffer and return the CRC checksum
 *
 * @description
 *      This function will compress a buffer if either a hardware based
 *    session or a software based session is available. If no session has been
 *    established - as indicated by the contents of *sess - then this function
 *    will attempt to set up a session using qzInit and qzSetupSession.
 *
 *    The resulting compressed block of data will be composed of one or more
 *    gzip blocks, as per RFC 1952.
 *
 *    This function will place completed compression blocks in the output
 *    buffer and put CRC32 or CRC64 checksum for compressed input data in
 *    the user provided buffer *crc.
 *
 *    The caller must check the updated src_len. This value will be the
 *    number of consumed bytes on exit. The calling API may have to
 *    process the destination buffer and call again.
 *
 *    The parameter dest_len will be set to the number of bytes produced in
 *    the destination buffer. This value may be zero if no data was produced
 *    which may occur if the consumed data is retained internally. A
 *    possible reason for this may be small amounts of data in the src
 *    buffer.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess     Session handle
 *                           (pointer to opaque instance and session data)
 * @param[in]       src      Point to source buffer
 * @param[in,out]   src_len  Length of source buffer. Modified to number
 *                           of bytes consumed
 * @param[in]       dest     Point to destination buffer
 * @param[in,out]   dest_len Length of destination buffer. Modified
 *                           to length of compressed data when
 *                           function returns
 * @param[in]       last     1 for 'No more data to be compressed'
 *                           0 for 'More data to be compressed'
 * @param[in,out]   crc      Pointer to CRC32 or CRC64 checksum buffer
 * @param[in,out]   ext_rc   qzCompressCrcExt or qzCompressCrc64Ext only.
 *                           If not NULL, ext_rc point to a location where
 *                           extended return codes may be returned. See
 *                           extended return code section for details.
 *                           if NULL, no extended information will be
 *                           provided.
 *
 * @retval QZ_OK             Function executed successfully
 * @retval QZ_FAIL           Function did not succeed
 * @retval QZ_PARAMS         *sess is NULL or member of params is invalid
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzCompressCrc(QzSession_T *sess,
                             const unsigned char *src,
                             unsigned int *src_len,
                             unsigned char *dest,
                             unsigned int *dest_len,
                             unsigned int last,
                             unsigned long *crc);

QATZIP_API int qzCompressCrcExt(QzSession_T *sess,
                                const unsigned char *src,
                                unsigned int *src_len,
                                unsigned char *dest,
                                unsigned int *dest_len,
                                unsigned int last,
                                unsigned long *crc,
                                uint64_t *ext_rc);

QATZIP_API int qzCompressCrc64(QzSession_T *sess,
                               const unsigned char *src,
                               unsigned int *src_len,
                               unsigned char *dest,
                               unsigned int *dest_len,
                               unsigned int last,
                               uint64_t *crc);

QATZIP_API int qzCompressCrc64Ext(QzSession_T *sess,
                                  const unsigned char *src,
                                  unsigned int *src_len,
                                  unsigned char *dest,
                                  unsigned int *dest_len,
                                  unsigned int last,
                                  uint64_t *crc,
                                  uint64_t *ext_rc);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Compress a buffer and write metadata for each compressed block into
 *      the opaque metadata structure.
 *
 * @description
 *      This function will compress a buffer if either a hardware based
 *      session or a software based session is available. If no session has
 *      been established - as indicated by the contents of *sess - then this
 *      function will attempt to set up a session using qzInit and
 *      qzSetupSession.
 *
 *      This function will place completed compression blocks in the output
 *      buffer.
 *
 *      The caller must check the updated src_len. This value will be the
 *      number of consumed bytes on exit. The calling API may have to
 *      process the destination buffer and call again.
 *
 *      The parameter dest_len will be set to the number of bytes produced in
 *      the destination buffer. This value may be zero if no data was produced
 *      which may occur if the consumed data is retained internally. A
 *      possible reason for this may be small amounts of data in the src
 *      buffer.
 *
 *      The metadata for each compressed block will be written into the opaque
 *      metadata structure specified as function param metadata.
 *
 *      comp_thrshold specifies compression threshold of a block.
 *      If compressed size of the block is > comp_thrshold, the
 *      compression function shall copy the uncompressed data to the output
 *      buffer and set the size of the block in the metadata to the size of the
 *      uncompressed block. If the compressed size of the block is <=
 *      comp_thrshold, the compressed data will be copied to the output buffer
 *      and the compressed size will be set in the metadata.
 *
 *      hw_buff_sz_override specifies the data size to be used for the each
 *      compression operation. It overrides the hw_buff_sz parameter specified
 *      at session creation. If 0 is provided for this parameter, then the
 *      hw_buff_sz specified at session creation will be used. Memory for the
 *      opaque metadata structure should be allocated based on the hw_buff_sz
 *      or the hw_buff_sz_override that is used for the compression operation.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess                Session handle
 *                                      (pointer to opaque instance and
 *                                      session data)
 * @param[in]       src                 Point to source buffer.
 * @param[in,out]   src_len             Length of source buffer. Modified to
 *                                      number of bytes consumed.
 * @param[in]       dest                Point to destination buffer.
 * @param[in,out]   dest_len            Length of destination buffer. Modified
 *                                      to length of compressed data when
 *                                      function returns.
 * @param[in]       last                1 for 'No more data to be compressed'
 *                                      0 for 'More data to be compressed'
 * @param[in,out]   ext_rc              If not NULL, ext_rc point to a location
 *                                      where extended return codes may be
 *                                      returned. See extended return code
 *                                      section for details. if NULL, no
 *                                      extended information will be provided.
 * @param[in,out]   metadata            Pointer to opaque metadata.
 * @param[in]       hw_buff_sz_override Data size to be used for compression.
 * @param[in]       comp_thrshold       Compressed block threshold.
 *
 * @retval QZ_OK                        Function executed successfully
 * @retval QZ_FAIL                      Function did not succeed
 * @retval QZ_PARAMS                    *sess or *metadata is NULL or Member of
 *                                      params is invalid, hw_buff_sz_override
 *                                      is invalid data size.
 * @retval QZ_METADATA_OVERFLOW         Unable to populate metadata due to
 *                                      insufficient memory allocated.
 * @retval QZ_NOT_SUPPORTED             Compression with metadata is not
 *                                      supported with given algorithm
 *                                      or format.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzCompressWithMetadataExt(QzSession_T *sess,
        const unsigned char *src,
        unsigned int *src_len,
        unsigned char *dest,
        unsigned int *dest_len,
        unsigned int last,
        uint64_t *ext_rc,
        QzMetadataBlob_T *metadata,
        uint32_t hw_buff_sz_override,
        uint32_t comp_thrshold);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Decompress a buffer
 *
 * @description
 *      This function will decompress a buffer if either a hardware based
 *    session or a software based session is available. If no session has been
 *    established - as indicated by the contents of *sess - then this function
 *    will attempt to set up a session using qzInit and qzSetupSession.
 *
 *    The input compressed block of data will be composed of one or more
 *    gzip blocks, as per RFC 1952.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]     sess      Session handle
 *                          (pointer to opaque instance and session data)
 * @param[in]     src       Point to source buffer
 * @param[in]     src_len   Length of source buffer. Modified to
 *                          length of processed compressed data
 *                          when function returns
 * @param[in]      dest     Point to destination buffer
 * @param[in,out]  dest_len Length of destination buffer. Modified
 *                          to length of decompressed data when
 *                          function returns
 * @param[in,out]   ext_rc  qzDecompressExt only.
 *                          If not NULL, ext_rc point to a location where
 *                          extended return codes may be returned. See
 *                          extended return code section for details.
 *                          if NULL, no extended information will be
 *                          provided.
 *
 * @retval QZ_OK            Function executed successfully
 * @retval QZ_FAIL          Function did not succeed
 * @retval QZ_PARAMS        *sess is NULL or member of params is invalid
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzDecompress(QzSession_T *sess, const unsigned char *src,
                            unsigned int *src_len, unsigned char *dest,
                            unsigned int *dest_len);

QATZIP_API int qzDecompressExt(QzSession_T *sess, const unsigned char *src,
                               unsigned int *src_len, unsigned char *dest,
                               unsigned int *dest_len, uint64_t *ext_rc);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Decompress a buffer and return the CRC checksum
 *
 * @description
 *      This function will decompress a buffer if either a hardware based
 *    session or a software based session is available. If no session has been
 *    established - as indicated by the contents of *sess - then this function
 *    will attempt to set up a session using qzInit and qzSetupSession.
 *
 *    This function will place completed decompression chunks in the output
 *    buffer and put the CRC32 or CRC64 checksum for compressed input data in
 *    the user provided buffer *crc.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]     sess      Session handle
 *                          (pointer to opaque instance and session data)
 * @param[in]     src       Point to source buffer
 * @param[in]     src_len   Length of source buffer. Modified to
 *                          length of processed compressed data
 *                          when function returns
 * @param[in]      dest     Point to destination buffer
 * @param[in,out]  dest_len Length of destination buffer. Modified
 *                          to length of decompressed data when
 *                          function returns
 * @param[in,out]  crc      Pointer to CRC32 or CRC64 checksum buffer
 * @param[in,out]  ext_rc   qzDecompressCrcExt or qzDecompressCrc64Ext only.
 *                          If not NULL, ext_rc point to a location where
 *                          extended return codes may be returned. See
 *                          extended return code section for details.
 *                          if NULL, no extended information will be
 *                          provided.
 *
 * @retval QZ_OK            Function executed successfully
 * @retval QZ_FAIL          Function did not succeed
 * @retval QZ_PARAMS        *sess is NULL or member of params is invalid
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzDecompressCrc(QzSession_T *sess,
                               const unsigned char *src,
                               unsigned int *src_len,
                               unsigned char *dest,
                               unsigned int *dest_len,
                               unsigned long *crc);

QATZIP_API int qzDecompressCrcExt(QzSession_T *sess,
                                  const unsigned char *src,
                                  unsigned int *src_len,
                                  unsigned char *dest,
                                  unsigned int *dest_len,
                                  unsigned long *crc,
                                  uint64_t *ext_rc);

QATZIP_API int qzDecompressCrc64(QzSession_T *sess,
                                 const unsigned char *src,
                                 unsigned int *src_len,
                                 unsigned char *dest,
                                 unsigned int *dest_len,
                                 uint64_t *crc);

QATZIP_API int qzDecompressCrc64Ext(QzSession_T *sess,
                                    const unsigned char *src,
                                    unsigned int *src_len,
                                    unsigned char *dest,
                                    unsigned int *dest_len,
                                    uint64_t *crc,
                                    uint64_t *ext_rc);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Decompress a buffer with metadata.
 *
 * @description
 *      This function will decompress a buffer if either a hardware based
 *      session or a software based session is available.
 *      If no session has been established - as indicated by the content
 *      of *sess - then this function will attempt to set up a session using
 *      qzInit and qzSetupSession.
 *
 *      The metadata function parameter specifies metadata of compressed file
 *      which can be used for regular or parallel decompression.
 *
 *      hw_buff_sz_override specifies the data size to be used for the each
 *      decompression operation. It overrides the hw_buff_sz parameter specified
 *      at session creation. If 0 is provided for this parameter, then the
 *      hw_buff_sz specified at session creation will be used. Memory for the
 *      opaque metadata structure should be allocated based on the hw_buff_sz
 *      or the hw_buff_sz_override that is used for the compression operation.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess                Session handle
 *                                      (pointer to opaque instance and session
 data)
 * @param[in]       src                 Point to source buffer
 * @param[in]       src_len             Length of source buffer. Modified to
 *                                      length of processed compressed data
 *                                      when function returns
 * @param[in]       dest                Point to destination buffer
 * @param[in,out]   dest_len            Length of destination buffer. Modified
 *                                      to length of decompressed data when
 *                                      function returns
 * @param[in,out]   ext_rc              If not NULL, ext_rc points to a location
 *                                      where extended return codes may be
 *                                      returned. See extended return code
 *                                      section for details.
 *                                      if NULL, no extended information will be
 *                                      provided.
 * @param[in]       metadata            Pointer to opaque metadata.
 * @param[in]       hw_buff_sz_override Expected size of decompressed block.
 *
 * @retval QZ_OK                        Function executed successfully.
 * @retval QZ_FAIL                      Function did not succeed.
 * @retval QZ_PARAMS                    *sess or *metadata is NULL or Member of
 *                                      params is invalid, hw_buff_sz_override
 *                                      is invalid data size.
 * @retval QZ_METADATA_OVERFLOW         Unable to populate metadata due to
 *                                      insufficient memory allocated.
 * @retval QZ_NOT_SUPPORTED             Decompression with metadata is not
 *                                      supported with given algorithm
 *                                      or format.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzDecompressWithMetadataExt(QzSession_T *sess,
        const unsigned char *src,
        unsigned int *src_len,
        unsigned char *dest,
        unsigned int *dest_len,
        uint64_t *ext_rc,
        QzMetadataBlob_T *metadata,
        uint32_t hw_buff_sz_override);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Uninitialize a QATzip session
 *
 * @description
 *      This function disconnects a session from a hardware instance and
 *    deallocates buffers. If no session has been initialized, then no
 *    action will take place.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess  Session handle
 *                        (pointer to opaque instance and session data)
 *
 * @retval QZ_OK          Function executed successfully
 * @retval QZ_FAIL        Function did not succeed
 * @retval QZ_PARAMS      *sess is NULL or member of params is invalid
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzTeardownSession(QzSession_T *sess);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Terminates a QATzip session
 *
 * @description
 *      This function closes the connection with QAT.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess  Session handle
 *                        (pointer to opaque instance and session data)
 *
 * @retval QZ_OK          Function executed successfully
 * @retval QZ_FAIL        Function did not succeed
 * @retval QZ_PARAMS      *sess is NULL or member of params is invalid
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzClose(QzSession_T *sess);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Get current QAT status
 *
 * @description
 *    This function retrieves the status of QAT in the platform.
 *    The status structure will be filled in as follows:
 *    qat_hw_count         Number of discovered QAT devices on PCU bus
 *    qat_service_init     1 if qzInit has been successfully run, 0 otherwise
 *    qat_mem_drvr         1 if the QAT memory driver is installed, 0 otherwise
 *    qat_instance_attach  1 if session has attached to a hardware instance,
 *                         0 otherwise
 *    memory_alloced       Amount of memory, in kilobytes, from kernel or huge
 *                         pages allocated  by this process/thread.
 *    using_huge_pages     1 if memory is being allocated from huge pages, 0 if
 *                         memory is being allocated from standard kernel memory
 *    hw_session_status    Hw session status: one of:
 *                                            QZ_OK
 *                                            QZ_FAIL
 *                                            QZ_NO_HW
 *                                            QZ_NO_MDRV
 *                                            QZ_NO_INST_ATTACH
 *                                            QZ_LOW_MEM
 *                                            QZ_NOSW_NO_HW
 *                                            QZ_NOSW_NO_MDRV
 *                                            QZ_NOSW_NO_INST_ATTACH
 *                                            QZ_NOSW_LOW_MEM
 *                                            QZ_NO_SW_AVAIL
 *
 * Applications should verify the elements of the status structure are
 * correct for the required operations. It should be noted that some
 * information will be available only after qzInit has been called, either
 * implicitly or explicitly.  The qat_service_init element of the status
 * structure will indicate if initialization has taken place.
 *
 * The hw_session_status will depend on the availability of hardware based
 * compression and software based compression. The following table indicates
 * what hw_session_status based on the availability of compression engines
 * and the sw_backup flag.
 *
 * | HW    | SW Engine | sw_backup | hw_session_stat   |
 * | avail | avail     | setting   |                   |
 * |-------|-----------|-----------|-------------------|
 * | N     | N         | 0         | QZ_NOSW_NO_HW     |
 * | N     | N         | 1         | QZ_NOSW_NO_HW     |
 * | N     | Y         | 0         | QZ_FAIL           |
 * | N     | Y         | 1         | QZ_NO_HW (1)      |
 * | Y     | N         | 0         | QZ_OK             |
 * | Y     | N         | 1         | QZ_NO_SW_AVAIL (2)|
 * | Y     | Y         | 0         | QZ_OK             |
 * | Y     | Y         | 1         | QZ_OK             |
 *
 * Note 1:
 *     If an application indicates software backup is required by setting
 *     sw_backup=1, and a software engine is available and if no hardware based
 *     compression engine is available then the hw_session_status will be set
 *     to QZ_NO_HW.  All compression and decompression will use the software
 *     engine.
 * Note 2:
 *     If an application indicates software backup is required by setting
 *     sw_backup=1, and if no software based compression engine is available
 *     then the hw_session_status will be set to QZ_NO_SW_AVAIL.  In this
 *     case, QAT based compression may be used however no software backup
 *     will available.
 *     If the application relies on software backup being avialable, then
 *     this return code can be treated as an error.
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess    Session handle
 *                          (pointer to opaque instance and session data)
 * @param[in]       status  Pointer to QATzip status structure
 * @retval QZ_OK            Function executed successfully. The hardware based
 *                          compression session has been created
 * @retval QZ_PARAMS        *status is NULL
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzGetStatus(QzSession_T *sess, QzStatus_T *status);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Get the maximum compressed output length
 *
 * @description
 *      Get the maximum compressed output length.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]
 *         src_sz     Input data length in bytes
 *         sess       Session handle
 *                    (pointer to opaque instance and session data)
 *
 * @retval dest_sz    Max compressed data output length in bytes.
 *                    When src_sz is equal to 0, the return value is QZ_COMPRESSED_SZ_OF_EMPTY_FILE(34).
 *                    When integer overflow happens, the return value is 0
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
#define QZ_SKID_PAD_SZ 48
#define QZ_COMPRESSED_SZ_OF_EMPTY_FILE 34
QATZIP_API
unsigned int qzMaxCompressedLength(unsigned int src_sz, QzSession_T *sess);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Set default QzSessionParams_T value
 *
 * @description
 *      Set default QzSessionParams_T value.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]
 *              defaults   The pointer to value to be set as default
 *
 * @retval      QZ_OK      Success on setting default value
 * @retval      QZ_PARAM   Fail to set default value
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzSetDefaults(QzSessionParams_T *defaults);

QATZIP_API int qzSetDefaultsDeflate(QzSessionParamsDeflate_T *defaults);

QATZIP_API int qzSetDefaultsLZ4(QzSessionParamsLZ4_T *defaults);

QATZIP_API int qzSetDefaultsLZ4S(QzSessionParamsLZ4S_T *defaults);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Get default QzSessionParams_T value
 *
 * @description
 *      Get default QzSessionParams_T value.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]   defaults   The pointer to default value
 *
 * @retval      QZ_OK      Success on getting default value
 * @retval      QZ_PARAM   Fail to get default value
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzGetDefaults(QzSessionParams_T *defaults);

QATZIP_API int qzGetDefaultsDeflate(QzSessionParamsDeflate_T *defaults);

QATZIP_API int qzGetDefaultsLZ4(QzSessionParamsLZ4_T *defaults);

QATZIP_API int qzGetDefaultsLZ4S(QzSessionParamsLZ4S_T *defaults);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Allocate different types of memory
 *
 * @description
 *      Allocate different types of memory.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sz                  Memory size to be allocated
 * @param[in]       numa                NUMA node from which to allocate memory
 * @param[in]       force_pinned        PINNED_MEM allocate contiguous memory
 *                                      COMMON_MEM allocate non-contiguous memory
 *
 * @retval          NULL                Fail to allocate memory
 * @retval          address             The address of allocated memory
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API void *qzMalloc(size_t sz, int numa, int force_pinned);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Allocate memory for metadata.
 *
 * @description
 *      Allocate memory for metadata. The function takes the size of entire
 *      input buffer and the data size at which individual block will be
 *      compressed. These parameters will be used to calculate and allocate
 *      required memory for metadata.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in, out]       metadata       Pointer to opaque metadata.
 * @param[in]            data_size      Size of uncompressed buffer.
 * @param[in]            hw_buff_sz     Data size at which individual block
 *                                      will be compressed.
 *
 * @retval QZ_OK                        Function executed successfully
 * @retval QZ_FAIL                      Function did not succeed
 * @retval QZ_PARAMS                    *metadata is NULL, or data_size is 0,
 *                                      or data_size is greater than 1GB, or
 *                                      incorrect hw_buff_sz.
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzAllocateMetadata(QzMetadataBlob_T *metadata,
                                  size_t data_size,
                                  uint32_t hw_buff_sz);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Free allocated memory
 *
 * @description
 *      Free allocated memory.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       m                   Memory address to be freed
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API void qzFree(void *m);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Free memory allocated for metadata.
 *
 * @description
 *      Free memory allocated for metadata.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       metadata    Pointer to opaque metadata.
 *
 * @retval QZ_OK                Function executed successfully.
 * @retval QZ_FAIL              Function did not succeed.
 * @retval QZ_PARAMS            *metadata is NULL.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzFreeMetadata(QzMetadataBlob_T *metadata);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Check whether the address is available
 *
 * @description
 *      Check whether the address is available.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]
 *              a       Address to be checked
 *
 * @retval      1       The address is available
 * @retval      0       The address is not available
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzMemFindAddr(unsigned char *a);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      QATzip Stream data storage
 *
 * @description
 *      This structure contains metadata needed for stream operation.
 *
 *****************************************************************************/
typedef struct QzStream_S {
    unsigned int in_sz;
    /**< Set by application, reset by QATzip to indicate consumed data */
    unsigned int out_sz;
    /**< Set by application, reset by QATzip to indicate processed data */
    unsigned char *in ;
    /**< Input data pointer set by application */
    unsigned char *out ;
    /**< Output data pointer set by application */
    unsigned int pending_in;
    /**< Unprocessed bytes held in QATzip */
    unsigned int pending_out;
    /**< Processed bytes held in QATzip */
    QzCrcType_T crc_type;
    /**< Checksum type in Adler, CRC32 or none */
    unsigned int crc_32;
    /**< Checksum value */
    unsigned long long reserved;
    /**< Reserved for future use */
    void *opaque;
    /**< Internal storage managed by QATzip */
} QzStream_T;

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Compress data in stream and return checksum
 *
 * @description
 *      This function will compress data in stream buffer if either a hardware
 *    based session or a software based session is available. If no session
 *    has been established - as indicated by the contents of *sess - then this
 *    function will attempt to set up a session using qzInit and qzSetupSession.
 *    The function will start to compress the data when receiving sufficient
 *    number of bytes - as defined by hw_buff_sz in QzSessionParams_T - or
 *    reaching the end of input data - as indicated by last parameter.
 *
 *    The resulting compressed block of data will be composed of one or more
 *    gzip blocks, per RFC 1952, or deflate blocks, per RFC 1951.
 *
 *    This function will place completed compression blocks in the *out
 *    of QzStream_T structure and put checksum for compressed input data
 *    in crc32 of QzStream_T structure.
 *
 *    The caller must check the updated in_sz of QzStream_T. This value will
 *    be the number of consumed bytes on exit. The calling API may have to
 *    process the destination buffer and call again.
 *
 *    The parameter out_sz in QzStream_T will be set to the number of bytes
 *    produced in the destination buffer. This value may be zero if no data
 *    was produced which may occur if the consumed data is retained internally.
 *    A possible reason for this may be small amounts of data in the src
 *    buffer.
 *
 *    The caller must check the updated pending_in of QzStream_T. This value
 *    will be the number of unprocessed bytes held in QATzip. The calling API
 *    may have to feed more input data or indicate reaching the end of input
 *    and call again.
 *
 *    The caller must check the updated pending_out of QzStream_T. This value
 *    will be the number of processed bytes held in QATzip. The calling API
 *    may have to process the destination buffer and call again.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess     Session handle
 *                           (pointer to opaque instance and session data)
 * @param[in,out]   strm     Stream handle
 * @param[in]       last     1 for 'No more data to be compressed'
 *                           0 for 'More data to be compressed'
 *                           (always set to 1 in the Microsoft(R)
 *                           Windows(TM) QATzip implementation)
 *
 * @retval QZ_OK             Function executed successfully
 * @retval QZ_FAIL           Function did not succeed
 * @retval QZ_PARAMS         *sess is NULL or member of params is invalid
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API
int qzCompressStream(QzSession_T *sess, QzStream_T *strm, unsigned int last);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Decompress data in stream and return checksum
 *
 * @description
 *      This function will decompress data in stream buffer if either a hardware
 *    based session or a software based session is available. If no session
 *    has been established - as indicated by the contents of *sess - then this
 *    function will attempt to set up a session using qzInit and qzSetupSession.
 *    The function will start to decompress the data when receiving sufficient
 *    number of bytes - as defined by hw_buff_sz in QzSessionParams_T - or
 *    reaching the end of input data - as indicated by last parameter.
 *
 *    The input compressed block of data will be composed of one or more
 *    gzip blocks, per RFC 1952, or deflate blocks, per RFC 1951.
 *
 *    This function will place completed decompression blocks in the *out
 *    of QzStream_T structure and put checksum for decompressed data in
 *    crc32 of QzStream_T structure.
 *
 *    The caller must check the updated in_sz of QzStream_T. This value will
 *    be the number of consumed bytes on exit. The calling API may have to
 *    process the destination buffer and call again.
 *
 *    The parameter out_sz in QzStream_T will be set to the number of bytes
 *    produced in the destination buffer. This value may be zero if no data
 *    was produced which may occur if the consumed data is retained internally.
 *    A possible reason for this may be small amounts of data in the src
 *    buffer.
 *
 *    The caller must check the updated pending_in of QzStream_T. This value
 *    will be the number of unprocessed bytes held in QATzip. The calling API
 *    may have to feed more input data or indicate reaching the end of input
 *    and call again.
 *
 *    The caller must check the updated pending_out of QzStream_T. This value
 *    will be the number of processed bytes held in QATzip. The calling API
 *    may have to process the destination buffer and call again.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess     Session handle
 *                           (pointer to opaque instance and session data)
 * @param[in,out]   strm     Stream handle
 * @param[in]       last     1 for 'No more data to be compressed'
 *                           0 for 'More data to be compressed'
 *
 * @retval QZ_OK             Function executed successfully
 * @retval QZ_FAIL           Function did not succeed
 * @retval QZ_PARAMS         *sess is NULL or member of params is invalid
 * @retval QZ_NEED_MORE      *last is set but end of block is absent
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API
int qzDecompressStream(QzSession_T *sess, QzStream_T *strm, unsigned int last);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Terminates a QATzip stream
 *
 * @description
 *      This function disconnects stream handle from session handle then reset
 *    stream flag and release stream memory.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess  Session handle
 *                        (pointer to opaque instance and session data)
 *
 * @retval QZ_OK          Function executed successfully
 * @retval QZ_FAIL        Function did not succeed
 * @retval QZ_PARAMS      *sess is NULL or member of params is invalid
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzEndStream(QzSession_T *sess, QzStream_T *strm);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Requests the release versions of the QATZip Library sub components.
 *
 * @description
 *      Populate an array of pre-allocated QzSoftwareVersionInfo_T structs
 *    with the names and versions of QATzip sub components.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      Yes
 * @threadSafe
 *      Yes
 *
 * @param[in, out]   api_info  pointer to a QzSoftwareVersionInfo_T
 *                             structure to populate.
 * @param[in, out]   num_elem  pointer to an unsigned int expressing
 *                             how many elements are in the array
 *                             provided in api_info
 *
 * @retval QZ_OK               Function executed successfully
 * @retval QZ_FAIL             Function did not succeed
 * @retval QZ_NO_SW_AVAIL      Function did not find a software provider for
 *                             fallback
 * @retval QZ_NO_HW            Function did not find an installed kernel driver
 * @retval QZ_NOSW_NO_HW       Functions did not find an installed kernel driver
                               or software provider
 * @retval QZ_PARAMS           *api_info or num_elem is NULL or not large
 *                             enough to store all QzSoftwareVersionInfo_T
 *                             structures
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API
int qzGetSoftwareComponentVersionList(QzSoftwareVersionInfo_T *api_info,
                                      unsigned int *num_elem);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Requests the number of Software components used by the QATZip library
 *
 * @description
 *      This function populates num_elem variable with the number of
 *      software components available to the library.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      Yes
 * @threadSafe
 *      Yes
 *
 * @param[in, out]   num_elem  pointer to an unsigned int to populate
 *                             how many software componets are associated
 *                             with QATZip
 *
 * @retval QZ_OK               Function executed successfully
 * @retval QZ_FAIL             Function did not succeed
 * @retval QZ_NO_SW_AVAIL      Function did not find a software provider for
 *                             fallback
 * @retval QZ_NO_HW            Function did not find an installed kernel driver
 * @retval QZ_NOSW_NO_HW       Functions did not find an installed kernel driver
                               or software provider
 * @retval QZ_PARAMS           *num_elem is NULL
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzGetSoftwareComponentCount(unsigned int *num_elem);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Requests the CRC64 configuration of the provided session
 *
 * @description
 *      This function populates crc64_config with the CRC64 configuration
 *      details of sess. This function has a dependency on invoking a setup
 *      session function first.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      Yes
 * @threadSafe
 *      Yes
 *
 * @param[in]       sess           Session handle
 *                                 (pointer to opaque instance and session data)
 * @param[out]      crc64_config   Configuration for CRC 64 generation.
 *
 * @retval QZ_OK               Function executed successfully
 * @retval QZ_FAIL             Session was not setup
 * @retval QZ_PARAMS           *sess or *crc64_config is NULL
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzGetSessionCrc64Config(QzSession_T *sess,
                                       QzCrc64Config_T *crc64_config);

/**
*****************************************************************************
* @ingroup qatZip
*      Sets the CRC64 configuration of the provided session with a
*      user defined set of parameters.
*
* @description
*      This function populates the CRC64 configuration details of sess
*      using the paramaters provided in crc64_config. This function has a
*      dependency on invoking a setup session function first.
*
* @context
*      This function shall not be called in an interrupt context.
* @assumptions
*      None
* @sideEffects
*      None
* @blocking
*      Yes
* @reentrant
*      Yes
* @threadSafe
*      Yes
*
* @param[in]       sess           Session handle
*                                 (pointer to opaque instance and session data)
* @param[out]      crc64_config   Configuration for CRC 64 generation.
*
* @retval QZ_OK               Function executed successfully
* @retval QZ_FAIL             Session was not setup
* @retval QZ_PARAMS           *sess or *crc64_config is NULL or contains
*                             invalid paramters.
*
* @pre
*      None
* @post
*      None
* @note
*      Only a synchronous version of this function is provided.
*
* @see
*      None
*
*****************************************************************************/
QATZIP_API int qzSetSessionCrc64Config(QzSession_T *sess,
                                       QzCrc64Config_T *crc64_config);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Read metadata parameters.
 *
 * @description
 *      This function reads metadata information for the block specified by the
 *      function param block_num.
 *
 *      block_offset returns offset value in bytes from the previous compressed
 *      block of the compressed data.
 *
 *      block_size returns the block size in bytes of the compressed block.
 *      Some blocks may be uncompressed if size > threshold as specified during
 *      compression and the size returned will reflect the same.
 *
 *      block_flags returns the value 1 if the data is compressed and 0 if the
 *      data is not compressed.
 *
 *      block_hash returns the xxHash value of the plain text of the hw_buff_sz
 *      payload sent for compression operation.
 *
 *      If NULL is specified for any of the metadata parameters (block_offset,
 *      block_size, block_flags, block_hash) reading the parameter value
 *      will be ignored.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]            block_num      Block number of which metadata
 *                                      information should be read.
 * @param[in]            metadata       Pointer to opaque metadata.
 * @param[in, out]       block_offset   Pointer to the block offset value.
 * @param[in, out]       block_size     Pointer to the block size value.
 * @param[in, out]       block_flags    Pointer to the block flags value.
 * @param[in, out]       block_hash     Pointer to the block xxHash value.
 *
 * @retval QZ_OK                        Function executed successfully.
 * @retval QZ_FAIL                      Function did not succeed.
 * @retval QZ_PARAMS                    Metadata is NULL.
 * @retval QZ_OUT_OF_RANGE              block_num specified is out of range.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzMetadataBlockRead(uint32_t block_num,
                                   QzMetadataBlob_T *metadata,
                                   uint32_t *block_offset,
                                   uint32_t *block_size,
                                   uint32_t *block_flags,
                                   uint32_t *block_hash);

/**
 *****************************************************************************
 * @ingroup qatZip
 *      Write metadata parameters.
 *
 * @description
 *      This function writes metadata information for the block specified by the
 *      function param block_num.
 *
 *      block_offset writes offset value in bytes from the previous compressed
 *      block of the compressed data.
 *
 *      block_size writes the block size in bytes of the compressed block.
 *
 *      block_flags causes the metadata to indicate the data is compressed if
 *      passed a value of 1 and indicates uncompressed if value
 *      passed is zero (0).
 *
 *      block_hash writes the xxHash value of the plain text of the hw_buff_sz
 *      payload sent for compression operation.
 *
 *      If NULL is specified for any of the metadata parameters (block_offset,
 *      block_size, block_flags, block_hash) writing the parameter value
 *      into metadata will be ignored.
 *
 * @context
 *      This function shall not be called in an interrupt context.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      Yes
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       block_num      Block number into which metadata information
 *                                 should be written.
 * @param[in, out]  metadata       Pointer to opaque metadata.
 * @param[in]       block_offset   Pointer to the block offset value.
 * @param[in]       block_size     Pointer to the block size value.
 * @param[in]       block_flags    Pointer to the block flags value.
 * @param[in]       block_hash     Pointer to the block xxHash value.
 *
 * @retval QZ_OK                   Function executed successfully.
 * @retval QZ_FAIL                 Function did not succeed.
 * @retval QZ_PARAMS               Metadata is NULL.
 * @retval QZ_OUT_OF_RANGE         block_num specified is out of range.
 *
 * @pre
 *      None
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
QATZIP_API int qzMetadataBlockWrite(uint32_t block_num,
                                    QzMetadataBlob_T *metadata,
                                    uint32_t *block_offset,
                                    uint32_t *block_size,
                                    uint32_t *block_flags,
                                    uint32_t *block_hash);

#ifdef __cplusplus
}
#endif

#endif
