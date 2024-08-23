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

#ifndef _UTILS_QZIP_H
#define _UTILS_QZIP_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <fcntl.h>
#include <qz_utils.h>
#include <qatzip.h> /* new QATzip interface */
#ifdef HAVE_QAT_HEADERS
#include <qat/cpa_dc.h>
#else
#include <cpa_dc.h>
#endif
#include <pthread.h>
#include <zlib.h>
#include <libgen.h>


/* qzip version */
#define QZIP_VERSION "1.2.0"

/* field offset in signature header */
#define SIGNATUREHEADER_OFFSET_BASE                  8
#define SIGNATUREHEADER_OFFSET_NEXTHEADER_OFFSET     4
#define SIGNATUREHEADER_OFFSET_NEXTHEADER_SIZE      12
#define SIGNATUREHEADER_OFFSET_NEXTHEADER_CRC       20

/* resolving status */
#define RESOLVE_STATUS_IN_HEADER                   0x0001
#define RESOLVE_STATUS_IN_ARCHIVE_PROPERTIES       0x0002
#define RESOLVE_STATUS_IN_STREAMSINFO              0x0004
#define RESOLVE_STATUS_IN_FILESINFO                0x0008
#define RESOLVE_STATUS_IN_PACKINFO                 0x0010
#define RESOLVE_STATUS_IN_CODERSINFO               0x0020
#define RESOLVE_STATUS_IN_SUBSTREAMSINFO           0x0040

/* definitions of 7z archive tags */
#define PROPERTY_ID_END                            0x00
#define PROPERTY_ID_HEADER                         0x01
#define PROPERTY_ID_ARCHIVE_PROPERTIES             0x02
#define PROPERTY_ID_ADDITIONAL_STREAMSINFO         0x03
#define PROPERTY_ID_MAIN_STREAMSINFO               0x04
#define PROPERTY_ID_FILESINFO                      0x05
#define PROPERTY_ID_PACKINFO                       0x06
#define PROPERTY_ID_UNPACKINFO                     0x07
#define PROPERTY_ID_SUBSTREAMSINFO                 0x08
#define PROPERTY_ID_SIZE                           0x09
#define PROPERTY_ID_CRC                            0x0a
#define PROPERTY_ID_FOLDER                         0x0b
#define PROPERTY_ID_CODERS_UNPACK_SIZE             0x0c
#define PROPERTY_ID_NUM_UNPACK_STREAM              0x0d
#define PROPERTY_ID_EMPTY_STREAM                   0x0e
#define PROPERTY_ID_EMPTY_FILE                     0x0f
#define PROPERTY_ID_ANTI                           0x10
#define PROPERTY_ID_NAME                           0x11
#define PROPERTY_ID_CTIME                          0x12
#define PROPERTY_ID_ATIME                          0x13
#define PROPERTY_ID_MTIME                          0x14
/* Support windows(low 16 bit) and unix(high 16 bit) */
#define PROPERTY_ID_ATTRIBUTES                     0x15
#define PROPERTY_ID_COMMENT                        0x16
#define PROPERTY_ID_ENCODED_HEADER                 0x17
#define PROPERTY_ID_STARTPOS                       0x18
#define PROPERTY_ID_DUMMY                          0x19
#define PROPERTY_CONTENT_DUMMY                     0x00

#define FLAG_ATTR_DEFINED_SET                      0x01
#define FLAG_ATTR_DEFINED_UNSET                    0x00
#define FLAG_ATTR_EXTERNAL_UNSET                   0x00

/* 7z format version */
#define G_7ZHEADER_MAJOR_VERSION                   0x00
#define G_7ZHEADER_MINOR_VERSION                   0x04

/* default size for allocating memory for one node */
#define QZ_DIRLIST_DEFAULT_NUM_PER_NODE            100
#define QZ_FILELIST_DEFAULT_NUM_PER_NODE           1000

