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
#include "qzip.h"

char const *const g_license_msg[] = {
    "Copyright (C) 2021 Intel Corporation.",
    0
};

char *g_program_name = NULL; /* program name */
int g_decompress = 0;        /* g_decompress (-d) */
int g_keep = 0;                     /* keep (don't delete) input files */
QzSession_T g_sess;
QzipParams_T g_params_th = {
    .huffman_hdr = QZ_HUFF_HDR_DEFAULT,
    .direction = QZ_DIRECTION_DEFAULT,
    .data_fmt = QZIP_DEFLATE_GZIP_EXT,
    .comp_lvl = QZ_COMP_LEVEL_DEFAULT,
    .comp_algorithm = QZ_COMP_ALGOL_DEFAULT,
    .hw_buff_sz = QZ_HW_BUFF_SZ,
    .polling_mode = QZ_PERIODICAL_POLLING,
    .req_cnt_thrshold = 32
};

/* Estimate maximum data expansion after decompression */
const unsigned int g_bufsz_expansion_ratio[] = {5, 20, 50, 100};

/* Command line options*/
char const g_short_opts[] = "A:H:L:C:r:o:O:P:dfhkVR";
const struct option g_long_opts[] = {
    /* { name  has_arg  *flag  val } */
    {"decompress", 0, 0, 'd'}, /* decompress */
    {"uncompress", 0, 0, 'd'}, /* decompress */
    {"force",      0, 0, 'f'}, /* force overwrite of output file */
    {"help",       0, 0, 'h'}, /* give help */
    {"keep",       0, 0, 'k'}, /* keep (don't delete) input files */
    {"version",    0, 0, 'V'}, /* display version number */
    {"algorithm",  1, 0, 'A'}, /* set algorithm type */
    {"huffmanhdr", 1, 0, 'H'}, /* set huffman header type */
    {"level",      1, 0, 'L'}, /* set compression level */
    {"chunksz",    1, 0, 'C'}, /* set chunk size */
    {"output",     1, 0, 'O'}, /* set output header format(gzip, gzipext, 7z,
                                  deflate_4B, lz4, lz4s) */
    {"recursive",  0, 0, 'R'}, /* set recursive mode when compressing a
                                  directory */
    {"polling",    1, 0, 'P'}, /* set polling mode when compressing and
                                  decompressing */
    { 0, 0, 0, 0 }
};

const unsigned int USDM_ALLOC_MAX_SZ = (2 * 1024 * 1024 - 5 * 1024);


void tryHelp(void)
{
    QZ_PRINT("Try `%s --help' for more information.\n", g_program_name);
    exit(ERROR);
}

void help(void)
{
    static char const *const help_msg[] = {
        "Compress or uncompress FILEs (by default, compress FILES in-place).",
        "",
        "Mandatory arguments to long options are mandatory for short options "
        "too.",
        "",
        "  -A, --algorithm   set algorithm type",
        "  -d, --decompress  decompress",
        "  -f, --force       force overwrite of output file and compress links",
        "  -h, --help        give this help",
        "  -H, --huffmanhdr  set huffman header type",
        "  -k, --keep        keep (don't delete) input files",
        "  -V, --version     display version number",
        "  -L, --level       set compression level",
        "  -C, --chunksz     set chunk size",
        "  -O, --output      set output header format(gzip|gzipext|7z|deflate_4B|lz4|lz4s)",
        "  -r,               set max in-flight request number",
        "  -R,               set Recursive mode for a directory",
        "  -o,               set output file name",
        "  -P, --polling     set polling mode, only supports busy polling settings",
        "",
        "With no FILE, read standard input.",
        0
    };
    char const *const *p = help_msg;

    QZ_PRINT("Usage: %s [OPTION]... [FILE]...\n", g_program_name);
    while (*p) {
        QZ_PRINT("%s\n", *p++);
    }
}

void freeTimeList(RunTimeList_T *time_list)
{
    RunTimeList_T *time_node = time_list;
    RunTimeList_T *pre_time_node = NULL;

    while (time_node) {
        pre_time_node = time_node;
        time_node = time_node->next;
        free(pre_time_node);
    }
}

