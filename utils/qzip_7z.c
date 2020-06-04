/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2020 Intel Corporation. All rights reserved.
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
#include "qzip.h"

#define OK 0
#define ERROR 1

extern unsigned int g_bufsz_expansion_ratio[4];
extern int g_keep;

static const unsigned char g_header_signature[] = {
    '7', 'z', 0xBC, 0xAF, 0x27, 0x1C
};

static const char g_deflate_codecId[] = {
    0x04, 0x01, 0x08
};

static const char g_property_data[] = {
    'Q', 'A', 'T'
};

static uint64_t const extra_byte_boundary[] = {
    0x0,
    0x7f,
    0x3fff,
    0x1fffff,
    0xfffffff,
    0x7ffffffff,
    0x3ffffffffff,
    0x1ffffffffffff,
    0xffffffffffffff,
    0xffffffffffffffff
};

static uint8_t const first_byte_table[] = {
    0,
    0x80/* 1000 0000 */,
    0xc0/* 1100 0000 */,
    0xe0/* 1110 0000 */,
    0xf0/* 1111 0000 */,
    0xf8/* 1111 1000 */,
    0xfc/* 1111 1100 */,
    0xfe/* 1111 1110 */,
    0xff/* 1111 1111 */
};

FILETIME_T  unixtimeToFiletime(unsigned long t, uint32_t nsec)
{
    FILETIME_T  ft;
    unsigned long long secs = t * TICKS_PER_SEC + DELTA_TIME * TICKS_PER_SEC +
                              nsec / 100;
    ft.low  = (uint32_t)secs;
    ft.high = (uint32_t)(secs >> 32);
    return ft;
}

time_t filetimeToUnixtime(FILETIME_T ft)
{
    time_t  t;
    uint64_t ti;
    ti = (uint64_t)ft.high << 32;
    ti += ft.low;
    t = ti / TICKS_PER_SEC  - DELTA_TIME;
    return t;
}

#define writeByte(b, fp, crc) writeTag(b, fp, crc)

static size_t  writeTag(unsigned char tag, FILE *fp, uint32_t *crc)
{
    size_t  n;
    n = fwrite(&tag, sizeof(unsigned char), 1, fp);
    CHECK_FWRITE_RETURN(n, 1)
    if (crc) {
        *crc = crc32(*crc, &tag, 1);
    }
    return n;
}

static size_t writeTime(unsigned int t, FILE *fp, uint32_t *crc)
{
    size_t n;
    n = fwrite(&t, sizeof(unsigned int), 1, fp);
    CHECK_FWRITE_RETURN(n, 1)
    if (crc) {
        *crc = crc32(*crc, (unsigned char *)&t, 4);
    }
    return 4;
}

static size_t writeNumber(uint64_t u64, FILE *fp, uint32_t *crc)
{
    uint64_t         size;
    int              n;
    unsigned char    u64_bytes[9];
    n = getUint64Bytes(u64, u64_bytes);
    size = fwrite(u64_bytes, sizeof(unsigned char), n, fp);
    CHECK_FWRITE_RETURN(size, n)
    if (crc) {
        *crc = crc32(*crc, u64_bytes, n);
    }
    return size;
}

static unsigned char readByte(FILE *fp)
{
    unsigned char     c;
    int               n;
    n = fread(&c, 1, 1, fp);
    CHECK_FREAD_RETURN(n, 1)
    return c;
}

static unsigned char *readNByte(int n, FILE *fp)
{
    unsigned char    *p;
    int              size;
    p = (unsigned char *)malloc(n * sizeof(unsigned char));
    CHECK_ALLOC_RETURN_VALUE(p)

    size = fread(p, 1, n, fp);
    CHECK_FREAD_RETURN(size, n)
    return p;
}

static void skipNByte(int n, FILE *fp)
{
    fseek(fp, n, SEEK_CUR);
}

static uint32_t readCRC(FILE *fp)
{
    uint32_t     crc;
    int          n;
    n = fread(&crc, sizeof(uint32_t), 1, fp);
    CHECK_FREAD_RETURN(n, 1)
    return crc;
}

static int getExtraByteNum2(uint8_t first)
{
    int i;
    if (first == 0xff) return 8;

    for (i = 0; i < sizeof(first_byte_table); ++i) {
        if (first >= first_byte_table[i] && first < first_byte_table[i + 1])
            break;
    }
    return i;
}

static int getExtraByteNum(uint64_t n)
{
    int i;
    int boundary_len = sizeof(extra_byte_boundary)
                       / sizeof(extra_byte_boundary[0]);

    if (n == 0) return 0;

    for (i = 0; i < boundary_len; ++i) {
        if (n > extra_byte_boundary[i])
            continue;
        break;
    }
    return i - 1;
}

/*
 * from UINT64 to uint64_t
 */
uint64_t getU64FromBytes(FILE *fp)
{
    int              i;
    int              k;
    int              extra;
    uint64_t         ret;
    unsigned char    c;
    uint8_t          p = 0;
    unsigned char    buf[8] = {0};

    c = readByte(fp);
    extra = getExtraByteNum2(c);
    for (i = 0, k = 7; i < extra; ++i, --k) {
        p += 1 << k;
    }
    for (i = 0; i < extra; ++i) {
        buf[i] = readByte(fp);
    }
    buf[i] = c & ~p;

    memcpy(&ret, buf, sizeof(buf));
    return ret;
}

/**
 * get the number n's UINT64 form
 * n: the number
 * p: the bytes
 * return: total bytes
 */
int getUint64Bytes(uint64_t n, unsigned char *p)
{
    int             i;
    int             extra = getExtraByteNum(n);
    uint64_t        number = n;
    unsigned char   first_byte;

    for (i = 0; i < extra; ++i) {
        number /= 0x100;
    }

    first_byte = number | first_byte_table[extra];
    number = n;

    p[0] = first_byte;
    for (i = 0; i < extra; ++i) {
        p[i + 1] = number % 0x100;
        number /= 0x100;
    }
    return extra + 1;
}

/**
 * this means no catagory, every files are in one folder
 * the folder number/packed streams number is equal to
 * the number of catagory names
 * default is the last one, DO NOT delete it, add new
 * catagory names in front of it
 */
static const char *g_catagory_names[] = {
    "default"
};

#ifdef QZ7Z_DEBUG
void printSignatureHeader(Qz7zSignatureHeader_T *sheader)
{
    QZ_DEBUG("-----signature header start-----\n");
    QZ_DEBUG("signature: %c %c %x %x %x %x\n", sheader->signature[0],
             sheader->signature[1],
             sheader->signature[2], sheader->signature[3],
             sheader->signature[4], sheader->signature[5]);
    QZ_DEBUG("major version: %d minor version: %d\n", sheader->majorVersion,
             sheader->minorVersion);

    QZ_DEBUG("nextheaderoffset: %lu\n", sheader->nextHeaderOffset);
    QZ_DEBUG("nextHeaderSize: %lu\n", sheader->nextHeaderSize);
    QZ_DEBUG("nextHeaderCRC: %u\n", sheader->nextHeaderCRC);
    QZ_DEBUG("startHeaderCRC: %u\n", sheader->startHeaderCRC);
    QZ_DEBUG("-----end of signature header-----\n");
}

void printEndHeader(Qz7zEndHeader_T *eheader)
{
    int i;
    int j;
    QZ_DEBUG("-----print end header-------\n");
    if (eheader->propertyInfo) {
        QZ_DEBUG(" ----------ArchiveProperties-------------\n");
        QZ_DEBUG("Develop ID: %lx \n", eheader->propertyInfo->id);
    }

    if (eheader->streamsInfo) {
        QZ_DEBUG(" ----------StreamsInfo ------------------\n");
        QZ_DEBUG(" NumPackStreams: %lu\n",
                 eheader->streamsInfo->packInfo->NumPackStreams);
        QZ_DEBUG(" PackSize: ");
        for (i = 0; i < eheader->streamsInfo->packInfo->NumPackStreams; ++i) {
            QZ_DEBUG("%lu ", eheader->streamsInfo->packInfo->PackSize[i]);
        }

        QZ_DEBUG("\n ----------CodersInfo -------------------\n");
        QZ_DEBUG("    NumFolders: %lu\n",
                 eheader->streamsInfo->codersInfo->numFolders);
        for (i = 0; i < eheader->streamsInfo->codersInfo->numFolders; ++i) {
            QZ_DEBUG(" %lu ", eheader->streamsInfo->codersInfo->unPackSize[i]);
        }

        QZ_DEBUG("\n ----------SubstreamsInfo-------------------\n");
        QZ_DEBUG("    NumUnpackSubstreamsInFolders: \n");
        for (i = 0; i < eheader->streamsInfo->codersInfo->numFolders; ++i) {
            if (eheader->streamsInfo->substreamsInfo->numUnPackStreams) {
                QZ_DEBUG(" %lu ",
                         eheader->streamsInfo->substreamsInfo->
                         numUnPackStreams[i]);
                QZ_DEBUG("\n unpacksize: \n");
                for (j = 0; j < eheader->streamsInfo->substreamsInfo->
                     numUnPackStreams[i]; ++j) {
                    QZ_DEBUG(" %lu ", eheader->streamsInfo->substreamsInfo->
                             unPackSize[j]);
                }
            }
        }

    }

    if (eheader->filesInfo) {
        QZ_DEBUG("\n --------------FilesInfo -------------\n");
    }
}
#endif

static int doCompressBuffer(QzSession_T *sess,
                            unsigned char *src, unsigned int *src_len,
                            unsigned char *dst, unsigned int *dst_len,
                            RunTimeList_T *time_list, FILE *dst_file,
                            off_t *dst_file_size, int last)
{
    int ret = QZ_FAIL;
    unsigned int done = 0;
    unsigned int buf_processed = 0;
    unsigned int buf_remaining = *src_len;
    unsigned int bytes_written;
    unsigned int output_len = 0;
    RunTimeList_T *time_node = time_list;

    while (time_node->next) {
        time_node = time_node->next;
    }

    while (!done) {
        RunTimeList_T *run_time = calloc(1, sizeof(RunTimeList_T));
        CHECK_ALLOC_RETURN_VALUE(run_time)
        run_time->next = NULL;
        time_node->next = run_time;
        time_node = run_time;

        gettimeofday(&run_time->time_s, NULL);

        /* do actual work */
        ret = qzCompress(sess, src, src_len, dst, dst_len, last);
        if (QZ_BUF_ERROR == ret && 0 == *src_len) {
            done = 1;
        }
        QZ_DEBUG("qzCompress returned: src_len=%u  dst_len=%u\n", *src_len,
                 *dst_len);

        if (ret != QZ_OK &&
            ret != QZ_BUF_ERROR &&
            ret != QZ_DATA_ERROR) {
            QZ_ERROR("doCompressBuffer in qzip_7z.c :failed with error: %d\n",
                     ret);
            break;
        }

        gettimeofday(&run_time->time_e, NULL);

        bytes_written = fwrite(dst, 1, *dst_len, dst_file);
        CHECK_FWRITE_RETURN_FP(dst_file, bytes_written, *dst_len)
        *dst_file_size += bytes_written;

        buf_processed += *src_len;
        buf_remaining -= *src_len;
        output_len += *dst_len;
        if (0 == buf_remaining) {
            done = 1;
        }
        src += *src_len;
        QZ_DEBUG("src_len is %u ,buf_remaining is %u\n", *src_len,
                 buf_remaining);
        *src_len = buf_remaining;
    }

    *src_len = buf_processed;
    *dst_len = output_len;
    return ret;
}

