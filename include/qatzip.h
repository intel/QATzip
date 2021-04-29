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
#include <stdbool.h>

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
#define QATZIP_API_VERSION_NUM_MAJOR (1)

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

#ifdef ERR_INJECTION
#include "cf.h"
#endif

/**
 *****************************************************************************
 *
 *  This API provides access to underlying compression functions in QAT
 *  hardware. The API supports an implementation that provides compression
 *  service in software if not all of the required resources are available
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

#define QZ_MAX_ALGORITHMS  ((int)255)
#define QZ_DEFLATE         ((unsigned char)8)
#define MIN(a,b) (((a)<(b))?(a):(b))
#ifdef __linux__
#define QZ_MEMCPY(dest, src, dest_sz, src_sz) \
        memcpy((void *)(dest), (void *) (src), MIN((size_t)dest_sz, \
               (size_t)src_sz))
#endif
#ifdef _WIN64
#define QZ_MEMCPY(dest, src, dest_sz, src_sz) \
        memcpy_s((void *)(dest), dest_sz, (void *) (src), MIN(dest_sz, src_sz))
#endif

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
    /**< Compression level 1 to MAX_COMP_LEVEL */
    unsigned char comp_algorithm;
    /**< Compress/decompression algorithms */
    unsigned int max_forks;
    /**< Maximum forks permitted in the current thread */
    /**< 0 means no forking permitted */
    unsigned char sw_backup;
    /**< 0 means no sw backup, 1 means sw backup */
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
    FallbackError *fbError;
    FallbackError *fbErrorCurr;
    /* Linked list for simulated errors from HW */
#endif
    bool is_busy_polling;
    /**< true means busy polling */
} QzSessionParams_T;

#define QZ_HUFF_HDR_DEFAULT          QZ_DYNAMIC_HDR
#define QZ_DIRECTION_DEFAULT         QZ_DIR_BOTH
#define QZ_DATA_FORMAT_DEFAULT       QZ_DEFLATE_GZIP_EXT
#define QZ_COMP_LEVEL_DEFAULT        1
#define QZ_COMP_ALGOL_DEFAULT        QZ_DEFLATE
#define QZ_POLL_SLEEP_DEFAULT        10
#define QZ_MAX_FORK_DEFAULT          3
#define QZ_SW_BACKUP_DEFAULT         1
#define QZ_HW_BUFF_SZ                (64*1024)
#define QZ_HW_BUFF_MIN_SZ            (1*1024)
#define QZ_HW_BUFF_MAX_SZ            (512*1024)
#define QZ_STRM_BUFF_SZ_DEFAULT      QZ_HW_BUFF_SZ
#define QZ_STRM_BUFF_MIN_SZ          (1*1024)
#define QZ_STRM_BUFF_MAX_SZ          (2*1024*1024 - 5*1024)
#define QZ_COMP_THRESHOLD_DEFAULT    1024
#define QZ_COMP_THRESHOLD_MINIMUM    128
#define QZ_REQ_THRESHOLD_MINIMUM     1
#define QZ_REQ_THRESHOLD_MAXIMUM     NUM_BUFF
#define QZ_REQ_THRESHOLD_DEFAULT     QZ_REQ_THRESHOLD_MAXIMUM
#define QZ_WAIT_CNT_THRESHOLD_DEFAULT 8
#define QZ_DEFLATE_COMP_LVL_MINIMUM   (1)

#include <cpa_dc.h>
#if (CPA_DC_API_VERSION_NUM_MAJOR >= 3) && (CPA_DC_API_VERSION_NUM_MINOR >= 0)
#define QZ_DEFLATE_COMP_LVL_MAXIMUM   (12)
#else
#define QZ_DEFLATE_COMP_LVL_MAXIMUM   (9)
#endif

#define QZ_PERIODICAL_POLLING         (false)
#define QZ_BUSY_POLLING               (true)
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
    unsigned char qat_service_stated;
    /**< Check if the QAT service is running properly on at least one device */
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
 *                                 (pointer to opaque instance and session data.)
 * @param[in]       sw_backup      0 for no sw backup, 1 for sw backup
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
 *    If *sess includes an existing hardware or software session, then this
 *    session will be torn down before a new one is attempted.
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
 * @retval QZ_PARAMS               *sess is NULL or member of params is invalid
 * @retval QZ_NOSW_NO_HW           No hardware and no sw session being
 *                                 established
 * @retval QZ_NOSW_NO_MDRV         No memory driver. No software session
 *                                 established
 * @retval QZ_NOSW_NO_INST_ATTACH  No instance available
 *                                 No software session established
 * @retval QZ_NO_LOW_MEM           Not enough pinned memory available
 *                                 No software session established
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
QATZIP_API int qzCompress(QzSession_T *sess, const unsigned char *src,
                          unsigned int *src_len, unsigned char *dest,
                          unsigned int *dest_len, unsigned int last);

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
 *    buffer and put CRC32 checksum for compressed input data in user provided
 *    buffer *crc.
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
 * @param[in,out]   crc      Point to CRC32 checksum buffer
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
QATZIP_API int qzCompressCrc(QzSession_T *sess, const unsigned char *src,
                             unsigned int *src_len, unsigned char *dest,
                             unsigned int *dest_len, unsigned int last,
                             unsigned long *crc);

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
 *      This function retrieves the status of QAT in the platform.
 *    The status structure will be filled in as follows:
 *    qat_hw_count         Number of discovered QAT devices on PCU bus
 *    qat_service_stated   1 if qzInit has been successfully run, 0 otherwise
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

#ifdef __cplusplus
}
#endif

#endif