void displayStats(RunTimeList_T *time_list,
                  off_t insize, off_t outsize, int is_compress)
{
    /* Calculate time taken (from begin to end) in micro seconds */
    unsigned long us_begin = 0;
    unsigned long us_end = 0;
    double us_diff = 0;
    RunTimeList_T *time_node = time_list;

    while (time_node) {
        us_begin = time_node->time_s.tv_sec * 1000000 +
                   time_node->time_s.tv_usec;
        us_end = time_node->time_e.tv_sec * 1000000 + time_node->time_e.tv_usec;
        us_diff += (us_end - us_begin);
        time_node = time_node->next;
    }

    if (insize) {
        assert(0 != us_diff);
        double size = (is_compress) ? insize : outsize;
        double throughput = (size * CHAR_BIT) / us_diff; /* in MB (megabytes) */
        double compressionRatio = ((double)insize) / ((double)outsize);
        double spaceSavings = 1 - ((double)outsize) / ((double)insize);

        QZ_PRINT("Time taken:    %9.3lf ms\n", us_diff / 1000);
        QZ_PRINT("Throughput:    %9.3lf Mbit/s\n", throughput);
        if (is_compress) {
            QZ_PRINT("Space Savings: %9.3lf %%\n", spaceSavings * 100.0);
            QZ_PRINT("Compression ratio: %.3lf : 1\n", compressionRatio);
        }
    }
}

int doProcessBuffer(QzSession_T *sess,
                    unsigned char *src, unsigned int *src_len,
                    unsigned char *dst, unsigned int dst_len,
                    RunTimeList_T *time_list, FILE *dst_file,
                    off_t *dst_file_size, int is_compress)
{
    int ret = QZ_FAIL;
    unsigned int done = 0;
    unsigned int buf_processed = 0;
    unsigned int buf_remaining = *src_len;
    unsigned int bytes_written = 0;
    unsigned int valid_dst_buf_len = dst_len;
    RunTimeList_T *time_node = time_list;


    while (time_node->next) {
        time_node = time_node->next;
    }

    while (!done) {
        RunTimeList_T *run_time = calloc(1, sizeof(RunTimeList_T));
        assert(NULL != run_time);
        run_time->next = NULL;
        time_node->next = run_time;
        time_node = run_time;

        gettimeofday(&run_time->time_s, NULL);

        /* Do actual work */
        if (is_compress) {
            ret = qzCompress(sess, src, src_len, dst, &dst_len, 1);
            if (QZ_BUF_ERROR == ret && 0 == *src_len) {
                done = 1;
            }
        } else {
            ret = qzDecompress(sess, src, src_len, dst, &dst_len);

            if (QZ_DATA_ERROR == ret ||
                (QZ_BUF_ERROR == ret && 0 == *src_len)) {
                done = 1;
            }
        }

        if (ret != QZ_OK &&
            ret != QZ_BUF_ERROR &&
            ret != QZ_DATA_ERROR) {
            const char *op = (is_compress) ? "Compression" : "Decompression";
            QZ_ERROR("doProcessBuffer:%s failed with error: %d\n", op, ret);
            break;
        }

        gettimeofday(&run_time->time_e, NULL);

        bytes_written = fwrite(dst, 1, dst_len, dst_file);
        assert(bytes_written == dst_len);
        *dst_file_size += bytes_written;

        buf_processed += *src_len;
        buf_remaining -= *src_len;
        if (0 == buf_remaining) {
            done = 1;
        }
        src += *src_len;
        QZ_DEBUG("src_len is %u ,buf_remaining is %u\n", *src_len,
                 buf_remaining);
        *src_len = buf_remaining;
        dst_len = valid_dst_buf_len;
        bytes_written = 0;
    }

    *src_len = buf_processed;
    return ret;
}