/* archiveproperties develop ID */
#define QZ7Z_DEVELOP_PREFIX             0x3ful
#define QZ7Z_DEVELOP_ID                 ('Q'*1ul<<32|'A'<<24|'T'<<16|'7'<<8|'z')
#define QZ7Z_DEVELOP_SUBID              0x0a01ul


/* return codes from qzip */
#define OK      0
#define ERROR   1

/* internal return codes for functions that implement 7z format */
#define QZ7Z_OK                       OK
#define QZ7Z_ERR_INVALID_SIZE         -200
#define QZ7Z_ERR_OPEN                 -201
#define QZ7Z_ERR_OOM                  -202
#define QZ7Z_ERR_CONCAT_FILE          -203
#define QZ7Z_ERR_STAT                 -204
#define QZ7Z_ERR_IOCTL                -205
#define QZ7Z_ERR_END_HEADER           -206
#define QZ7Z_ERR_NULL_INPUT_LIST      -207
#define QZ7Z_ERR_REMOVE               -208
#define QZ7Z_ERR_RESOLVE_END_HEADER   -209
#define QZ7Z_ERR_NOT_EXPECTED_CHAR    -210
#define QZ7Z_ERR_GETCWD               -211
#define QZ7Z_ERR_READ_EOF             -212
#define QZ7Z_ERR_READ_LESS            -213
#define QZ7Z_ERR_WRITE_EOF            -214
#define QZ7Z_ERR_WRITE_LESS           -215
#define QZ7Z_ERR_MKDIR                -216
#define QZ7Z_ERR_CHDIR                -217
#define QZ7Z_ERR_CREATE_TEMP          -218
#define QZ7Z_ERR_HEADER_CRC           -219
#define QZ7Z_ERR_TIMES                -220
#define QZ7Z_ERR_SIG_HEADER_BROKEN    -221
#define QZ7Z_ERR_READLINK             -222
#define QZ7Z_ERR_SIG_HEADER           -223
#define QZ7Z_ERR_RESOLVE_SUBSTREAMS   -224
#define QZ7Z_ERR_UNEXPECTED           -225

#define MAX_PATH_LEN   1024 /* max pathname length */
#define SUFFIX_GZ      ".gz"
#define SUFFIX_7Z      ".7z"
#define SUFFIX_LZ4     ".lz4"
#define SUFFIX_LZ4S    ".lz4s"

#define QZIP_GET_LOWER_32BITS(v)  ((v) & 0xFFFFFFFF)

typedef enum QzSuffix_E {
    E_SUFFIX_GZ,
    E_SUFFIX_7Z,
    E_SUFFIX_LZ4,
    E_SUFFIX_LZ4S,
    E_SUFFIX_UNKNOWN = 999
} QzSuffix_T;

typedef enum QzSuffixCheckStatus_E {
    E_CHECK_SUFFIX_OK,
    E_CHECK_SUFFIX_UNSUPPORT,
    E_CHECK_SUFFIX_FORMAT_UNMATCH
} QzSuffixCheckStatus_T;

#define SRC_BUFF_LEN         (512 * 1024 * 1024)

typedef enum QzipDataFormat_E {
    QZIP_DEFLATE_4B = 0,
    /**< Data is in raw deflate format with 4 byte header */
    QZIP_DEFLATE_GZIP,
    /**< Data is in deflate wrapped by GZip header and footer */
    QZIP_DEFLATE_GZIP_EXT,
    /**< Data is in deflate wrapped by GZip extended header and footer */
    QZIP_DEFLATE_RAW,
    /**< Data is in raw deflate format */
    QZIP_LZ4_FH,
    /**< Data is in LZ4 format with frame headers */
    QZIP_LZ4S_BK,
    /**< Data is in LZ4s format with block headers */
} QzipDataFormat_T;