static int doDecompressBuffer(QzSession_T *sess,
                              unsigned char *src, unsigned int *src_len,
                              unsigned char *dst, unsigned int *dst_len,
                              RunTimeList_T *time_list, int last)
{
    int ret = QZ_FAIL;
    unsigned int done = 0;
    unsigned int buf_processed = 0;
    unsigned int src_remain = *src_len;
    unsigned int output_len = 0;
    RunTimeList_T *time_node = time_list;
    unsigned int src_remain_output = *dst_len;
    unsigned int total = *dst_len;

    while (time_node->next) {
        time_node = time_node->next;
    }

    while (!done) {
        RunTimeList_T *run_time = calloc(1, sizeof(RunTimeList_T));
        CHECK_ALLOC_RETURN_VALUE(run_time)
        run_time->next = NULL;
        time_node->next = run_time;
        time_node = run_time;

        gettimeofday(&run_time->time_s, NULL);

        /* do actual work */
        ret = qzDecompress(sess, src, src_len, dst, &src_remain_output);

        if (QZ_DATA_ERROR == ret ||
            (QZ_BUF_ERROR == ret && 0 == *src_len)) {
            done = 1;
        }

        if (ret != QZ_OK &&
            ret != QZ_BUF_ERROR &&
            ret != QZ_DATA_ERROR) {
            QZ_ERROR("doDecompressBuffer in qzip_7z.c :failed with error: %d\n",
                     ret);
            break;
        }

        gettimeofday(&run_time->time_e, NULL);

        *dst_len = src_remain_output;
        buf_processed += *src_len;
        src_remain -= *src_len;
        output_len += *dst_len;
        src_remain_output = total - output_len;
        if (0 == src_remain) {
            done = 1;
        }
        if (0 == src_remain_output) {
            done = 1;
        }
        src += *src_len;
        QZ_DEBUG("src_len is %u ,src_remain is %u\n", *src_len, src_remain);
        *src_len = src_remain;
    }

    *src_len = buf_processed;
    *dst_len = output_len;
    return ret;
}

int doCompressFile(QzSession_T *sess, Qz7zItemList_T *list,
                   const char *dst_file_name)
{
    int ret = OK;
    struct stat src_file_stat;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    off_t src_file_size = 0, dst_file_size = 0, file_remaining = 0;
    const char *src_file_name = NULL;
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;
    FILE *src_file = NULL;
    FILE *dst_file = NULL;
    Qz7zEndHeader_T *eheader = NULL;
    unsigned int bytes_read = 0;
    unsigned long bytes_processed = 0;
    unsigned int ratio_idx = 0;
    const unsigned int ratio_limit =
        sizeof(g_bufsz_expansion_ratio) / sizeof(unsigned int);
    unsigned int read_more = 0;
    int src_fd = 0;
    RunTimeList_T *time_list_head = malloc(sizeof(RunTimeList_T));
    CHECK_ALLOC_RETURN_VALUE(time_list_head)
    gettimeofday(&time_list_head->time_s, NULL);
    time_list_head->time_e = time_list_head->time_s;
    time_list_head->next = NULL;

    size_t  total_compressed_size = 0;
    int is_last;
    int n_part; // how much parts can the src file be splited
    int n_part_i;


    dst_file = fopen(dst_file_name, "w+");
    if (!dst_file) {
        QZ_ERROR("Cannot open file: %s\n", dst_file_name);
        return QZ7Z_ERR_OPEN;
    }
    Qz7zSignatureHeader_T *sheader = generateSignatureHeader();
    if (!sheader) {
        QZ_ERROR("Cannot generate signuature header, out of memory");
        return QZ7Z_ERR_OOM;
    }

    qz7zWriteSignatureHeader(sheader, dst_file);

    uint64_t non_empty_number = list->items[1]->total;

    if (non_empty_number) {

        for (int i = 0; i < non_empty_number; ++i) {

            Qz7zFileItem_T *cur_file = qzListGet(list->items[1], i);
            src_file_name = cur_file->fileName;

            ret = lstat(src_file_name, &src_file_stat);
            if (ret) {
                QZ_ERROR("stat(): failed\n");
                return QZ7Z_ERR_STAT;
            }

            if (S_ISBLK(src_file_stat.st_mode)) {
                if ((src_fd = open(src_file_name, O_RDONLY)) < 0) {
                    perror(src_file_name);
                    return QZ7Z_ERR_OPEN;
                } else {
                    if (ioctl(src_fd, BLKGETSIZE, &src_file_size) < 0) {
                        close(src_fd);
                        perror(src_file_name);
                        return QZ7Z_ERR_IOCTL;
                    }
                    src_file_size *= 512;
                    /* size get via BLKGETSIZE is divided by 512 */
                    close(src_fd);
                }
            } else {
                src_file_size = src_file_stat.st_size;
            }
            src_buffer_size = (src_file_size > SRC_BUFF_LEN) ?
                              SRC_BUFF_LEN : src_file_size;
            dst_buffer_size = qzMaxCompressedLength(src_buffer_size, sess);

            if (0 == src_file_size) {
                dst_buffer_size = 1024;
            }

            src_buffer = malloc(src_buffer_size);
            CHECK_ALLOC_RETURN_VALUE(src_buffer)
            dst_buffer = malloc(dst_buffer_size);
            CHECK_ALLOC_RETURN_VALUE(dst_buffer)
            src_file = fopen(src_file_name, "r");
            if (!src_file) {
                QZ_ERROR("create %s error\n", src_file_name);
                return QZ7Z_ERR_OPEN;
            }

            file_remaining = src_file_size;
            read_more = 1;

            n_part = src_file_size / SRC_BUFF_LEN;
            n_part = (src_file_size % SRC_BUFF_LEN) ? n_part + 1 : n_part;
            is_last = 0;
            n_part_i = 1;

            do {
                is_last = (i == non_empty_number - 1) && (n_part_i++ == n_part);

                if (read_more) {
                    if (cur_file->isSymLink) {
                        int size;
                        size = readlink(cur_file->fileName, (char *)src_buffer,
                                        src_buffer_size);
                        bytes_read = size;
                    } else {
                        bytes_read = fread(src_buffer, 1, src_buffer_size,
                                           src_file);
                        QZ_PRINT("Reading input file %s (%u Bytes)\n",
                                 src_file_name, bytes_read);
                    }
                } else {
                    bytes_read = file_remaining;
                }

                puts("Compressing...");

                ret = doCompressBuffer(sess, src_buffer, &bytes_read,
                                       dst_buffer, &dst_buffer_size,
                                       time_list_head, dst_file, &dst_file_size,
                                       is_last);

                if (QZ_DATA_ERROR == ret || QZ_BUF_ERROR == ret) {
                    bytes_processed += bytes_read;
                    if (0 != bytes_read) {
                        if (-1 == fseek(src_file, bytes_processed, SEEK_SET)) {
                            ret = ERROR;
                            goto exit;
                        }
                        read_more = 1;
                    } else if (QZ_BUF_ERROR == ret) {
                        // dest buffer not long enough
                        if (ratio_limit == ratio_idx) {
                            QZ_ERROR("Could not expand more destination "
                                     "buffer\n");
                            ret = ERROR;
                            goto exit;
                        }

                        free(dst_buffer);
                        dst_buffer_size = src_buffer_size *
                                          g_bufsz_expansion_ratio[ratio_idx++];
                        dst_buffer = malloc(dst_buffer_size);
                        if (NULL == dst_buffer) {
                            QZ_ERROR("Fail to allocate destination buffer "
                                     "with size %u\n", dst_buffer_size);
                            ret = ERROR;
                            goto exit;
                        }

                        read_more = 0;
                    } else {
                        // corrupt data
                        ret = ERROR;
                        goto exit;
                    }
                } else if (QZ_OK != ret) {
                    QZ_ERROR("Process file error: %d\n", ret);
                    ret = ERROR;
                    goto exit;
                } else {
                    if (cur_file->isSymLink) {
                        read_more = 0;
                    } else {
                        read_more = 1;
                    }
                }

                file_remaining -= bytes_read;
                total_compressed_size = dst_file_size;

            } while (file_remaining > 0);

            free(src_buffer);
            free(dst_buffer);
            fclose(src_file);
        }// end for

    } else {
        QZ_PRINT("Compressing...\n");
    }

    eheader = generateEndHeader(list, total_compressed_size);

    uint64_t eheader_size;
    uint32_t crc = 0;
    eheader_size = qz7zWriteEndHeader(eheader, dst_file, &crc);
    if (eheader_size == 0) {
        QZ_ERROR("Cannot write 7z end header\n");
        return QZ7Z_ERR_END_HEADER;
    }

    QZ_DEBUG("total compressed: %lu\n"
             "eheader_size: %lu\n"
             "crc: %x\n",
             total_compressed_size, eheader_size, crc);

    uint32_t  start_crc = 0;
    unsigned char start_header[24];

    memcpy(start_header + SIGNATUREHEADER_OFFSET_NEXTHEADER_OFFSET,
           &total_compressed_size, sizeof(total_compressed_size));
    memcpy(start_header + SIGNATUREHEADER_OFFSET_NEXTHEADER_SIZE, &eheader_size,
           sizeof(eheader_size));
    memcpy(start_header + SIGNATUREHEADER_OFFSET_NEXTHEADER_CRC, &crc,
           sizeof(crc));
    start_crc = crc32(start_crc,
                      start_header + SIGNATUREHEADER_OFFSET_NEXTHEADER_OFFSET,
                      20);
    memcpy(start_header, &start_crc, sizeof(start_crc));
    fseek(dst_file, SIGNATUREHEADER_OFFSET_BASE, SEEK_SET);
    fwrite(start_header, 1, sizeof(start_header), dst_file);

    displayStats(time_list_head, src_file_size, dst_file_size,
                 1/* is_compress */);

exit:
    freeTimeList(time_list_head);
    free(sheader);
    qzFreeEndHeader(eheader, 1);
    fclose(dst_file);

    if (!g_keep && OK == ret) {
        int re = deleteSourceFile(list);
        if (re != QZ7Z_OK) {
            QZ_ERROR("deleteSourceFile error: %d\n", re);
            return re;
        }
    }

    return ret;
}

int qz7zCompress(QzSession_T *sess, Qz7zItemList_T *list,
                 const char *out_name)
{
    return doCompressFile(sess, list, out_name);
}

int deleteSourceFile(Qz7zItemList_T *list)
{
    if (list == NULL) {
        QZ_ERROR("the input is NULL\n");
        return QZ7Z_ERR_NULL_INPUT_LIST;
    }
    QzListHead_T *head;
    QzListNode_T *ptr;
    for (int i = 1; i >= 0; i--) {
        head = list->items[i];
        if (head->total == 0) {
            continue;
        }
        int n_node = (head->total + (head->num - 1)) / head->num;
        for (int node = n_node - 1; node >= 0; node--) {
            ptr = head->next;
            int end = 0;
            while (end < node) {
                ptr = ptr->next;
                end++;
            }
            for (int j = ptr->used - 1; j >= 0; j--) {
                Qz7zFileItem_T *item = (Qz7zFileItem_T *)(*(ptr->items + j));
                int re = remove(item->fileName);
                if (re != 0) {
                    QZ_ERROR("Remove error\n");
                    return QZ7Z_ERR_REMOVE;
                }
            }
        }
    }
    return QZ7Z_OK;
}

Qz7zSignatureHeader_T *qz7zResolveSignatureHeader(FILE *fp)
{
    int n;
    Qz7zSignatureHeader_T *sheader = malloc(sizeof(Qz7zSignatureHeader_T));
    CHECK_ALLOC_RETURN_VALUE(sheader)

    n = fread(&sheader->signature, sizeof(unsigned char), 6, fp);
    CHECK_FREAD_RETURN(n, 6);
    n = fread(&sheader->majorVersion, sizeof(unsigned char), 1, fp);
    CHECK_FREAD_RETURN(n, 1);
    n = fread(&sheader->minorVersion, sizeof(unsigned char), 1, fp);
    CHECK_FREAD_RETURN(n, 1);
    n = fread(&sheader->startHeaderCRC, sizeof(uint32_t), 1, fp);
    CHECK_FREAD_RETURN(n, 1);
    n = fread(&sheader->nextHeaderOffset, sizeof(uint64_t), 1, fp);
    CHECK_FREAD_RETURN(n, 1);
    n = fread(&sheader->nextHeaderSize, sizeof(uint64_t), 1, fp);
    CHECK_FREAD_RETURN(n, 1);
    n = fread(&sheader->nextHeaderCRC, sizeof(uint32_t), 1, fp);
    CHECK_FREAD_RETURN(n, 1);

    return sheader;
}