void doProcessFile(QzSession_T *sess, const char *src_file_name,
                   const char *dst_file_name, int is_compress)
{
    int ret = OK;
    struct stat src_file_stat;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    off_t src_file_size = 0, dst_file_size = 0, file_remaining = 0;
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;
    FILE *src_file = NULL;
    FILE *dst_file = NULL;
    unsigned int bytes_read = 0;
    unsigned long bytes_processed = 0;
    unsigned int ratio_idx = 0;
    const unsigned int ratio_limit =
        sizeof(g_bufsz_expansion_ratio) / sizeof(unsigned int);
    unsigned int read_more = 0;
    int src_fd = 0;
    RunTimeList_T *time_list_head = malloc(sizeof(RunTimeList_T));
    assert(NULL != time_list_head);
    gettimeofday(&time_list_head->time_s, NULL);
    time_list_head->time_e = time_list_head->time_s;
    time_list_head->next = NULL;

    //open file
    src_fd = open(src_file_name, O_RDONLY);
    if (src_fd < 0) {
        QZ_PRINT("Open input file %s failed\n", src_file_name);
        exit(ERROR);
    }
    ret = fstat(src_fd, &src_file_stat);
    assert(!ret);

    if (S_ISBLK(src_file_stat.st_mode)) {
        /* ioctl return device size / 512, so device size = src_file_size * 512 */
        if (ioctl(src_fd, BLKGETSIZE, &src_file_size) < 0) {
            close(src_fd);
            perror(src_file_name);
            exit(ERROR);
        }
        src_file_size *= 512;
    } else {
        src_file_size = src_file_stat.st_size;
    }
    src_buffer_size = (src_file_size > SRC_BUFF_LEN) ?
                      SRC_BUFF_LEN : src_file_size;
    if (is_compress) {
        dst_buffer_size = qzMaxCompressedLength(src_buffer_size, sess);
        if (0 == dst_buffer_size) {
            perror("During SW compression, src file size is less than HW size!\n");
            exit(ERROR);
        }
    } else { /* decompress */
        dst_buffer_size = src_buffer_size *
                          g_bufsz_expansion_ratio[ratio_idx++];
    }

    if (0 == src_file_size && is_compress) {
        dst_buffer_size = 1024;
    }

    src_buffer = malloc(src_buffer_size);
    assert(src_buffer != NULL);
    dst_buffer = malloc(dst_buffer_size);
    assert(dst_buffer != NULL);
    src_file = fdopen(src_fd, "r");
    assert(src_file != NULL);
    dst_file = fopen(dst_file_name, "w");
    assert(dst_file != NULL);

    file_remaining = src_file_size;
    read_more = 1;
    do {
        if (read_more) {
            bytes_read = fread(src_buffer, 1, src_buffer_size, src_file);
            QZ_PRINT("Reading input file %s (%u Bytes)\n", src_file_name,
                     bytes_read);
        } else {
            bytes_read = file_remaining;
        }

        puts((is_compress) ? "Compressing..." : "Decompressing...");

        ret = doProcessBuffer(sess, src_buffer, &bytes_read, dst_buffer,
                              dst_buffer_size, time_list_head, dst_file,
                              &dst_file_size, is_compress);

        if (QZ_DATA_ERROR == ret || QZ_BUF_ERROR == ret) {
            bytes_processed += bytes_read;
            if (0 != bytes_read) {
                if (-1 == fseek(src_file, bytes_processed, SEEK_SET)) {
                    ret = ERROR;
                    goto exit;
                }
                read_more = 1;
            } else if (QZ_BUF_ERROR == ret) {
                //dest buffer not long enough
                if (ratio_limit == ratio_idx) {
                    QZ_ERROR("Could not expand more destination buffer\n");
                    ret = ERROR;
                    goto exit;
                }

                free(dst_buffer);
                dst_buffer_size = src_buffer_size *
                                  g_bufsz_expansion_ratio[ratio_idx++];
                dst_buffer = malloc(dst_buffer_size);
                if (NULL == dst_buffer) {
                    QZ_ERROR("Fail to allocate destination buffer with size "
                             "%u\n", dst_buffer_size);
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
        }

        file_remaining -= bytes_read;
    } while (file_remaining > 0);

    displayStats(time_list_head, src_file_size, dst_file_size, is_compress);

exit:
    freeTimeList(time_list_head);
    fclose(src_file);
    fclose(dst_file);
    close(src_fd);
    free(src_buffer);
    free(dst_buffer);
    if (!g_keep && OK == ret) {
        unlink(src_file_name);
    }
    if (ret) {
        exit(ret);
    }
}
int qzipSetupSessionDeflate(QzSession_T *sess, QzipParams_T *params)
{
    int status;
    QzSessionParamsDeflate_T deflate_params;

    status = qzGetDefaultsDeflate(&deflate_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    switch (params->data_fmt) {
    case QZIP_DEFLATE_4B:
        deflate_params.data_fmt = QZ_DEFLATE_4B;
        break;
    case QZIP_DEFLATE_GZIP:
        deflate_params.data_fmt = QZ_DEFLATE_GZIP;
        break;
    case QZIP_DEFLATE_GZIP_EXT:
        deflate_params.data_fmt = QZ_DEFLATE_GZIP_EXT;
        break;
    case QZIP_DEFLATE_RAW:
        deflate_params.data_fmt = QZ_DEFLATE_RAW;
        break;
    default:
        QZ_ERROR("Unsupported data format\n");
        return ERROR;
    }

    deflate_params.huffman_hdr = params->huffman_hdr;
    deflate_params.common_params.direction = params->direction;
    deflate_params.common_params.comp_lvl = params->comp_lvl;
    deflate_params.common_params.comp_algorithm = params->comp_algorithm;
    deflate_params.common_params.hw_buff_sz = params->hw_buff_sz;
    deflate_params.common_params.polling_mode = params->polling_mode;
    deflate_params.common_params.req_cnt_thrshold = params->req_cnt_thrshold;

    status = qzSetupSessionDeflate(sess, &deflate_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    return OK;
}

int qzipSetupSessionLZ4(QzSession_T *sess, QzipParams_T *params)
{
    int status;
    QzSessionParamsLZ4_T lz4_params;

    status = qzGetDefaultsLZ4(&lz4_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    lz4_params.common_params.direction = params->direction;
    lz4_params.common_params.comp_lvl = params->comp_lvl;
    lz4_params.common_params.comp_algorithm = params->comp_algorithm;
    lz4_params.common_params.hw_buff_sz = params->hw_buff_sz;
    lz4_params.common_params.polling_mode = params->polling_mode;
    lz4_params.common_params.req_cnt_thrshold = params->req_cnt_thrshold;

    status = qzSetupSessionLZ4(sess, &lz4_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    return OK;
}

int qzipSetupSessionLZ4S(QzSession_T *sess, QzipParams_T *params)
{
    int status;
    QzSessionParamsLZ4S_T lz4s_params;

    status = qzGetDefaultsLZ4S(&lz4s_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    lz4s_params.common_params.direction = params->direction;
    lz4s_params.common_params.comp_lvl = params->comp_lvl;
    lz4s_params.common_params.comp_algorithm = params->comp_algorithm;
    lz4s_params.common_params.hw_buff_sz = params->hw_buff_sz;
    lz4s_params.common_params.polling_mode = params->polling_mode;
    lz4s_params.common_params.req_cnt_thrshold = params->req_cnt_thrshold;

    status = qzSetupSessionLZ4S(sess, &lz4s_params);
    if (status < 0) {
        QZ_ERROR("Session setup failed with error: %d\n", status);
        return ERROR;
    }

    return OK;
}

int qatzipSetup(QzSession_T *sess, QzipParams_T *params)
{
    int status;

    QZ_DEBUG("mw>>> sess=%p\n", sess);
    status = qzInit(sess, 1);
    if (status != QZ_OK &&
        status != QZ_DUPLICATE) {
        QZ_ERROR("QAT init failed with error: %d\n", status);
        return ERROR;
    }
    QZ_DEBUG("QAT init OK with error: %d\n", status);

    switch (params->data_fmt) {
    case QZIP_DEFLATE_4B:
    case QZIP_DEFLATE_GZIP:
    case QZIP_DEFLATE_GZIP_EXT:
    case QZIP_DEFLATE_RAW:
        status = qzipSetupSessionDeflate(sess, params);
        if (status != OK) {
            QZ_ERROR("qzipSetupSessionDeflate fail with error: %d\n", status);
        }
        break;
    case QZIP_LZ4_FH:
        status = qzipSetupSessionLZ4(sess, params);
        if (status != OK) {
            QZ_ERROR("qzipSetupSessionLZ4 fail with error: %d\n", status);
        }
        break;
    case QZIP_LZ4S_BK:
        status = qzipSetupSessionLZ4S(sess, params);
        if (status != OK) {
            QZ_ERROR("qzipSetupSessionLZ4S fail with error: %d\n", status);
        }
        break;
    default:
        QZ_ERROR("Unsupported data format\n");
        return ERROR;
    }

    QZ_DEBUG("Session setup OK with error: %d\n", status);
    return 0;
}

int qatzipClose(QzSession_T *sess)
{
    qzTeardownSession(sess);
    qzClose(sess);

    return 0;
}

QzSuffix_T getSuffix(const char *filename)
{
    QzSuffix_T  s = E_SUFFIX_UNKNOWN;
    size_t len = strlen(filename);
    if (len >= strlen(SUFFIX_GZ) &&
        !strcmp(filename + (len - strlen(SUFFIX_GZ)), SUFFIX_GZ)) {
        s = E_SUFFIX_GZ;
    } else if (len >= strlen(SUFFIX_7Z) &&
               !strcmp(filename + (len - strlen(SUFFIX_7Z)), SUFFIX_7Z)) {
        s = E_SUFFIX_7Z;
    }  else if (len >= strlen(SUFFIX_LZ4) &&
                !strcmp(filename + (len - strlen(SUFFIX_LZ4)), SUFFIX_LZ4)) {
        s = E_SUFFIX_LZ4;
    }  else if (len >= strlen(SUFFIX_LZ4S) &&
                !strcmp(filename + (len - strlen(SUFFIX_LZ4S)), SUFFIX_LZ4S)) {
        s = E_SUFFIX_LZ4S;
    }
    return s;
}

bool hasSuffix(const char *fname)
{
    size_t len = strlen(fname);
    switch (g_params_th.data_fmt) {
    case QZIP_LZ4_FH:
        if (len >= strlen(SUFFIX_LZ4) &&
            !strcmp(fname + (len - strlen(SUFFIX_LZ4)), SUFFIX_LZ4)) {
            return 1;
        }
        break;
    case QZIP_LZ4S_BK:
        if (len >= strlen(SUFFIX_LZ4S) &&
            !strcmp(fname + (len - strlen(SUFFIX_LZ4S)), SUFFIX_LZ4S)) {
            return 1;
        }
        break;
    case QZIP_DEFLATE_RAW:
    case QZIP_DEFLATE_GZIP_EXT:
    case QZIP_DEFLATE_GZIP:
    case QZIP_DEFLATE_4B:
    default:
        if (len >= strlen(SUFFIX_GZ) &&
            !strcmp(fname + (len - strlen(SUFFIX_GZ)), SUFFIX_GZ)) {
            return 1;
        } else if (len >= strlen(SUFFIX_7Z) &&
                   !strcmp(fname + (len - strlen(SUFFIX_7Z)), SUFFIX_7Z)) {
            return 1;
        }
        break;
    }
    return 0;
}

QzSuffixCheckStatus_T checkSuffix(QzSuffix_T suffix, int is_format_set)
{
    if (E_SUFFIX_GZ == suffix) {
        if (!is_format_set) {
            // format is not specified, reassign data format by suffix instead of default value
            g_params_th.data_fmt = QZIP_DEFLATE_GZIP_EXT;
            return E_CHECK_SUFFIX_OK;
        }
        if (QZIP_DEFLATE_GZIP_EXT != g_params_th.data_fmt &&
            QZIP_DEFLATE_GZIP != g_params_th.data_fmt &&
            QZIP_DEFLATE_4B != g_params_th.data_fmt) {
            return E_CHECK_SUFFIX_FORMAT_UNMATCH;
        } else {
            return E_CHECK_SUFFIX_OK;
        }
    } else if (E_SUFFIX_7Z == suffix) {
        if (!is_format_set) {
            g_params_th.data_fmt = QZIP_DEFLATE_RAW;
            return E_CHECK_SUFFIX_OK;
        }
        if (QZIP_DEFLATE_RAW != g_params_th.data_fmt) {
            return E_CHECK_SUFFIX_FORMAT_UNMATCH;
        } else {
            return E_CHECK_SUFFIX_OK;
        }
    } else if (E_SUFFIX_LZ4 == suffix) {
        if (!is_format_set) {
            g_params_th.data_fmt = QZIP_LZ4_FH;
            return E_CHECK_SUFFIX_OK;
        }
        if (QZIP_LZ4_FH != g_params_th.data_fmt) {
            return E_CHECK_SUFFIX_FORMAT_UNMATCH;
        } else {
            return E_CHECK_SUFFIX_OK;
        }
    } else {    //unsupported suffix
        return E_CHECK_SUFFIX_UNSUPPORT;
    }
}

int makeOutName(const char *in_name, const char *out_name,
                char *oname, int is_compress)
{
    if (is_compress) {
        if (hasSuffix(in_name)) {
            QZ_ERROR("Warning: %s already has suffix -- unchanged\n",
                     in_name);
            return -1;
        }
        /* add suffix */
        if (g_params_th.data_fmt == QZIP_LZ4_FH) {
            snprintf(oname, MAX_PATH_LEN, "%s%s", out_name ? out_name : in_name,
                     SUFFIX_LZ4);
        } else if (g_params_th.data_fmt == QZIP_LZ4S_BK) {
            snprintf(oname, MAX_PATH_LEN, "%s%s", out_name ? out_name : in_name,
                     SUFFIX_LZ4S);
        } else if (g_params_th.data_fmt == QZIP_DEFLATE_RAW) {
            snprintf(oname, MAX_PATH_LEN, "%s%s", out_name ? out_name : in_name,
                     SUFFIX_7Z);
        } else {
            snprintf(oname, MAX_PATH_LEN, "%s%s", out_name ? out_name : in_name,
                     SUFFIX_GZ);
        }
    } else {
        if (!hasSuffix(in_name)) {
            QZ_ERROR("%s: Wrong suffix. Supported suffix: 7z/gz/lz4\n",
                     in_name);
            return -1;
        }
        /* remove suffix */
        snprintf(oname, MAX_PATH_LEN, "%s", out_name ? out_name : in_name);
        if (NULL == out_name) {
            if (g_params_th.data_fmt == QZIP_LZ4_FH) {
                oname[strlen(in_name) - strlen(SUFFIX_LZ4)] = '\0';
            } else {
                oname[strlen(in_name) - strlen(SUFFIX_GZ)] = '\0';
            }
        }
    }

    return 0;
}

/* Makes a complete file system path by adding a file name to the path of its
 * parent directory. */
void mkPath(char *path, const char *dirpath, char *file)
{
    const int nprinted = snprintf(path, MAX_PATH_LEN, "%s/%s", dirpath, file);
    if (nprinted >= MAX_PATH_LEN || nprinted < 0) {
        /* truncated, or output error */
        assert(0);
    }
}


void processDir(QzSession_T *sess, const char *in_name,
                const char *out_name, int is_compress)
{
    DIR *dir;
    struct dirent *entry;
    char inpath[MAX_PATH_LEN];

    dir = opendir(in_name);
    assert(dir);

    while ((entry = readdir(dir))) {
        /* Ignore anything starting with ".", which includes the special
         * files ".", "..", as well as hidden files. */
        if (entry->d_name[0] == '.') {
            continue;
        }

        /* Qualify the file with its parent directory to obtain a complete
         * path. */
        mkPath(inpath, in_name, entry->d_name);

        processFile(sess, inpath, out_name, is_compress);
    }
    closedir(dir);
}

void processFile(QzSession_T *sess, const char *in_name,
                 const char *out_name, int is_compress)
{
    int ret;
    struct stat fstat;
    struct timespec timebuf[2];

    ret = stat(in_name, &fstat);
    if (ret) {
        perror(in_name);
        exit(-1);
    }

    if (S_ISDIR(fstat.st_mode)) {
        processDir(sess, in_name, out_name, is_compress);
    } else {
        char oname[MAX_PATH_LEN];
        memset(oname, 0, MAX_PATH_LEN);

        if (makeOutName(in_name, out_name, oname, is_compress)) {
            return;
        }
        doProcessFile(sess, in_name, oname, is_compress);

        if (access(oname, F_OK) == 0) {
            //update src file stat to dst file
            memset(timebuf, 0, sizeof(timebuf));
            timebuf[0].tv_nsec = UTIME_NOW;
            timebuf[1].tv_sec = fstat.st_mtime;
            utimensat(AT_FDCWD, oname, timebuf, 0);
        }
    }
}

void version(void)
{
    char const *const *p = g_license_msg;

    QZ_PRINT("%s v%s\n", g_program_name, QZIP_VERSION);
    while (*p) {
        QZ_PRINT("%s\n", *p++);
    }
}

char *qzipBaseName(char *fname)
{
    char *p;

    if ((p = strrchr(fname, '/')) != NULL) {
        fname = p + 1;
    }
    return fname;
}

void processStream(QzSession_T *sess, FILE *src_file, FILE *dst_file,
                   int is_compress)
{
    int ret = OK;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    off_t dst_file_size = 0;
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;
    unsigned int bytes_read = 0;
    unsigned int ratio_idx = 0;
    const unsigned int ratio_limit =
        sizeof(g_bufsz_expansion_ratio) / sizeof(unsigned int);
    unsigned int read_more = 0;
    RunTimeList_T *time_list_head = malloc(sizeof(RunTimeList_T));
    assert(NULL != time_list_head);
    gettimeofday(&time_list_head->time_s, NULL);
    time_list_head->time_e = time_list_head->time_s;
    time_list_head->next = NULL;
    int pending_in = 0;
    int bytes_input = 0;

    src_buffer_size = SRC_BUFF_LEN;
    if (is_compress) {
        dst_buffer_size = qzMaxCompressedLength(src_buffer_size, sess);
        if (0 == dst_buffer_size) {
            perror("During SW compression, src file size is less than HW size!\n");
            exit(ERROR);
        }
    } else { /* decompress */
        dst_buffer_size = src_buffer_size *
                          g_bufsz_expansion_ratio[ratio_idx++];
    }

    src_buffer = malloc(src_buffer_size);
    assert(src_buffer != NULL);
    dst_buffer = malloc(dst_buffer_size);
    assert(dst_buffer != NULL);

    read_more = 1;
    while (!feof(stdin)) {
        if (read_more) {
            bytes_read = fread(src_buffer + pending_in, 1,
                               src_buffer_size - pending_in, src_file);
            if (0 == is_compress) {
                bytes_read += pending_in;
                bytes_input = bytes_read;
                pending_in = 0;
            }
        }

        ret = doProcessBuffer(sess, src_buffer, &bytes_read, dst_buffer,
                              dst_buffer_size, time_list_head, dst_file,
                              &dst_file_size, is_compress);

        if (QZ_DATA_ERROR == ret || QZ_BUF_ERROR == ret) {
            if (!is_compress) {
                pending_in = bytes_input - bytes_read;
            }
            if (0 != bytes_read) {
                if (!is_compress && pending_in > 0) {
                    memmove(src_buffer, src_buffer + bytes_read,
                            src_buffer_size - bytes_read);
                }
                read_more = 1;
            } else if (QZ_BUF_ERROR == ret) {
                // dest buffer not long enough
                if (ratio_limit == ratio_idx) {
                    QZ_ERROR("Could not expand more destination buffer\n");
                    ret = ERROR;
                    goto exit;
                }

                free(dst_buffer);
                dst_buffer_size = src_buffer_size *
                                  g_bufsz_expansion_ratio[ratio_idx++];
                dst_buffer = malloc(dst_buffer_size);
                if (NULL == dst_buffer) {
                    QZ_ERROR("Fail to allocate destination buffer with size "
                             "%u\n", dst_buffer_size);
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
        }
    }

exit:
    freeTimeList(time_list_head);
    free(src_buffer);
    free(dst_buffer);

    if (ret) {
        exit(ret);
    }
}