typedef struct QzipParams_S {
    QzHuffmanHdr_T huffman_hdr;
    QzDirection_T direction;
    QzipDataFormat_T data_fmt;
    unsigned int comp_lvl;
    unsigned char comp_algorithm;
    unsigned char force;
    unsigned char keep;
    unsigned int hw_buff_sz;
    unsigned int polling_mode;
    unsigned int recursive_mode;
    unsigned int req_cnt_thrshold;
    char *output_filename;
} QzipParams_T;

#define QZ7Z_PROPERTY_ID_INTEL7Z_1001   ((QZ7Z_DEVELOP_PREFIX << 56) | \
                                        (QZ7Z_DEVELOP_ID << 16) | \
                                        (QZ7Z_DEVELOP_SUBID))

/* check allocated memory */
#define CHECK_ALLOC_RETURN_VALUE(p) \
    if (NULL == p) {\
        printf("%s:%d oom\n", __FILE__, __LINE__); \
        exit(-QZ7Z_ERR_OOM);\
    }

/* check fread return */
#define CHECK_FREAD_RETURN(ret, n) if((ret) != (n)) { \
    if (feof(fp)) { \
        fprintf(stderr, "fread reach EOF.\n"); \
            exit(-QZ7Z_ERR_READ_EOF); \
        } else { \
            fprintf(stderr, "fread errors.\n"); \
            exit(-QZ7Z_ERR_READ_LESS); \
        } \
    }

/* check fwrite return */
#define CHECK_FWRITE_RETURN(ret, n) CHECK_FWRITE_RETURN_FP(fp, ret, n)
#define CHECK_FWRITE_RETURN_FP(fp, ret, n) if((ret) != (n)) { \
    if (feof((fp))) { \
        fprintf(stderr, "fwrite reach EOF.\n"); \
            exit(-QZ7Z_ERR_WRITE_EOF); \
        } else { \
            fprintf(stderr, "fwrite errors.\n"); \
            exit(-QZ7Z_ERR_WRITE_LESS); \
        } \
    }

typedef struct RunTimeList_S {
    struct timeval time_s;
    struct timeval time_e;
    struct RunTimeList_S *next;
} RunTimeList_T;

/* Windows FILETIME structure
 * contains a 64-bit value representing the number of 100-nanosecond intervals
 * since January 1, 1601 (UTC).
 */

/* 1601 to 1970 is 369 years plus 89 leap days */
#define  NUM_DAYS    (134774UL)

/* time in seconds from 1601 Jan 1 to 1970 Jan 1 */
#define  DELTA_TIME  (NUM_DAYS * (24 * 60 * 60UL))

#define  NANO_SEC     1000000000UL
#define  TICKS_PER_SEC  10000000UL


typedef struct FILETIME {
    uint32_t low;
    uint32_t high;
} FILETIME_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     Qatzip linked list node structure
 *
 * @description
 *     Qatzip linked list node structure
 *
 ******************************************************************************/
typedef struct QzListNode_S {
    uint32_t              num;      // number of allocated region per node
    uint32_t              used;     // used space of element
    void                  **items;
    struct QzListNode_S   *next;
} QzListNode_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     Qatzip linked list head structure
 *
 * @description
 *     Qatzip linked list head structure
 *
 ******************************************************************************/
typedef struct QzListHead_S {
    uint32_t          total;    // total elements
    uint32_t          num;      // number of allocated region per node
    QzListNode_T      *next;
} QzListHead_T;


/**
 ******************************************************************************
 * @ingroup qatZip
 *     A file or a directory compressed by qatzip
 *
 * @description
 *     This structure contains a file or directory and it's attributes
 *
 ******************************************************************************/