#define QZ7Z_DEVELOP_ID_PREFIX_SHIFT  56
#define QZ7Z_DEVELOP_ID_SHIFT         16
Qz7zArchiveProperty_T *qz7zResolveArchiveProperties(FILE *fp)
{
    Qz7zArchiveProperty_T *property = malloc(sizeof(Qz7zArchiveProperty_T));
    CHECK_ALLOC_RETURN_VALUE(property)

    uint64_t  id = getU64FromBytes(fp);

    if ((id >> QZ7Z_DEVELOP_ID_PREFIX_SHIFT) != 0x3f /* 7z dev id prefix */) {
        QZ_ERROR("7z file ArchiveProperties develop ID error.\n"
                 "develop ID should starts with 0x3f\n");
        return NULL;
    }
    QZ_DEBUG("id = %lu\n", id);

    if (((id >> QZ7Z_DEVELOP_ID_SHIFT) & 0xffffffffff) != QZ7Z_DEVELOP_ID) {
        QZ_ERROR("7z file ArchiveProperties develop ID(%lu) error.\n"
                 , id >> 16 & 0xffffffffff);
        return NULL;
    }

    if ((id & 0xffff) != QZ7Z_DEVELOP_SUBID) {
        QZ_ERROR("7z file ArchiveProperties develop subID(%u) error.\n"
                 , id & 0xffff);
        return NULL;
    }

    property->id = id;

    uint64_t size = getU64FromBytes(fp);

    skipNByte(size, fp);

    if (readByte(fp) != PROPERTY_ID_END) {
        QZ_ERROR("Resolve PackInfo: kEnd (0x00) expected\n");
        return NULL;
    }
    return property;
}

Qz7zPackInfo_T *qz7zResolvePackInfo(FILE *fp)
{
    Qz7zPackInfo_T  *pack = malloc(sizeof(Qz7zPackInfo_T));
    CHECK_ALLOC_RETURN_VALUE(pack)

    pack->PackPos = getU64FromBytes(fp);
    pack->NumPackStreams = getU64FromBytes(fp);
    pack->PackSize = malloc(pack->NumPackStreams * sizeof(uint64_t));
    CHECK_ALLOC_RETURN_VALUE(pack->PackSize)

    if (readByte(fp) != PROPERTY_ID_SIZE) {
        QZ_ERROR("Resolve PackInfo: kSize (0x09) expected\n");
        return NULL;
    }

    for (int i = 0; i < pack->NumPackStreams; ++i) {
        pack->PackSize[i] = getU64FromBytes(fp);
    }

    if (readByte(fp) != PROPERTY_ID_END) {
        QZ_ERROR("Resolve PackInfo: kEnd (0x00) expected\n");
        return NULL;
    }

    return pack;
}

Qz7zCodersInfo_T *qz7zResolveCodersInfo(FILE *fp)
{
    unsigned char c;
    Qz7zCodersInfo_T *coders = malloc(sizeof(Qz7zCodersInfo_T));
    CHECK_ALLOC_RETURN_VALUE(coders)

    if ((c = readByte(fp)) != PROPERTY_ID_FOLDER) {
        QZ_ERROR("Resolve CodersInfo: kFolders(0x0b) expected: %02x\n", c);
        return NULL;
    }

    coders->numFolders = getU64FromBytes(fp);
    coders->folders = malloc(coders->numFolders * sizeof(Qz7zFolderInfo_T));
    CHECK_ALLOC_RETURN_VALUE(coders->folders)

    if ((c = readByte(fp)) == 0) {
        for (int i = 0; i < coders->numFolders; ++i) {
            Qz7zFolderInfo_T *p = &coders->folders[i];
            size_t n;
            unsigned int id_size;
            p->numCoders = readByte(fp);
            p->coder_list = malloc(sizeof(Qz7zCoder_T));
            p->coder_list->coderFirstByte.uc = readByte(fp);
            id_size = p->coder_list->coderFirstByte.st.CodecIdSize;
            p->coder_list->codecID = malloc(id_size);
            n = fread(p->coder_list->codecID, 1, id_size, fp);
            CHECK_FREAD_RETURN(n, id_size)
            QZ_DEBUG("codec id: %0x %0x %0x \n",
                     p->coder_list->codecID[0],
                     p->coder_list->codecID[1],
                     p->coder_list->codecID[2]);
        }
    } else if (c == 1) {
        coders->dataStreamIndex = getU64FromBytes(fp);
    } else {
        QZ_ERROR("Folders(0x00) or DataStreamIndex(0x01) expected\n");
        return NULL;
    }

    if ((c = readByte(fp)) != PROPERTY_ID_CODERS_UNPACK_SIZE) {
        QZ_ERROR("Resolve CodersInfo: kCoderUnpackSize(0x0c) expected: %02x\n",
                 c);
        return NULL;
    }

    coders->unPackSize = malloc(coders->numFolders * sizeof(uint64_t));
    for (int i = 0; i < coders->numFolders; ++i) {
        coders->unPackSize[i] = getU64FromBytes(fp);
    }

    if (readByte(fp) != PROPERTY_ID_END) {
        QZ_ERROR("Resolve CodersInfo: kEnd (0x00) expected\n");
        return NULL;
    }
    QZ_DEBUG("Resolve CodersInfo: finished\n");

    return coders;
}

Qz7zSubstreamsInfo_T *qz7zResolveSubstreamsInfo(int n_folder, FILE *fp)
{
    unsigned char c;
    int total = 1;
    int end = 0;

    Qz7zSubstreamsInfo_T *substreams = malloc(sizeof(Qz7zSubstreamsInfo_T));
    CHECK_ALLOC_RETURN_VALUE(substreams)
    memset(substreams, 0, sizeof(Qz7zSubstreamsInfo_T));

    while (!end) {
        c = readByte(fp);

        switch (c) {

        case PROPERTY_ID_NUM_UNPACK_STREAM:
            total = 0;
            QZ_DEBUG("Resolve SubstreamsInfo: number folders: %d\n", n_folder);
            substreams->numUnPackStreams = (uint64_t *)malloc(n_folder *
                                           sizeof(uint64_t));
            CHECK_ALLOC_RETURN_VALUE(substreams->numUnPackStreams)

            for (int i = 0; i < n_folder; ++i) {
                substreams->numUnPackStreams[i] = getU64FromBytes(fp);
                QZ_DEBUG(" numUnPackStreams[i] = %lu\n",
                         substreams->numUnPackStreams[i]);
                total += substreams->numUnPackStreams[i];
            }
            QZ_DEBUG("resolve numUnpackStreams(0x0d) done\n");
            break;

        case PROPERTY_ID_SIZE:
            if (total - n_folder == 0) {
                QZ_DEBUG("every folder has one file. No unpacksize part. \n");
            } else {
                substreams->unPackSize = (uint64_t *)malloc((total - n_folder) *
                                         sizeof(uint64_t));
                CHECK_ALLOC_RETURN_VALUE(substreams->unPackSize)
                for (int i = 0; i < total - n_folder; ++i) {

                    substreams->unPackSize[i] = getU64FromBytes(fp);
                    QZ_DEBUG("unpacksize: %lu total:%d folder:%d\n",
                             substreams->unPackSize[i], total, n_folder);
                }
            }

            QZ_DEBUG("resolve kSize(0x09) done \n");
            break;

        case PROPERTY_ID_CRC:
            if ((c = readByte(fp)) != 1) {
                QZ_DEBUG("Resolve Substreams Info: not allaredefined ERROR. "
                         "c = %02x\n", c);
                return NULL;
            }
            QZ_DEBUG(" read allaredefined : 111 total: %d\n", total);

            for (int i = 0; i < total; ++i) {
                readCRC(fp);
            }

            QZ_DEBUG("resolve CRC(0x0a) done of substreams\n");
            break;

        case PROPERTY_ID_END:
            end = 1;
            break;

        default:
            QZ_ERROR("resolve unexpected byte\n");
            return NULL;
        }
    }

    QZ_DEBUG("Resolve SubstreamsInfo: finished\n");
    return substreams;
}

int readNames(Qz7zFileItem_T *p, uint64_t num, FILE *fp)
{
    unsigned char c;
    uint64_t u64;
    int i, j;
    char path[PATH_MAX];

    /* wc is used to store a wchar_t
       for ASCII, it is stored at wc[0] and wc[1] is '\0' */
    char wc[2];
    size_t n;

    u64 = getU64FromBytes(fp);  // size
    QZ_DEBUG("u64 = %lu\n", u64);
    (void)u64;
    c = readByte(fp);
    if (c != 0) {
        QZ_ERROR("0x11 label external is not 0. Exit. c = %d\n", c);
        return QZ7Z_ERR_NOT_EXPECTED_CHAR;
    }

    for (i = 0; i < num; ++i) {
        memset(path, 0, sizeof(path));
        j = 0;
        while (1) {

            n = fread(wc, 1, sizeof(wc), fp);
            CHECK_FREAD_RETURN(n, sizeof(wc))
            if (!wc[0] && !wc[1])  // two-zero byte means the end
                break;

            path[j++] = wc[0];
        }
        path[j] = 0;
        QZ_DEBUG("path: %s length: %d\n", path, j);

        (p + i)->nameLength = j + 1; // not include terminal null byte
        (p + i)->fileName = malloc(j + 1);
        CHECK_ALLOC_RETURN_VALUE((p + i)->fileName)

        strncpy((p + i)->fileName, path, j + 1);
    }
    return QZ7Z_OK;
}

int readTimes(Qz7zFileItem_T *p, uint64_t num, FILE *fp)
{
    uint64_t    section_size;
    size_t      nr;
    FILETIME_T  ft;

    section_size = getU64FromBytes(fp);
    (void)section_size;
    if (readByte(fp) != 1) {
        QZ_ERROR("Resolve Times: AllAreDefined must be 1\n");
        return QZ7Z_ERR_TIMES;
    }
    if (readByte(fp) != 0) {
        QZ_ERROR("Resolve Times: External must be 0\n");
        return QZ7Z_ERR_TIMES;
    }

    for (int i = 0; i < num; ++i) {
        nr = fread(&ft.low, sizeof(uint32_t), 1, fp);
        if (nr < 1) {
            QZ_ERROR("readTimes: fread error\n");
            return QZ7Z_ERR_READ_LESS;
        }
        nr = fread(&ft.high, sizeof(uint32_t), 1,  fp);
        if (nr < 1) {
            QZ_ERROR("readTimes: fread error\n");
            return QZ7Z_ERR_READ_LESS;
        }
        (p + i)->mtime = filetimeToUnixtime(ft);
        (p + i)->atime = filetimeToUnixtime(ft);
    }
    return QZ7Z_OK;
}

int readAttributes(Qz7zFileItem_T *p, uint64_t num, FILE *fp)
{
    unsigned char c;
    uint64_t u64;
    uint32_t attr;
    size_t      nr;

    u64 = getU64FromBytes(fp); // size
    (void)u64;

    c = readByte(fp); // AllAreDefined
    if (c != 1) {
        QZ_ERROR("Resolve Attributes: AllAreDefined is not 1. Exit. "
                 "c = %d\n", c);
        return QZ7Z_ERR_NOT_EXPECTED_CHAR;
    }

    c = readByte(fp); // External
    if (c != 0) {
        QZ_ERROR("Resolve Attributes: External is not 0. Exit. c = %d\n", c);
        return QZ7Z_ERR_NOT_EXPECTED_CHAR;
    }

    for (int i = 0; i < num; ++i) {
        nr = fread(&attr, sizeof(uint32_t), 1, fp);
        if (nr < 1) {
            QZ_ERROR("readAttributes: fread error\n");
            return QZ7Z_ERR_READ_LESS;
        }
        (p + i)->attribute = attr;
    }
    return QZ7Z_OK;
}

Qz7zFilesInfo_Dec_T *qz7zResolveFilesInfo(FILE *fp)
{
    int n;
    int i;
    int j;
    int end;
    int file_index;
    unsigned char c;
    uint64_t u64;
    uint64_t total_num;
    uint64_t dir_num;
    uint64_t file_num;

    Qz7zFilesInfo_Dec_T *files = malloc(sizeof(Qz7zFilesInfo_Dec_T));
    CHECK_ALLOC_RETURN_VALUE(files)
    memset(files, 0, sizeof(Qz7zFilesInfo_Dec_T));

    total_num = getU64FromBytes(fp);
    Qz7zFileItem_T *p = (Qz7zFileItem_T *)malloc(total_num * sizeof(
                            Qz7zFileItem_T));
    CHECK_ALLOC_RETURN_VALUE(p)

    memset(p, 0, total_num * sizeof(*p));

    end = 0;
    while (!end) {
        switch ((c = readByte(fp))) {
        case PROPERTY_ID_END:
            end = 1;
            break;

        case PROPERTY_ID_EMPTY_STREAM:
            file_index = 0;
            dir_num = 0;
            /* n bytes to hold this infomation */
            n = (total_num % 8) ? (total_num / 8 + 1) : (total_num / 8);
            u64 = getU64FromBytes(fp);  /* property size */

            file_index = 0;
            for (i = 0; i < n; ++i) { // read n bytes property
                c = readByte(fp);
                for (j = 7; j >= 0; --j) {
                    int is_dir = !!(c & 1 << j);
                    p[file_index++].isDir = is_dir;
                    if (is_dir) {
                        dir_num++;
                    }
                    if (total_num == file_index) {
                        break;
                    }
                }
            }
            file_num = total_num - dir_num;
            files->file_num = file_num;
            files->dir_num = dir_num;

            break;

        case PROPERTY_ID_EMPTY_FILE:
            file_index = 0;

            u64 = getU64FromBytes(fp); // property size
            for (i = 0; i < u64; ++i) {
                c = readByte(fp);
                for (j = 7; j >= 0; --j) {
                    p[file_index].isEmpty = !!(c & 1 << j);
                    if (p[file_index].isEmpty) {
                        p[file_index].isDir = 0;
                    }
                    file_index++;
                    if (total_num == file_index)
                        break;
                }
            }
            break;

        case PROPERTY_ID_NAME:
            if (readNames(p, total_num, fp) < 0) return NULL;
            break;

        case PROPERTY_ID_CTIME:
        case PROPERTY_ID_ATIME:
        case PROPERTY_ID_MTIME:
            readTimes(p, total_num, fp);
            break;

        case PROPERTY_ID_ATTRIBUTES:
            if (readAttributes(p, total_num, fp) < 0) return NULL;
            break;

        case PROPERTY_ID_DUMMY:
            c = readByte(fp);
            if (c) {
                readNByte(c, fp); // to skip
            }
            break;

        default:
            QZ_ERROR("Not expected attribute\n");
            return NULL;
        }
    }

    files->items = p;
    return files;
}

Qz7zEndHeader_T *qz7zResolveEndHeader(FILE *fp, Qz7zSignatureHeader_T *sheader)
{
    Qz7zEndHeader_T *eheader = malloc(sizeof(Qz7zEndHeader_T));
    CHECK_ALLOC_RETURN_VALUE(eheader)

    eheader->streamsInfo = malloc(sizeof(Qz7zStreamsInfo_T));
    CHECK_ALLOC_RETURN_VALUE(eheader->streamsInfo)

    eheader->filesInfo_Dec = malloc(sizeof(Qz7zFilesInfo_Dec_T));
    CHECK_ALLOC_RETURN_VALUE(eheader->filesInfo_Dec)

    unsigned int status = 0;
    unsigned char c;
    int end = 0;
    int has_archive_property = 0;
    int has_streams_info = 0;

    fseek(fp, sheader->nextHeaderOffset + 0x20, SEEK_CUR);

    while (!end) {

        switch (c = readByte(fp)) {
        case PROPERTY_ID_HEADER:
            status = RESOLVE_STATUS_IN_HEADER;
            break;
        case PROPERTY_ID_ARCHIVE_PROPERTIES:
            has_archive_property = 1;
            status = RESOLVE_STATUS_IN_ARCHIVE_PROPERTIES;
            eheader->propertyInfo = qz7zResolveArchiveProperties(fp);
            if (!eheader->propertyInfo) return NULL;
            break;
        case PROPERTY_ID_MAIN_STREAMSINFO:
            has_streams_info = 1;
            status = RESOLVE_STATUS_IN_STREAMSINFO;
            break;
        case PROPERTY_ID_PACKINFO:
            status = RESOLVE_STATUS_IN_PACKINFO;
            eheader->streamsInfo->packInfo = qz7zResolvePackInfo(fp);
            if (!eheader->streamsInfo->packInfo) return NULL;
            break;
        case PROPERTY_ID_UNPACKINFO:
            status = RESOLVE_STATUS_IN_CODERSINFO;
            eheader->streamsInfo->codersInfo = qz7zResolveCodersInfo(fp);
            if (!eheader->streamsInfo->codersInfo) return NULL;
            break;
        case PROPERTY_ID_SUBSTREAMSINFO:
            status = RESOLVE_STATUS_IN_SUBSTREAMSINFO;
            eheader->streamsInfo->substreamsInfo =
                qz7zResolveSubstreamsInfo(eheader->streamsInfo
                                          ->codersInfo->numFolders, fp);
            if (!eheader->streamsInfo->substreamsInfo) return NULL;
            break;
        case PROPERTY_ID_FILESINFO:
            status = RESOLVE_STATUS_IN_FILESINFO;
            eheader->filesInfo_Dec = qz7zResolveFilesInfo(fp);
            if (!eheader->filesInfo_Dec) return NULL;
            break;
        case PROPERTY_ID_END:
            QZ_DEBUG("readed kEnd: %d\n\n\n", status);
            if (status == RESOLVE_STATUS_IN_FILESINFO)
                end = 1;
            break;
        default:
            QZ_ERROR("Resolve End Header Error\n");
            return NULL;
        }
    }

    if (!has_archive_property) {
        QZ_ERROR("ERROR: property 'QAT7z' not found\n");
        QZ_ERROR("This archive is not compressed by QAT,");
        QZ_ERROR("QAT only support 7z archive compressed by QAT\n");
        return NULL;
    }

    if (!has_streams_info) {
        free(eheader->streamsInfo);
        eheader->streamsInfo = NULL;
    }

    return eheader;
}

static int createEmptyFile(const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        QZ_ERROR("create %s error\n", filename);
        return -1;
    }
    fclose(fp);
    return 0;
}

/**
 * create `newdir` at current dirctory
 * back: 1 return to original dir, 0 otherwise
 */
static int createDir(const char *newdir, int back)
{
    int ret;
    char *dirc, *basec;
    char pwd[PATH_MAX];

    if (!getcwd(pwd, sizeof(pwd))) {
        return QZ7Z_ERR_GETCWD;
    }
    QZ_DEBUG("working directory: %s\n", pwd);

    dirc = strdup(newdir);
    dirc = dirname(dirc);

    if (!strcmp(dirc, ".")) {
        if (mkdir(newdir, 0755) < 0) {
            if (errno != EEXIST) {
                perror("create dir failed\n");
                return QZ7Z_ERR_MKDIR;
            }
        }
        if (chdir(newdir) < 0) {
            perror("cannot change working dir\n");
            return QZ7Z_ERR_CHDIR;
        }

        if (back) {
            if (chdir(pwd) < 0) {
                perror("cannot change working dir\n");
                return QZ7Z_ERR_CHDIR;
            }
        }

        return QZ7Z_OK;
    }

    basec = strdup(newdir);
    basec = basename(basec);

    if ((ret = createDir(dirc, 0)) < 0) return ret;
    if ((ret = createDir(basec, 0)) < 0) return ret;

    if (back) {
        if (chdir(pwd) < 0) {
            perror("cannot change working dir\n");
            return QZ7Z_ERR_CHDIR;
        }
    }
    return QZ7Z_OK;
}

void decompressEmptyfilesAndDirectories(Qz7zFilesInfo_Dec_T *info)
{
    uint64_t  num = info->dir_num;
    Qz7zFileItem_T  *p = info->items;
    for (int i = 0; i < num; ++i) {
        if (p[i].isDir) {
            createDir(p[i].fileName, 1);
        } else {
            createEmptyFile(p[i].fileName);
        }
    }
}

int checkHeaderCRC(Qz7zSignatureHeader_T *sh, FILE *fp)
{
    uint32_t  crc = 0;
    crc = crc32(crc, (unsigned char *)&sh->nextHeaderOffset, sizeof(uint64_t));
    crc = crc32(crc, (unsigned char *)&sh->nextHeaderSize, sizeof(uint64_t));
    crc = crc32(crc, (unsigned char *)&sh->nextHeaderCRC, sizeof(uint32_t));

    if (crc != sh->startHeaderCRC) {
        QZ_ERROR("Signature CRC failed\n");
        return -1;
    }

    size_t n;
    unsigned char buf[4096];
    crc = 0;
    fseek(fp, sh->nextHeaderOffset + 0x20, SEEK_SET);
    while ((n = fread(buf, 1, sizeof(buf), fp))) {
        crc = crc32(crc, buf, n);
    }
    if (crc != sh->nextHeaderCRC) {
        QZ_ERROR("End header CRC failed\n");
        return -1;
    }
    fseek(fp, 0, SEEK_SET);
    return 0;
}

void qzFreePropertyInfo(Qz7zArchiveProperty_T *info)
{
    Qz7zArchiveProperty_T *cur = info;
    while (cur) {
        free(cur->data);
        cur = cur->next;
    }
    free(info);
}

void qzFreeStreamsInfo(Qz7zStreamsInfo_T *info)
{
    if (info) {
        if (info->packInfo) {
            free(info->packInfo->PackSize);
            free(info->packInfo);
        }

        if (info->codersInfo) {
            if (info->codersInfo->folders) {
                Qz7zCoder_T *cur = info->codersInfo->folders->coder_list;
                while (cur) {
                    free(cur->codecID);
                    cur = cur->next;
                }
                free(info->codersInfo->folders->coder_list);
                free(info->codersInfo->folders);
            }
            free(info->codersInfo->unPackSize);
            free(info->codersInfo);
        }

        if (info->substreamsInfo) {
            free(info->substreamsInfo->numUnPackStreams);
            free(info->substreamsInfo->unPackSize);
            if (info->substreamsInfo->digests) {
                free(info->substreamsInfo->digests->crc);
                free(info->substreamsInfo->digests);
            }
            free(info->substreamsInfo);
        }
        free(info);
    }
}

void qzFreeFilesInfo(Qz7zFilesInfo_T *info)
{
    free(info);
}

void qzFreeFilesDecInfo(Qz7zFilesInfo_Dec_T *info)
{
    if (info) {
        free(info->items);
        free(info);
    }
}

void qzFreeEndHeader(Qz7zEndHeader_T *h, int is_compress)
{
    qzFreePropertyInfo(h->propertyInfo);
    qzFreeStreamsInfo(h->streamsInfo);
    if (is_compress) {
        qzFreeFilesInfo(h->filesInfo);
    } else {
        qzFreeFilesDecInfo(h->filesInfo_Dec);
    }
    free(h);
}