typedef struct Qz7zFileItem_S {
    char
    *fileName;   /* dynamic allocated memory for filename(pathname) */
    unsigned char   isDir;       /* 1byte */
    unsigned char   isEmpty;     /* 1byte */  /* is empty file */
    unsigned char   isSymLink;   /* 1byte */  /* is symbol link */
    unsigned char   isAnti;      /* 1byte */  /* is anti file(on windows) */
    uint32_t        baseNameLength; /* base pathname length */
    uint32_t        nameLength;  /* memory allocated length */
    size_t          size;        /* for file it's file's length*/
    uint32_t        crc;
    uint32_t        attribute;
    uint64_t        atime;
    uint32_t        atime_nano;
    uint64_t        mtime;
    uint32_t        mtime_nano;
} Qz7zFileItem_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z signature header
 *
 * @description
 *     This structure contains a 7z signature header
 *
 ******************************************************************************/
typedef struct Qz7zSignatureHeader_S {
    unsigned char   signature[6]; /* {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C}*/
    unsigned char   majorVersion; /* 0x00 */
    unsigned char   minorVersion; /* 0x04 */
    uint32_t        startHeaderCRC;
    uint64_t        nextHeaderOffset;
    uint64_t        nextHeaderSize;
    uint32_t        nextHeaderCRC;
} Qz7zSignatureHeader_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Coder Info
 *
 * @description
 *     This structure presents Coder info
 *
 ******************************************************************************/
typedef struct Qz7zDigest_S {
    unsigned char   allAreDefined;
    uint64_t        numStreams;
    uint64_t        numDefined;
    uint32_t        *crc; /* array: CRC[NumDefined] */
} Qz7zDigest_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Pack Info structure
 *
 * @description
 *     This structure presents a 7z pack info
 *
 ******************************************************************************/
typedef struct Qz7zPackInfo_S {
    uint64_t        PackPos;
    uint64_t        NumPackStreams;
    uint64_t        *PackSize;           /* PackSize[NumPackStreams] */
    Qz7zDigest_T    *PackStreamDigests;  /* not used */
} Qz7zPackInfo_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     First byte of 7z Coder Info structure
 *
 * @description
 *     This structure contains some attributes of a coder
 *
 ******************************************************************************/
typedef union Qz7zCoderFirstByte_S {
    struct {
        unsigned    CodecIdSize     : 4;
        unsigned    IsComplexCoder  : 1;
        unsigned    HasAttributes   : 1;
        unsigned    Reserved        : 1;
        unsigned    MoreAlterMethods: 1; /* not used: 0 for ever */
    } st;
    unsigned char uc;
} Qz7zCoderFirstByte_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Coder Info list
 *
 * @description
 *     This structure is Coder info list node
 *
 ******************************************************************************/
typedef struct Qz7zCoder_S {
    Qz7zCoderFirstByte_T     coderFirstByte;
    unsigned char            *codecID ;     /* CodecID[CodecIdSize] */
    uint64_t                 numInStreams;  /* if it is complex coder */
    uint64_t                 numOutStreams; /* if it is complex coder */
    uint64_t                 propertySize;  /* if there are attributes */
    unsigned char            *properties;  /* array: Properties[PropertiSize]
                                              if there are attributes */
    struct Qz7zCoder_S       *next;
} Qz7zCoder_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Folder Info
 *
 * @description
 *     This structure is Folder info
 *
 ******************************************************************************/
typedef struct Qz7zFolderInfo_S {
    uint64_t             numCoders;
    Qz7zCoder_T          *coder_list;
    QzListHead_T         *items;     /* fileitems list */
    uint64_t             numBindPairs;
    uint64_t             *inIndex;    /* array: InIndex[NumBindPairs] */
    uint64_t             *outIndex;   /* array: InIndex[NumBindPairs] */
    uint64_t             numPackedStreams;
    uint64_t             *index;      /* array: Index[NumPackedStreams] */
} Qz7zFolderInfo_T;

typedef struct Qz7zFolderLoc_S {
    QzListNode_T         *node;
    unsigned int         index;
    size_t               pos;
} Qz7zFolderLoc_T;

typedef struct Qz7zFolderHandle_S {
    Qz7zFolderInfo_T     *folder;
    Qz7zFolderLoc_T      *loc;
} Qz7zFolderHandle_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Coder Info
 *
 * @description
 *     This structure presents Coder info
 *     The folders points to an array of all the folders
 *     This part contains all the folders info here, in one struct variable.
 *
 ******************************************************************************/