static int convertToSymlink(const char *name)
{
    FILE *file = fopen(name, "rb");
    if (file) {
        char buf[1000 + 1];
        char *ret = fgets(buf, sizeof(buf) - 1, file);
        fclose(file);
        if (ret) {
            int ir = unlink(name);
            if (ir == 0) {
                ir = symlink(buf, name);
            }
            return ir;
        }
    }
    return -1;
}

static int doDecompressFile(QzSession_T *sess, const char *src_file_name)
{
    int ret = OK;
    struct stat src_file_stat;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    unsigned int saved_dst_buffer_size = 0;
    off_t src_file_size = 0, dst_file_size = 0, file_remaining = 0;
    unsigned char *src_buffer = NULL;
    unsigned char *src_buffer_orig = NULL;
    unsigned char *dst_buffer = NULL;
    FILE *src_file = NULL;
    FILE *dst_file = NULL;
    unsigned int bytes_read = 0;
    unsigned int ratio_idx = 0;
    const unsigned int ratio_limit =
        sizeof(g_bufsz_expansion_ratio) / sizeof(unsigned int);
    unsigned int read_more = 0;
    int src_fd = 0;

    int is_last;
    int n_part; // how much parts can the src file be splited
    int n_part_i;
    Qz7zSignatureHeader_T *sheader;
    Qz7zEndHeader_T *eheader;

    RunTimeList_T *time_list_head = malloc(sizeof(RunTimeList_T));
    CHECK_ALLOC_RETURN_VALUE(time_list_head)
    gettimeofday(&time_list_head->time_s, NULL);
    time_list_head->time_e = time_list_head->time_s;
    time_list_head->next = NULL;

    RunTimeList_T *time_node = time_list_head;
    RunTimeList_T *run_time = calloc(1, sizeof(RunTimeList_T));
    CHECK_ALLOC_RETURN_VALUE(run_time)
    run_time->next = NULL;
    time_node->next = run_time;
    time_node = run_time;

    gettimeofday(&run_time->time_s, NULL);

    src_file = fopen(src_file_name, "r");
    if (src_file == NULL) {
        QZ_ERROR("cannot open file %s: %d\n", src_file_name, errno);
        return QZ7Z_ERR_OPEN;
    }

    sheader = qz7zResolveSignatureHeader(src_file);

#ifdef QZ7Z_DEBUG
    print_signature_header(sheader);
#endif

    if (checkHeaderCRC(sheader, src_file) < 0) {
        QZ_ERROR("Header error: CRC check failed\n");
        return QZ7Z_ERR_HEADER_CRC;
    }

    eheader = qz7zResolveEndHeader(src_file, sheader);
    if (!eheader) {
        QZ_ERROR("Cannot resolve end header\n");
        return QZ7Z_ERR_RESOLVE_END_HEADER;
    }
    fclose(src_file);

    // decode the dir
    Qz7zFileItem_T *p = eheader->filesInfo_Dec->items;
    uint64_t dir_num = eheader->filesInfo_Dec->dir_num;
    uint64_t fil_num = eheader->filesInfo_Dec->file_num;

    decompressEmptyfilesAndDirectories(eheader->filesInfo_Dec);

    uint64_t file_index = 0;
    // decode the content
    if (eheader->streamsInfo) {

        uint64_t folder_num = eheader->streamsInfo->codersInfo->numFolders;
        Qz7zFileItem_T *file_items = eheader->filesInfo_Dec->items +
                                     eheader->filesInfo_Dec->dir_num;

        for (int i = 0; i < folder_num; ++i) {
            uint64_t num_files_in_folder =
                eheader->streamsInfo->substreamsInfo->numUnPackStreams[i];
            uint64_t total_unPack_size =
                eheader->streamsInfo->codersInfo->unPackSize[i];
            for (int j = 0; j < num_files_in_folder - 1; ++file_index, ++j) {
                (file_items + file_index)->size =
                    eheader->streamsInfo->substreamsInfo->unPackSize[j];
                total_unPack_size -= (file_items + file_index)->size;
            }
            (file_items + file_index)->size = total_unPack_size;
        }

        ret = stat(src_file_name, &src_file_stat);
        if (ret) {
            perror(src_file_name);
            return QZ7Z_ERR_STAT;
        }

        if (S_ISBLK(src_file_stat.st_mode)) {
            if ((src_fd = open(src_file_name, O_RDONLY)) < 0) {
                perror(src_file_name);
                return QZ7Z_ERR_OPEN;
            } else {
                if (ioctl(src_fd, BLKGETSIZE, &src_file_size) < 0) {
                    close(src_fd);
                    perror(src_file_name);
                    return QZ7Z_ERR_IOCTL;
                }
                src_file_size *= 512;
                /* size get via BLKGETSIZE is divided by 512 */
                close(src_fd);
            }
        } else {
            src_file_size = src_file_stat.st_size;
        }

        src_file_size -= sheader->nextHeaderSize;
        src_file_size -= 32;

        src_buffer_size = (src_file_size > SRC_BUFF_LEN) ?
                          SRC_BUFF_LEN : src_file_size;
        dst_buffer_size = src_buffer_size *
                          g_bufsz_expansion_ratio[ratio_idx++];
        saved_dst_buffer_size = dst_buffer_size;

        src_buffer = malloc(src_buffer_size);
        CHECK_ALLOC_RETURN_VALUE(src_buffer)
        src_buffer_orig = src_buffer;
        dst_buffer = malloc(dst_buffer_size);
        CHECK_ALLOC_RETURN_VALUE(dst_buffer)
        src_file = fopen(src_file_name, "r");
        if (!src_file) {
            QZ_ERROR("file open error: %s\n", src_file_name);
            return QZ7Z_ERR_OPEN;
        }

        // skip the signature header
        fseek(src_file, 32, SEEK_SET);

        int i = 0; // file index

        file_remaining = src_file_size;
        read_more = 1;

        n_part = src_file_size / SRC_BUFF_LEN;
        n_part = (src_file_size % SRC_BUFF_LEN) ? n_part + 1 : n_part;
        is_last = 0;
        n_part_i = 1;

        off_t          cur_offset;
        cur_offset = 0;
        int need_created = 1;
        int file_read_processed_size = 0;

        do {
            is_last = (n_part_i++ == n_part);

            if (read_more) {
                src_buffer = src_buffer_orig;
                bytes_read = fread(src_buffer, 1, src_buffer_size, src_file);
                QZ_PRINT("Reading input file %s (%u Bytes)\n", src_file_name,
                         bytes_read);
            } else {
                bytes_read = file_remaining;
            }

            puts("Decompressing...");

            if (n_part > 1 && is_last) bytes_read -= sheader->nextHeaderSize;

            int buffer_remaining = bytes_read;
            do {
                bytes_read = buffer_remaining;
                ret = doDecompressBuffer(sess, src_buffer, &bytes_read,
                                         dst_buffer, &dst_buffer_size,
                                         time_list_head, is_last);

                file_read_processed_size += bytes_read;
                src_buffer += bytes_read;
                buffer_remaining -= bytes_read;
                if (QZ_DATA_ERROR == ret || QZ_BUF_ERROR == ret) {
                    if (0 != bytes_read) {
                        if (-1 == fseek(src_file, file_read_processed_size,
                                        SEEK_SET)) {
                            ret = ERROR;
                            goto exit;
                        }
                        read_more = 1;
                    } else if (QZ_BUF_ERROR == ret) {
                        //dest buffer not long enough
                        if (ratio_limit == ratio_idx) {
                            QZ_ERROR("Could not expand more"
                                     "destination buffer\n");
                            ret = ERROR;
                            goto exit;
                        }

                        free(dst_buffer);
                        dst_buffer_size = src_buffer_size *
                                          g_bufsz_expansion_ratio[ratio_idx++];
                        dst_buffer = malloc(dst_buffer_size);
                        if (NULL == dst_buffer) {
                            QZ_ERROR("Fail to allocate destination buffer "
                                     "with size %u\n", dst_buffer_size);
                            ret = ERROR;
                            goto exit;
                        }

                        read_more = 0;
                    } else {
                        // corrupt data
                        ret = ERROR;
                        goto exit;
                    }
                } else if (QZ_OK != ret) {
                    QZ_ERROR("Process file error: %d\n", ret);
                    ret = ERROR;
                    goto exit;
                } else {
                    read_more = 1;

                    unsigned int    dst_left;
                    unsigned char *dst_write;
                    size_t         n_written;
                    struct stat    stat_info;

                    dst_write = dst_buffer;
                    dst_left = dst_buffer_size;
                    dst_file_size += dst_buffer_size;

                    while (dst_left) {

                        if (need_created) {
                            dst_file = fopen((p + dir_num + i)->fileName, "w");
                            fseek(dst_file, 0, SEEK_END);
                            QZ_PRINT("Create %s ...\n",
                                     (p + dir_num + i)->fileName);
                            lstat((p + dir_num + i)->fileName, &stat_info);
                            stat_info.st_mode =
                                ((p + dir_num + i)->attribute) >> 16;
                        }

                        if ((p + dir_num + i)->size - cur_offset <= dst_left) {

                            n_written = fwrite(dst_write, 1,
                                               (p + dir_num + i)->size -
                                               cur_offset,
                                               dst_file);

                            if (n_written == (p + dir_num + i)->size -
                                cur_offset) {
                                fclose(dst_file);
                                ++i;
                                cur_offset = 0;
                                need_created = 1;

                                if (S_ISLNK(stat_info.st_mode)) {
                                    convertToSymlink((p + dir_num + i)->
                                                     fileName);
                                }
                            } else {
                                cur_offset += n_written;
                                need_created = 0;
                            }
                            dst_left -= n_written;
                            dst_write += n_written;

                        } else {
                            need_created = 0;
                            n_written = fwrite(dst_write, 1, dst_left,
                                               dst_file);
                            dst_write = dst_buffer; /* points to the start of
                                                     the dest buffer to read
                                                     more data */
                            cur_offset += dst_left;
                            break;
                        }
                    }// end while
                    dst_buffer_size = saved_dst_buffer_size;
                }
            } while (buffer_remaining);

            file_remaining -= file_read_processed_size;
        } while (file_remaining > 0);

    } else {
        QZ_PRINT("Decompressing...\n");
    }

    gettimeofday(&run_time->time_e, NULL);

    // restore the time
    struct utimbuf tb;
    for (int i = 0; i < dir_num + fil_num; ++i) {
        tb.actime = p[i].atime;
        tb.modtime = p[i].mtime;
        utime(p[i].fileName, &tb);
    }

    // restore the attribute
    for (int i = 0; i < dir_num + fil_num; ++i) {
        chmod(p[i].fileName, p->attribute >> 16);
    }

    displayStats(time_list_head, src_file_size, dst_file_size,
                 0/* is_compress */);
exit:
    freeTimeList(time_list_head);
    free(sheader);
    qzFreeEndHeader(eheader, 0);

    if (eheader->streamsInfo) {
        fclose(src_file);
        free(src_buffer_orig);
        free(dst_buffer);
    }
    if (!g_keep && OK == ret) {
        int re = remove(src_file_name);
        if (re != 0) {
            QZ_ERROR("deleteSourceFile error: %d\n", re);
            return re;
        }
    }
    return ret;
}

/*
 * the main decompress API for 7z file
 */
int qz7zDecompress(QzSession_T *sess, const char *archive)
{
    return doDecompressFile(sess, archive);
}

/*
 * calculate a file's crc
 */