typedef struct Qz7zCodersInfo_S {
    uint64_t             numFolders;
    Qz7zFolderInfo_T     *folders;      /* array  folders[numFolders] */
    uint64_t             dataStreamIndex;
    uint64_t             *unPackSize;   /* array: unPackSize[numFolders] */
    Qz7zDigest_T         *unPackDigests; /* not used */
} Qz7zCodersInfo_T ;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Substreams Info
 *
 * @description
 *     This structure presents Substreams info
 *
 ******************************************************************************/
typedef struct Qz7zSubstreamsInfo_S {
    uint64_t             numFolders;
    uint64_t
    *numUnPackStreams;          /* NumUnPackStreams[numFolders] */
    uint64_t
    *unPackSize;                /* unPackSize[AllNumFiles - 1], AllNumFiles is
                                   sum of the array NumUnPackStreamsInFolders */
    Qz7zDigest_T
    *digests;                   /* points to digests structure with all
                                   non-empty files num*/
} Qz7zSubstreamsInfo_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Streams Info structure
 *
 * @description
 *     This structure presents a 7z streams info
 *
 ******************************************************************************/
typedef struct Qz7zStreamsInfo_S {
    Qz7zPackInfo_T        *packInfo;
    Qz7zCodersInfo_T      *codersInfo;
    Qz7zSubstreamsInfo_T  *substreamsInfo;
} Qz7zStreamsInfo_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Files Info structure
 *
 * @description
 *     This structure presents a 7z files info. It is used for compressing
 *
 ******************************************************************************/
typedef struct Qz7zFilesInfo_S {
    uint64_t               num;
    QzListHead_T           *head[2];
} Qz7zFilesInfo_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Files Info structure
 *
 * @description
 *     This structure presents a 7z files info. It is used for decompressing
 *
 ******************************************************************************/
typedef struct Qz7zFilesInfo_DEC_S {
    uint64_t               dir_num;
    uint64_t               file_num;
    Qz7zFileItem_T         *items;
} Qz7zFilesInfo_Dec_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Files ArchiveProperties structure
 *
 * @description
 *     This structure presents a 7z Archive Properties Info, if there are more
 *     than one properties, the `next` points to the next property
 *
 ******************************************************************************/
typedef struct Qz7zArchiveProperty_S {
    uint64_t                       id;
    uint64_t                       size;
    unsigned char                  *data;
    struct Qz7zArchiveProperty_S   *next;
} Qz7zArchiveProperty_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z End header structure
 *
 * @description
 *     This structure contains a 7z end header
 *
 ******************************************************************************/
typedef struct Qz7zEndHeader_S {
    Qz7zArchiveProperty_T  *propertyInfo;
    Qz7zStreamsInfo_T      *streamsInfo;
    Qz7zFilesInfo_T        *filesInfo;
    Qz7zFilesInfo_Dec_T    *filesInfo_Dec;
} Qz7zEndHeader_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Category
 *
 * @description
 *     This structure presents a 7z category
 *
 ******************************************************************************/
typedef struct QzCatagory_S {
    unsigned char    cat_id;
    const char      *cat_name;
    QzListHead_T    *cat_files;
} QzCatagory_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     7z Category table
 *
 * @description
 *     This structure presents all categories
 *
 ******************************************************************************/
typedef struct QzCatagoryTable_S {
    unsigned int     cat_num;
    QzCatagory_T     *catas; /* array: catas[cat_num] */
} QzCatagoryTable_T;

/**
 ******************************************************************************
 * @ingroup qatZip
 *     Qatzip items list structure
 *
 * @description
 *     the structure is used for hold input arguments
 *     items[0] is the list of all directory and empty file
 *     items[1] is the list of all non-empty file
 *
 ******************************************************************************/
typedef struct Qz7zItemList_S {
    QzListHead_T       *items[2];
    QzCatagoryTable_T  *table;
} Qz7zItemList_T;

/* create a list return list head */
QzListHead_T *qzListCreate(int num_per_node);

/*  Add one element's address to the list */
void qzListAdd(QzListHead_T *head, void **node);

/* Get an element's address from a list */
void *qzListGet(QzListHead_T *head, int index);

/* Free all allocated memory pointed by head */
void qzListDestroy(QzListHead_T *head);

/* create the file items list */
Qz7zFileItem_T *fileItemCreate(char *pfilename);

/* destroy the items list */
void itemListDestroy(Qz7zItemList_T *p);

/* process the cmdline inputs */
Qz7zItemList_T *itemListCreate(int n, char **files);


/*
 * resolve functions
 */
Qz7zSignatureHeader_T *resolveSignatureHeader(FILE *fp);
Qz7zArchiveProperty_T *resolveArchiveProperties(FILE *fp);
Qz7zPackInfo_T *resolvePackInfo(FILE *fp);
Qz7zCodersInfo_T *resolveCodersInfo(FILE *fp);
Qz7zSubstreamsInfo_T *resolveSubstreamsInfo(int n_folder, FILE *fp);
Qz7zFilesInfo_Dec_T *resolveFilesInfo(FILE *fp);
Qz7zStreamsInfo_T *resolveMainStreamsInfo(FILE *fp);
Qz7zEndHeader_T *resolveEndHeader(FILE *fp, Qz7zSignatureHeader_T *sheader);

/* create category list */
QzCatagoryTable_T *createCatagoryList(void);
int scanFilesIntoCatagory(Qz7zItemList_T *the_list);
/*
 * generate functions
 */
Qz7zSignatureHeader_T *generateSignatureHeader(void);
Qz7zArchiveProperty_T *generatePropertyInfo(void);
Qz7zPackInfo_T *generatePackInfo(Qz7zItemList_T *the_list,
                                 size_t compressed_size);
Qz7zFolderInfo_T *generateFolderInfo(Qz7zItemList_T *the_list, int n_folders);
Qz7zCodersInfo_T *generateCodersInfo(Qz7zItemList_T *the_list);
Qz7zDigest_T *generateDigestInfo(QzListHead_T *head);
Qz7zSubstreamsInfo_T *generateSubstreamsInfo(Qz7zItemList_T *the_list);
Qz7zFilesInfo_T *generateFilesInfo(Qz7zItemList_T *the_list);
Qz7zEndHeader_T *generateEndHeader(Qz7zItemList_T *the_list,
                                   size_t compressed_size);

/*
 * write function
 */
int writeSignatureHeader(Qz7zSignatureHeader_T *header, FILE *fp);
size_t writeArchiveProperties(Qz7zArchiveProperty_T *property, FILE *fp,
                              uint32_t *crc);
size_t writePackInfo(Qz7zPackInfo_T *pack, FILE *fp, uint32_t *crc);
size_t writeFolder(Qz7zFolderInfo_T *folder, FILE *fp, uint32_t *crc);
size_t writeCodersInfo(Qz7zCodersInfo_T *coders, FILE *fp, uint32_t *crc);
size_t writeDigestInfo(Qz7zDigest_T *digest, FILE *fp, uint32_t *crc);
size_t writeStreamsInfo(Qz7zStreamsInfo_T *streams, FILE *fp, uint32_t *crc);
size_t writeFilesInfo(Qz7zFilesInfo_T *files, FILE *fp, uint32_t *crc);
size_t writeSubstreamsInfo(Qz7zSubstreamsInfo_T *substreams, FILE *fp,
                           uint32_t *crc);
size_t writeEndHeader(Qz7zEndHeader_T *header, FILE *fp, uint32_t *crc);

/*
 * free functions
 */