int64_t calculateCRC(char *filename, size_t n)
{
    FILE          *fp;
    uint32_t      crc = 0;
    size_t        ret;

    size_t remaining = n;
    size_t buffer_size;

    unsigned char *buf;
    buf = malloc(SRC_BUFF_LEN);
    if (!buf) {
        QZ_ERROR("oom\n");
        return QZ7Z_ERR_OOM;
    }

    fp = fopen(filename, "r");
    if (!fp) {
        QZ_ERROR("filename open error\n");
        return QZ7Z_ERR_OPEN;
    }

    while (remaining > 0) {
        if (remaining > SRC_BUFF_LEN)
            buffer_size = SRC_BUFF_LEN;
        else
            buffer_size = remaining;

        ret = fread(buf, 1, buffer_size, fp);
        CHECK_FREAD_RETURN(ret, buffer_size)

        crc = crc32(crc, buf, buffer_size);
        remaining -= buffer_size;
    }

    free(buf);
    fclose(fp);
    QZ_DEBUG("%s crc: %08x\n", filename, crc);
    return crc;
}

void qzListDestroy(QzListHead_T *head)
{
    Qz7zFileItem_T  *fi;
    QzListNode_T  *node, *tmp_node;
    node = head->next;
    do {
        for (int j = 0; j < node->used; ++j) {
            fi = node->items[j];
            free(fi->fileName);
            free(fi);
        }
        free(node->items);
        tmp_node = node;
        node = node->next;
        free(tmp_node);
    } while (node);
    free(head);
}

void qz7zItemListDestroy(Qz7zItemList_T *p)
{
    QzListHead_T  *h;
    for (int i = 0; i < 2; ++i) {
        h = p->items[i];
        qzListDestroy(h);
    }

    QzCatagoryTable_T *table = p->table;
    QzCatagory_T      cat;
    for (int i = 0; i < table->cat_num; ++i) {
        cat = table->catas[i];
        free(cat.cat_files->next->items);
        free(cat.cat_files->next);
        free(cat.cat_files);
        free(table->catas);
    }
    free(table);

    free(p);
}

uint32_t calculateSymCRC(char *filename, size_t n)
{
    uint32_t   crc = 0;
    char       *buf;
    buf = malloc(PATH_MAX + 1);
    if (!buf) {
        QZ_ERROR("oom\n");
        return 0;
    }

    ssize_t size = readlink(filename, buf, PATH_MAX);
    if ((unsigned int)size != (unsigned int)n) {
        QZ_ERROR("readlink error\n");
        return 0;
    }
    crc = crc32(crc, (unsigned char *)buf, n);

    free(buf);
    return crc;
}

Qz7zFileItem_T *qzFileItemCreate(char *f)
{
    Qz7zFileItem_T *p = malloc(sizeof(Qz7zFileItem_T));

    if (p) {
        memset(p, 0, sizeof(Qz7zFileItem_T));
        p->nameLength = strlen(f) + 1;
        p->fileName = (char *)malloc(p->nameLength);
        if (!p->fileName) {
            QZ_ERROR("oom\n");
            free(p);
            return NULL;
        }
        memset(p->fileName, 0, p->nameLength);
        strcpy(p->fileName, f);

        struct stat buf;
        if (lstat(p->fileName, &buf) < 0) {
            QZ_ERROR("stat func error\n");
            return NULL;
        }
        if (S_ISLNK(buf.st_mode)) {
            p->isSymLink = 1;
            p->crc = calculateSymCRC(p->fileName, buf.st_size);
            p->size = buf.st_size;
        } else if (S_ISDIR(buf.st_mode)) {
            p->isDir = 1;
        } else {
            p->size = buf.st_size;
            p->isEmpty = buf.st_size ? 0 : 1;
            p->crc = calculateCRC(p->fileName, p->size);
        }
        p->mtime = buf.st_mtime;
        p->mtime_nano = buf.st_mtim.tv_nsec;
        p->atime = buf.st_atime;
        p->atime_nano = buf.st_atim.tv_nsec;
        //p-7zip use 0x80000 as a unix file flag
        p->attribute = buf.st_mode << 16 | (0x8000);
    }
    return p;
}

#define QZ7Z_LIST_DEFAULT_NUM_PER_NODE  1000
void qzListAdd(QzListHead_T *head, void **fi)
{
    // let cur points to the first node
    QzListNode_T *cur = head->next;
    QzListNode_T *last = cur;

    while (cur && cur->used == cur->num) {
        last = cur;
        cur = cur->next;
    }

    if (cur) {   // this node is not full
#ifdef QZ7Z_DEBUG
        QZ_DEBUG("Before Add : head num: %d total: %d last->used: %d\n",
                 head->num, head->total, cur->used);
#endif
        cur->items[cur->used++] = *fi;
        head->total++;
    } else {

        cur = (QzListNode_T *)malloc(head->num * sizeof(QzListNode_T));
        CHECK_ALLOC_RETURN_VALUE(cur);
        cur->num = head->num;
        cur->used = 0;
        cur->next = NULL;
#ifdef QZ7Z_DEBUG
        QZ_DEBUG("applying a new node\n");
#endif
        cur->items = (void **)malloc(head->num * sizeof(void *));
        CHECK_ALLOC_RETURN_VALUE(cur->items)

        last->next = cur;
#ifdef QZ7Z_DEBUG
        QZ_DEBUG("Before Add : head num: %d total: %d cur->used: %d\n",
                 head->num, head->total, cur->used);
#endif
        cur->items[cur->used++] = *fi;
        head->total++;
    }

#ifdef QZ7Z_DEBUG
    QZ_DEBUG(" add %s to list %p : total: %u\n",
             ((Qz7zFileItem_T *)(*fi))->fileName, (void *)head, head->total);
    QZ_DEBUG("After Add : head num: %d total: %d last->used: %d\n",
             head->num, head->total, cur->used);
#endif
}

void *qzListGet(QzListHead_T *head, int index)
{
    int i;

    if (index >= head->total) {
        QZ_ERROR("qzListGet: index out of total\n");
        return NULL;
    }

    QzListNode_T *cur = head->next;
    int steps = index / head->num;

    for (i = 0; i < steps; ++i) {
        cur = cur->next;
    }

    return cur->items[index % head->num];
}

QzListHead_T *qzListCreate(int num_per_node)
{
    QzListNode_T *node;
    if (num_per_node <= 0) {
        num_per_node = QZ7Z_LIST_DEFAULT_NUM_PER_NODE;
    }

    QzListHead_T *p = (QzListHead_T *)malloc(sizeof(QzListHead_T));
    CHECK_ALLOC_RETURN_VALUE(p)

    node = (QzListNode_T *)malloc(sizeof(QzListNode_T));
    CHECK_ALLOC_RETURN_VALUE(node)

    node->items = (void **)malloc(num_per_node * sizeof(void *));
    CHECK_ALLOC_RETURN_VALUE(node->items)

    node->num = num_per_node;
    node->used = 0;
    node->next = NULL;

    p->next = node;
    p->num = num_per_node;
    p->total = 0;

    return p;
}

Qz7zSignatureHeader_T *generateSignatureHeader()
{
    Qz7zSignatureHeader_T  *header = malloc(sizeof(Qz7zSignatureHeader_T));

    CHECK_ALLOC_RETURN_VALUE(header)

    for (int i = 0; i < 6; ++i) {
        header->signature[i] = g_header_signature[i];
    }
    header->majorVersion =  G_7ZHEADER_MAJOR_VERSION;
    header->minorVersion =  G_7ZHEADER_MINOR_VERSION;
    return header;
}

static Qz7zPackInfo_T *generatePackInfo(Qz7zItemList_T *the_list,
                                        size_t compressed_size)
{
    Qz7zPackInfo_T *pack = malloc(sizeof(Qz7zPackInfo_T));

    CHECK_ALLOC_RETURN_VALUE(pack)
    memset(pack, 0, sizeof(Qz7zPackInfo_T));

    pack->PackPos = 0;
    pack->NumPackStreams = the_list->table->cat_num;

    pack->PackSize = malloc(pack->NumPackStreams * sizeof(uint64_t));
    CHECK_ALLOC_RETURN_VALUE(pack->PackSize)
    pack->PackSize[0] = compressed_size;

    pack->PackStreamDigests = NULL;
    return pack;
}

static Qz7zCoder_T *generateCoder()
{

    Qz7zCoder_T *coder = (Qz7zCoder_T *)malloc(sizeof(Qz7zCoder_T));
    CHECK_ALLOC_RETURN_VALUE(coder)

    coder->coderFirstByte.uc = 0x03; /* 0000 0011 */
    coder->codecID = (unsigned char *)malloc(3 * sizeof(unsigned char));
    CHECK_ALLOC_RETURN_VALUE(coder->codecID)

    memcpy((char *)coder->codecID, g_deflate_codecId, 3);
    coder->numInStreams = 0;
    coder->numOutStreams = 0;
    coder->propertySize = 0;
    coder->properties = NULL;
    coder->next = NULL;

    return coder;
}

static Qz7zFolderInfo_T *generateFolderInfo(Qz7zItemList_T *the_list,
        int n_folders)
{
    if (n_folders <= 0) {
        n_folders = 1;
    }
    Qz7zFolderInfo_T *folders = malloc(n_folders * sizeof(Qz7zFolderInfo_T));
    CHECK_ALLOC_RETURN_VALUE(folders)

    folders->items = the_list->items[1];

    folders->numCoders = 1;
    folders->coder_list = generateCoder();
    folders->numBindPairs = 0;
    folders->inIndex = NULL;
    folders->outIndex = NULL;

    folders->numPackedStreams = 0;
    folders->index = NULL;

    return folders;
}

static Qz7zCodersInfo_T *generateCodersInfo(Qz7zItemList_T *the_list)
{
    Qz7zCodersInfo_T *coders = malloc(sizeof(Qz7zCodersInfo_T));
    CHECK_ALLOC_RETURN_VALUE(coders)

    coders->numFolders = the_list->table->cat_num;
    coders->folders = generateFolderInfo(the_list, coders->numFolders);
    coders->unPackSize = malloc(coders->numFolders * sizeof(uint64_t));
    CHECK_ALLOC_RETURN_VALUE(coders->unPackSize)

    for (int i = 0; i < coders->numFolders; ++i) {
        QzCatagory_T *cat = &(the_list->table->catas[i]);
        coders->unPackSize[i] = 0;
        for (int j = 0; j < cat->cat_files->total; ++j) {
            Qz7zFileItem_T *p = (Qz7zFileItem_T *)qzListGet(cat->cat_files, j);
            coders->unPackSize[i] += p->size;
        }
    }

    coders->unPackDigests = NULL; /* not used */
    return coders;
}

static Qz7zDigest_T *generateDigestInfo(QzListHead_T *head)
{
    Qz7zDigest_T *digests = malloc(sizeof(Qz7zDigest_T));
    CHECK_ALLOC_RETURN_VALUE(digests)

    digests->allAreDefined = 1;
    digests->numStreams = head->total;
    digests->numDefined = head->total;
    digests->crc = malloc(digests->numDefined * sizeof(uint32_t));
    CHECK_ALLOC_RETURN_VALUE(digests->crc)

    for (int i = 0; i < digests->numDefined; ++i) {
        Qz7zFileItem_T *p = (Qz7zFileItem_T *)qzListGet(head, i);
        (digests->crc)[i] = p->crc;
    }

    return digests;
}