void freePropertyInfo(Qz7zArchiveProperty_T *info);
void freePackInfo(Qz7zPackInfo_T *info);
void freeCodersInfo(Qz7zCodersInfo_T *info);
void freeSubstreamsInfo(Qz7zSubstreamsInfo_T *info);
void freeStreamsInfo(Qz7zStreamsInfo_T *info);
void freeFilesInfo(Qz7zFilesInfo_T *info);
void freeFilesDecInfo(Qz7zFilesInfo_Dec_T *info);
void freeEndHeader(Qz7zEndHeader_T *eheader, int is_compress);


/* the main API for compress into 7z format */
int qz7zCompress(QzSession_T *sess, Qz7zItemList_T *the_list,
                 const char *out_name);

/* the main API for decompress a 7z file */
int qz7zDecompress(QzSession_T *sess, const char *archive);


/*
 * UINT64 conversion functions
 */
/* conversion from real uint64_t to UINT64 */
int getExtraByteNum(uint64_t n);
int getUint64Bytes(uint64_t n, unsigned char *u64);

/* conversion from UINT64 to uint64_t */
int getExtraByteNum2(uint8_t first);
uint64_t getU64FromBytes(FILE *fp);

#ifdef QZ7Z_DEBUG
void printSignatureHeader(Qz7zSignatureHeader_T *sheader);
void printEndHeader(Qz7zEndHeader_T *eheader);
#endif

/* create the directory in path of newdir */
int createDir(const char *newdir, int back);

/* delete source files represented by the list */
int deleteSourceFile(Qz7zItemList_T *the_list);

/* check whether the file is 7z archive */
int check7zArchive(const char *archive);

/* check whether the filename is directory */
int checkDirectory(const char *filename);

void freeTimeList(RunTimeList_T *time_list);
void displayStats(RunTimeList_T *time_list,
                  off_t insize, off_t outsize, int is_compress);

void tryHelp(void);
void help(void);
void version(void);
char *qzipBaseName(char *fname);
QzSuffix_T getSuffix(const char *filename);
bool hasSuffix(const char *fname);
QzSuffixCheckStatus_T checkSuffix(QzSuffix_T suffix, int is_format_set);
int makeOutName(const char *in_name, const char *out_name,
                char *oname, int is_compress);

/* Makes a complete file system path by adding a file name to the path of its
 * parent directory. */
void mkPath(char *path, const char *dirpath, char *file);



/*
 * internal api functions
 */
int qatzipSetup(QzSession_T *sess, QzipParams_T *params);
int qatzipClose(QzSession_T *sess);

void processFile(QzSession_T *sess, const char *in_name,
                 const char *out_name, int is_compress);

int doProcessBuffer(QzSession_T *sess,
                    unsigned char *src, unsigned int *src_len,
                    unsigned char *dst, unsigned int dst_len,
                    RunTimeList_T *time_list, FILE *dst_file,
                    off_t *dst_file_size, int is_compress);

void doProcessFile(QzSession_T *sess, const char *src_file_name,
                   const char *dst_file_name, int is_compress);

void processDir(QzSession_T *sess, const char *in_name,
                const char *out_name, int is_compress);

void processStream(QzSession_T *sess, FILE *src_file, FILE *dst_file,
                   int is_compress);

int doCompressFile(QzSession_T *sess, Qz7zItemList_T *list,
                   const char *dst_file_name);

int doDecompressFile(QzSession_T *sess, const char *src_file_name);

/*
 * extern declaration
 */
extern char const *const g_license_msg[2];
extern char *g_program_name;
extern int g_decompress;        /* g_decompress (-d) */
extern int g_keep;                     /* keep (don't delete) input files */
extern QzSession_T g_sess;
extern QzipParams_T g_params_th;
/* Estimate maximum data expansion after decompression */
extern const unsigned int g_bufsz_expansion_ratio[4];
/* Command line options*/
extern char const g_short_opts[];
extern const struct option g_long_opts[];
extern const unsigned int USDM_ALLOC_MAX_SZ;

#endif