static Qz7zSubstreamsInfo_T *generateSubstreamsInfo(Qz7zItemList_T *the_list)
{
    int index_of_file = 0; // index of all files in the list
    Qz7zFileItem_T *fi;
    Qz7zSubstreamsInfo_T *substreamsInfo = malloc(sizeof(Qz7zSubstreamsInfo_T));
    CHECK_ALLOC_RETURN_VALUE(substreamsInfo)

    QzListHead_T *h = the_list->items[1];
    uint64_t total_files = h->total;
    if (!total_files) return NULL;

    substreamsInfo->numFolders = the_list->table->cat_num;
    substreamsInfo->numUnPackStreams = malloc(substreamsInfo->numFolders
                                       * sizeof(uint64_t));
    CHECK_ALLOC_RETURN_VALUE(substreamsInfo->numUnPackStreams);

    // n_files - n_folder
    substreamsInfo->unPackSize = malloc((total_files - 1) * sizeof(uint64_t));
    CHECK_ALLOC_RETURN_VALUE(substreamsInfo->unPackSize);

    for (int i = 0; i < substreamsInfo->numFolders; ++i) {
        h = the_list->table->catas[i].cat_files;
        substreamsInfo->numUnPackStreams[i] = h->total;

        if (h->total == 1)
            continue; // folder has one file, don't need the unpacksize

        for (int j = 0; j < h->total - 1; ++j) {
            fi = qzListGet(h, j);
            substreamsInfo->unPackSize[index_of_file++] = fi->size;
        }
    }
    substreamsInfo->digests = generateDigestInfo(h);

    return substreamsInfo;
}

static Qz7zFilesInfo_T *generateFilesInfo(Qz7zItemList_T *the_list)
{
    Qz7zFilesInfo_T *filesInfo = malloc(sizeof(Qz7zFilesInfo_T));
    CHECK_ALLOC_RETURN_VALUE(filesInfo)
    memset(filesInfo, 0, sizeof(Qz7zFilesInfo_T));

    filesInfo->num = the_list->items[0]->total + the_list->items[1]->total;
    filesInfo->head[0] = the_list->items[0];
    filesInfo->head[1] = the_list->items[1];
    return filesInfo;
}

Qz7zStreamsInfo_T *generateStreamsInfo(Qz7zItemList_T *the_list,
                                       size_t compressed_size)
{
    uint64_t n = the_list->items[1]->total;
    if (!n) return NULL;

    Qz7zStreamsInfo_T *streams = malloc(sizeof(Qz7zStreamsInfo_T));
    CHECK_ALLOC_RETURN_VALUE(streams)

    streams->packInfo = generatePackInfo(the_list, compressed_size);
    streams->codersInfo = generateCodersInfo(the_list);
    streams->substreamsInfo = generateSubstreamsInfo(the_list);
    return streams;
}

Qz7zArchiveProperty_T *generatePropertyInfo()
{
    Qz7zArchiveProperty_T *property = malloc(sizeof(Qz7zArchiveProperty_T));
    CHECK_ALLOC_RETURN_VALUE(property)

    property->id = QZ7Z_PROPERTY_ID_INTEL7Z_1001;
    property->size = sizeof(g_property_data);
    property->data = malloc(property->size * sizeof(unsigned char));
    memcpy(property->data, g_property_data, property->size);
    property->next = NULL;
    return property;
}

Qz7zEndHeader_T *generateEndHeader(Qz7zItemList_T *the_list,
                                   size_t compressed_size)
{
    Qz7zEndHeader_T *header = malloc(sizeof(Qz7zEndHeader_T));
    CHECK_ALLOC_RETURN_VALUE(header)

    header->propertyInfo = generatePropertyInfo();
    header->streamsInfo = generateStreamsInfo(the_list, compressed_size);
    header->filesInfo = generateFilesInfo(the_list);
    return header;
}

QzCatagoryTable_T *createCatagoryList()
{
    QzCatagoryTable_T  *cat_tbl;
    cat_tbl = malloc(sizeof(QzCatagoryTable_T));
    CHECK_ALLOC_RETURN_VALUE(cat_tbl)

    cat_tbl->cat_num = sizeof(g_catagory_names) / sizeof(g_catagory_names[0]);

    cat_tbl->catas = malloc(cat_tbl->cat_num * sizeof(QzCatagory_T));
    CHECK_ALLOC_RETURN_VALUE(cat_tbl->catas)

    // the last one for all other files
    for (int i = 0; i < cat_tbl->cat_num; ++i) {
        cat_tbl->catas[i].cat_id = i;
        cat_tbl->catas[i].cat_name = g_catagory_names[i];
        cat_tbl->catas[i].cat_files = qzListCreate(1000);
    }

    return cat_tbl;
}

int getCatagory(QzCatagoryTable_T *tbl, Qz7zFileItem_T *p)
{
    return 0;
}

/*
 * return 0 sucess
 *       -1  failed
 */
int scanFilesIntoCatagory(Qz7zItemList_T *the_list)
{
    int cat_index;
    QzListHead_T *files = the_list->items[1];
    QzCatagory_T *cat = the_list->table->catas;

    for (int i = 0; i < files->total; ++i) {
        Qz7zFileItem_T *p = qzListGet(files, i);
        // decide the catagory for the fileitem
        cat_index = getCatagory(the_list->table, p);
        // add to the catagory list
        qzListAdd(cat[cat_index].cat_files, (void **)&p);
    }
    return 0;
}

int qz7zWriteSignatureHeader(Qz7zSignatureHeader_T *header, FILE *fp)
{
    size_t n;
    n = fwrite(header->signature, 1, sizeof(header->signature), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->signature))
    n = fwrite(&header->majorVersion, 1, sizeof(header->majorVersion), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->majorVersion))
    n = fwrite(&header->minorVersion, 1, sizeof(header->minorVersion), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->minorVersion))
    n = fwrite(&header->startHeaderCRC, 1, sizeof(header->startHeaderCRC), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->startHeaderCRC))
    n = fwrite(&header->nextHeaderOffset, 1, sizeof(header->nextHeaderOffset),
               fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->nextHeaderOffset))
    n = fwrite(&header->nextHeaderSize, 1, sizeof(header->nextHeaderSize), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->nextHeaderSize))
    n = fwrite(&header->nextHeaderCRC, 1, sizeof(header->nextHeaderCRC), fp);
    CHECK_FWRITE_RETURN(n, sizeof(header->nextHeaderCRC))
    return QZ7Z_OK;
}

/*
 * write property structure to file
 * if crc is not null, return the crc of the bytes that have written
 * return the bytes of written
 */
size_t qz7zWriteArchiveProperties(Qz7zArchiveProperty_T *property, FILE *fp,
                                  uint32_t *crc)
{
    size_t size;
    size_t total_size = 0;
    total_size += writeTag(PROPERTY_ID_ARCHIVE_PROPERTIES, fp, crc);
    total_size += writeNumber(property->id, fp, crc);
    total_size += writeNumber(property->size, fp, crc);
    size = fwrite(property->data, 1, property->size, fp);
    CHECK_FWRITE_RETURN(size, property->size)
    *crc = crc32(*crc, property->data, property->size);
    total_size += size;
    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

size_t qz7zWritePackInfo(Qz7zPackInfo_T *pack, FILE *fp, uint32_t *crc)
{
    int i;
    size_t total_size = 0;

    total_size += writeTag(PROPERTY_ID_PACKINFO, fp, crc);
    total_size += writeNumber(pack->PackPos, fp, crc);
    total_size += writeNumber(pack->NumPackStreams, fp, crc);
    total_size += writeTag(PROPERTY_ID_SIZE, fp, crc);
    for (i = 0; i < pack->NumPackStreams; ++i) {
        total_size += writeNumber(pack->PackSize[i], fp, crc);
    }
    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

size_t qz7zWriteFolder(Qz7zFolderInfo_T *folder, FILE *fp, uint32_t *crc)
{
    int i;
    size_t size;
    size_t total_size = 0;
    Qz7zCoder_T *coder = folder->coder_list;

    total_size += writeNumber(folder->numCoders, fp, crc);
    for (i = 0; i < folder->numCoders; ++i) {
        total_size += writeByte(coder->coderFirstByte.uc, fp, crc);
        size = fwrite(coder->codecID, sizeof(unsigned char),
                      coder->coderFirstByte.st.CodecIdSize, fp);
        CHECK_FWRITE_RETURN(size, coder->coderFirstByte.st.CodecIdSize)
        total_size += size;
        *crc = crc32(*crc, coder->codecID,
                     coder->coderFirstByte.st.CodecIdSize);
    }
    return total_size;
}

size_t qz7zWriteCodersInfo(Qz7zCodersInfo_T *coders, FILE *fp, uint32_t *crc)
{
    int i;
    size_t total_size = 0;

    total_size += writeTag(PROPERTY_ID_UNPACKINFO, fp, crc);
    total_size += writeTag(PROPERTY_ID_FOLDER, fp, crc);
    total_size += writeNumber(coders->numFolders, fp, crc);
    total_size += writeByte(0, fp, crc); // external = 0

    for (i = 0; i < coders->numFolders; ++i) {
        total_size += qz7zWriteFolder(&coders->folders[i], fp, crc);
    }

    total_size += writeTag(PROPERTY_ID_CODERS_UNPACK_SIZE, fp, crc);
    for (i = 0; i < coders->numFolders; ++i) {
        total_size += writeNumber(coders->unPackSize[i], fp, crc);
    }

    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

size_t qz7zWriteDigestInfo(Qz7zDigest_T *digest, FILE *fp, uint32_t *crc)
{
    int i;
    size_t size;
    size_t total_size = 0;
    total_size += writeByte(digest->allAreDefined, fp, crc);

    for (i = 0; i < digest->numStreams; ++i) {
        size = fwrite(&digest->crc[i], sizeof(digest->crc[i]), 1, fp);
        CHECK_FWRITE_RETURN(size, 1)
        total_size += size * sizeof(digest->crc[i]);
        *crc = crc32(*crc, (unsigned char *)&digest->crc[i],
                     sizeof(digest->crc[i]));
    }
    return total_size;
}

size_t qz7zWriteSubstreamsInfo(Qz7zSubstreamsInfo_T *substreams, FILE *fp,
                               uint32_t *crc)
{
    int i, j;
    int total_index = 0;
    int total_files = 0;
    size_t total_size = 0;

    if (!substreams) return 0;

    total_size += writeTag(PROPERTY_ID_SUBSTREAMSINFO, fp, crc);
    total_size += writeTag(PROPERTY_ID_NUM_UNPACK_STREAM, fp, crc);

    for (i = 0; i < substreams->numFolders; ++i) {
        total_files += substreams->numUnPackStreams[i];
        total_size += writeNumber(substreams->numUnPackStreams[i], fp, crc);
    }

    total_size += writeTag(PROPERTY_ID_SIZE, fp, crc);
    for (i = 0; i < substreams->numFolders; ++i) {
        for (j = 0; j < substreams->numUnPackStreams[i] - 1; ++j) {
            total_size += writeNumber(substreams->unPackSize[total_index++], fp,
                                      crc);
        }
    }

    total_size += writeTag(PROPERTY_ID_CRC, fp, crc);
    total_size += qz7zWriteDigestInfo(substreams->digests, fp, crc);

    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

size_t qz7zWriteStreamsInfo(Qz7zStreamsInfo_T *streams, FILE *fp,
                            uint32_t *crc)
{
    size_t total_size = 0;
    if (!streams) return 0;

    total_size += writeTag(PROPERTY_ID_MAIN_STREAMSINFO, fp, crc);
    total_size += qz7zWritePackInfo(streams->packInfo, fp, crc);
    total_size += qz7zWriteCodersInfo(streams->codersInfo, fp, crc);
    total_size += qz7zWriteSubstreamsInfo(streams->substreamsInfo, fp, crc);
    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

unsigned char *calculateEmptyStreamProperty(int m, int n, uint64_t *size)
{
    unsigned char *res;
    unsigned char sum = 0;
    *size = (!((m + n) % 8)) ? (m + n) / 8 : (m + n) / 8 + 1;
    res = malloc(*size);
    CHECK_ALLOC_RETURN_VALUE(res)
    memset(res, 0, *size);

    int i, j = 0, k;
    for (i = 0; i < m / 8; ++i) {
        res[j++] = 0xff;
    }
    for (i = 0, k = 7; i < m % 8; ++i, --k) {
        sum += 1 << k;
    }
    res[j++] = sum;
    return res;
}

unsigned char *calculateEmptyFileProperty(Qz7zFilesInfo_T *files,
        int m,
        uint64_t *size)
{
    int i, j;
    *size = (!((m) % 8)) ? (m) / 8 : ((m) / 8 + 1);
    unsigned char *res = malloc(*size);
    memset(res, 0, *size);
    int total_index = 0;

    for (i = 0; i < *size; ++i) {
        for (j = 7; j >= 0; --j) {
            Qz7zFileItem_T *p = qzListGet(files->head[0], total_index++);
            if (p->isEmpty) {
                QZ_DEBUG("%s is empty stream: i=%d j=%d\n", p->fileName, i, j);
                res[i] += 1 << j;
            }
            if (total_index == m) break;
        }
    }
    return res;
}

unsigned char *genNamesPart(QzListHead_T *head_dir, QzListHead_T *head_file,
                            uint64_t *size)
{
    int i;
    int j; // for buf index
    int k;
    uint64_t n_dir = head_dir->total;
    uint64_t n_file = head_file->total;
    Qz7zFileItem_T *p;
    unsigned char *buf;
    int buf_len = 0;

    for (i = 0; i < n_dir; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_dir, i);
        buf_len += 2 * p->nameLength;
    }
    for (i = 0; i < n_file; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_file, i);
        buf_len += 2 * p->nameLength;
    }
    buf = malloc(buf_len + 1);
    CHECK_ALLOC_RETURN_VALUE(buf)

    k = 0;
    buf[k++] = 0;
    for (i = 0; i < n_dir; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_dir, i);
        for (j = 0; j < p->nameLength; ++j) {
            buf[k++] = p->fileName[j];
            buf[k++] = 0;
        }
    }
    for (i = 0; i < n_file; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_file, i);
        for (j = 0; j < p->nameLength; ++j) {
            buf[k++] = p->fileName[j];
            buf[k++] = 0;
        }
    }

    *size = k;
    return buf;
}

uint32_t *genAttributes(QzListHead_T *head_dir, QzListHead_T *head_file)
{
    int i;
    int k;
    uint64_t n_dir = head_dir->total;
    uint64_t n_file = head_file->total;
    Qz7zFileItem_T *p;
    uint32_t *buf;

    buf = malloc(sizeof(((Qz7zFileItem_T *)0)->attribute) * (n_dir + n_file));
    CHECK_ALLOC_RETURN_VALUE(buf)

    k = 0;
    for (i = 0; i < n_dir; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_dir, i);
        buf[k++] = p->attribute;
    }
    for (i = 0; i < n_file; ++i) {
        p = (Qz7zFileItem_T *)qzListGet(head_file, i);
        buf[k++] = p->attribute;
    }

    return buf;
}

static int checkZerobyteFiles(Qz7zFilesInfo_T *files)
{
    Qz7zFileItem_T *pfile;
    int i;
    int n = files->head[0]->total;
    for (i = 0; i < n; ++i) {
        pfile = qzListGet(files->head[0], i);
        if (pfile->isEmpty) return 1;
    }
    return 0;
}

size_t qz7zWriteFilesInfo(Qz7zFilesInfo_T *files, FILE *fp, uint32_t *crc)
{
    int n_emptystream = files->head[0]->total;
    int n_file = files->head[1]->total;
    uint64_t size = 0;
    size_t ret_size;
    unsigned char *p;
    uint64_t total_size = 0;
    unsigned char *buf;
    uint32_t *attributes;
    int has_zerobyte_file = 0;

    total_size += writeTag(PROPERTY_ID_FILESINFO, fp, crc);
    total_size += writeNumber(files->num, fp, crc);
    total_size += writeTag(PROPERTY_ID_EMPTY_STREAM, fp, crc);
    p = calculateEmptyStreamProperty(n_emptystream, n_file, &size);
    total_size += writeNumber(size, fp, crc);
    ret_size = fwrite(p, 1, size, fp);
    CHECK_FWRITE_RETURN(size, ret_size)
    total_size += ret_size;
    *crc = crc32(*crc, p, size);
    free(p);
    p = NULL;
    has_zerobyte_file =  checkZerobyteFiles(files);

    if (n_emptystream && has_zerobyte_file) {
        total_size += writeTag(PROPERTY_ID_EMPTY_FILE, fp, crc);
        p = calculateEmptyFileProperty(files, n_emptystream, &size);
        total_size += writeNumber(size, fp, crc);
        ret_size = fwrite(p, 1, size, fp);
        CHECK_FWRITE_RETURN(size, ret_size)
        total_size += ret_size;
        *crc = crc32(*crc, p, size);
        free(p);
        p = NULL;
    }

    total_size += writeTag(PROPERTY_ID_DUMMY, fp, crc);
    total_size += writeNumber(2, fp, crc);
    total_size += writeByte(PROPERTY_CONTENT_DUMMY, fp, crc);
    total_size += writeByte(PROPERTY_CONTENT_DUMMY, fp, crc);

    total_size += writeTag(PROPERTY_ID_NAME, fp, crc);
    buf = genNamesPart(files->head[0], files->head[1], &size);
    total_size += writeNumber(size, fp, crc);
    ret_size = fwrite(buf, 1, size, fp);
    CHECK_FWRITE_RETURN(ret_size, size)
    total_size += ret_size;
    *crc = crc32(*crc, buf, size);
    free(buf);

    int i;
    Qz7zFileItem_T *pfile;
    FILETIME_T win_time;
    total_size += writeTag(PROPERTY_ID_MTIME, fp, crc);
    total_size += writeNumber((n_emptystream + n_file) * 8 + 2, fp, crc);
    total_size += writeTag(1, fp, crc);
    total_size += writeTag(0, fp, crc);

    for (i = 0; i < n_emptystream; ++i) {
        pfile = (Qz7zFileItem_T *)qzListGet(files->head[0], i);
        win_time = unixtimeToFiletime(pfile->mtime, pfile->mtime_nano);
        total_size += writeTime(win_time.low, fp, crc);
        total_size += writeTime(win_time.high, fp, crc);
    }

    for (i = 0; i < n_file; ++i) {
        pfile = (Qz7zFileItem_T *)qzListGet(files->head[1], i);
        win_time = unixtimeToFiletime(pfile->mtime, pfile->mtime_nano);
        total_size += writeTime(win_time.low, fp, crc);
        total_size += writeTime(win_time.high, fp, crc);
    }

    total_size += writeTag(PROPERTY_ID_ATTRIBUTES, fp, crc);
    attributes = genAttributes(files->head[0], files->head[1]);
    size = 2 + 4 * (n_emptystream + n_file);
    total_size += writeNumber(size, fp, crc);
    total_size += writeByte(FLAG_ATTR_DEFINED_SET, fp, crc);
    total_size += writeByte(FLAG_ATTR_EXTERNAL_UNSET, fp, crc);
    for (i = 0; i < n_emptystream + n_file; ++i) {
        total_size += writeTime(attributes[i], fp, crc);
    }
    free(attributes);

    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

/**
 * return total size that has written
 */
size_t qz7zWriteEndHeader(Qz7zEndHeader_T *header, FILE *fp, uint32_t *crc)
{
    size_t total_size = 0;
    total_size += writeTag(PROPERTY_ID_HEADER, fp, crc);
    total_size += qz7zWriteArchiveProperties(header->propertyInfo, fp, crc);
    total_size += qz7zWriteStreamsInfo(header->streamsInfo, fp, crc);
    total_size += qz7zWriteFilesInfo(header->filesInfo, fp, crc);
    total_size += writeTag(PROPERTY_ID_END, fp, crc);
    return total_size;
}

Qz7zItemList_T *processInputParams(int n, char **files)
{
    int      i;
    uint32_t dir_index = 0;
    // the index of next processing directory in the dir list

    Qz7zItemList_T *res = malloc(sizeof(Qz7zItemList_T));
    CHECK_ALLOC_RETURN_VALUE(res)

    res->items[0] = qzListCreate(QZ_DIRLIST_DEFAULT_NUM_PER_NODE);
    res->items[1] = qzListCreate(QZ_FILELIST_DEFAULT_NUM_PER_NODE);

    QzListHead_T *dirs_head = res->items[0];
    QZ_DEBUG("dirs_head : %p\n", (void *)dirs_head);
    QzListHead_T *files_head = res->items[1];
    QZ_DEBUG("files_head : %p\n", (void *)files_head);

    for (i = 0; i < n; ++i) {
#ifdef QZ7Z_DEBUG
        QZ_DEBUG("process %dth parameter: %s\n", i + 1, files[optind]);
#endif

        if (access(files[optind], F_OK)) {
            QZ_ERROR("%s: No such file or directory\n", files[optind]);
            return NULL;
        }

        Qz7zFileItem_T *fi = qzFileItemCreate(files[optind]);

        if (fi->isDir || fi->isEmpty) {
            qzListAdd(dirs_head, (void **)&fi);  // add it to directory list

            while (dir_index < dirs_head->total) {
                Qz7zFileItem_T  *processing =
                    (Qz7zFileItem_T *)qzListGet(dirs_head, dir_index);

                if (processing == NULL) {
                    QZ_DEBUG("qzListGet got NULL!\n");
                    return NULL;
                }
                QZ_DEBUG("processing: %s\n", processing->fileName);

                if (processing->isDir) {
                    struct dirent *dentry;
                    DIR *dirp = opendir(processing->fileName);
                    if (!dirp) {
                        QZ_ERROR("errors ocurs: %s \n", strerror(errno));
                        return NULL;
                    }

                    while ((dentry = readdir(dirp))) {
                        char file_path[PATH_MAX + 1];
                        memset(file_path, 0, PATH_MAX + 1);

                        if (!strcmp(dentry->d_name, ".")) continue;
                        if (!strcmp(dentry->d_name, "..")) continue;

                        snprintf(file_path, sizeof file_path, "%s/%s",
                                 processing->fileName, dentry->d_name);
                        QZ_DEBUG(" file_path: %s\n", file_path);
                        Qz7zFileItem_T *anotherfile =
                            qzFileItemCreate(file_path);

                        if (anotherfile->isDir || anotherfile->isEmpty) {
                            qzListAdd(dirs_head, (void **)&anotherfile);
                        } else {
                            qzListAdd(files_head, (void **)&anotherfile);
                        }
                    }
                }
                dir_index++;
            }
        } else {
            qzListAdd(files_head, (void **)&fi);
        }
        optind++;
    }// end for

    /* now the res->items has been resolved successfully */
    res->table = createCatagoryList();
    scanFilesIntoCatagory(res);

    return res;
}

/**
 * return 1 if 7z archive is good, others if not
 */
int check7zArchive(const char *archive)
{
    int           sig_wrong;
    size_t        n;
    unsigned char buf[8];
    FILE          *fp = fopen(archive, "r");

    if (!fp) {
        perror(archive);
        return QZ7Z_ERR_OPEN;
    }
    n = fread(buf, 1, sizeof(buf), fp);
    if (n < sizeof(buf)) return -1;

    if ((sig_wrong =
             memcmp(buf, g_header_signature, sizeof(g_header_signature)))) {
        QZ_ERROR("The archive signature header is broken\n");
        return QZ7Z_ERR_SIG_HEADER_BROKEN;
    }

    if (buf[6] != 0 || buf[7] != 4) {
        QZ_DEBUG("Warning: The 7z archive version is not 0.4\n");
        QZ_DEBUG("Warning: Maybe has some issues for other version\n");
    }
    fclose(fp);

    return 1;
}

/*
 * 1 if filename is dir, 0 if not
 */
int checkDirectory(const char *filename)
{
    struct stat buf;
    if (stat(filename, &buf) < 0) {
        QZ_ERROR("stat function error\n");
        return QZ7Z_ERR_STAT;
    }
    if (S_ISDIR(buf.st_mode)) {
        return 1;
    } else {
        return 0;
    }
}
