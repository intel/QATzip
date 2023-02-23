/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2021-2023 Intel Corporation. All rights reserved.
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

#include "qzstd.h"

QzSession_T qzstd_g_sess;
QzSessionParamsLZ4S_T g_sess_params = {{0}};
QzSessionParamsLZ4S_T sess_params_zstd_default = {
    .common_params.direction         = QZ_DIRECTION_DEFAULT,
    .common_params.comp_lvl          = QZ_COMP_LEVEL_DEFAULT,
    .common_params.comp_algorithm    = QZ_LZ4s,
    .common_params.max_forks         = QZ_MAX_FORK_DEFAULT,
    .common_params.sw_backup         = QZ_SW_BACKUP_DEFAULT,
    .common_params.hw_buff_sz        = QZ_HW_BUFF_SZ,
    .common_params.strm_buff_sz      = QZ_STRM_BUFF_SZ_DEFAULT,
    .common_params.input_sz_thrshold = QZ_COMP_THRESHOLD_DEFAULT,
    .common_params.req_cnt_thrshold  = 32,
    .common_params.wait_cnt_thrshold = QZ_WAIT_CNT_THRESHOLD_DEFAULT,
    .common_params.polling_mode   = QZ_PERIODICAL_POLLING,
    .lz4s_mini_match   = 3,
    .qzCallback        = zstdCallBack,
    .qzCallback_external = NULL
};

static unsigned int LZ4MINMATCH = 2;

void qzstd_help()
{
    static char const *const help_msg[] = {
        "Compress or uncompress a file with zstandard format.",
        "",
        "  -d,       decompress",
        "  -h,       show help information",
        "  -L,       set compression level of QAT",
        "  -o,       set output file name",
        "  -C,       set zstd block size && QAT hw buffer size",
        "  -r,       set max inflight request number",
        "  -P,       set polling mode, only supports busy polling settings",
        "  -m,       set the mini match size for the lz4s search algorithm, \
                     only support mini_match 3 and 4",
        "",
        "Only support one input file.",
        0
    };
    char const *const *p = help_msg;

    while (*p) {
        printf("%s\n", *p++);
    }
}

static double get_time_diff(struct timeval time_e, struct timeval time_s)
{
    unsigned long us_begin = time_s.tv_sec * 1000000 + time_s.tv_usec;
    unsigned long us_end = time_e.tv_sec * 1000000 + time_e.tv_usec;
    return us_end - us_begin;
}

unsigned qzstd_isLittleEndian(void)
{
    const union {
        U32 u;
        BYTE c[4];
    } one = {1}; /* don't use static : performance detrimental */
    return one.c[0];
}

static U16 qzstd_read16(const void *memPtr)
{
    U16 val;
    memcpy(&val, memPtr, sizeof(val));
    return val;
}

U16 qzstd_readLE16(const void *memPtr)
{
    if (qzstd_isLittleEndian()) {
        return qzstd_read16(memPtr);
    } else {
        const BYTE *p = (const BYTE *)memPtr;
        return (U16)((U16)p[0] + (p[1] << 8));
    }
}

void decLz4Block(unsigned char *lz4s, int lz4sSize, ZSTD_Sequence *zstdSeqs,
                 unsigned int *seq_offset)
{
    unsigned char *ip = lz4s;
    unsigned char *endip = lz4s + lz4sSize;

    while (ip < endip && lz4sSize > 0) {
        size_t length = 0;
        size_t offset = 0;
        unsigned int literalLen = 0, matchlen = 0;
        /* get literal length */
        unsigned const token = *ip++;
        if ((length = (token >> ML_BITS)) == RUN_MASK) {
            unsigned s;
            do {
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        literalLen = (unsigned short)length;
        ip += length;
        if (ip == endip) {
            // Meet the end of the LZ4 sequence
            /* update ZSTD_Sequence */
            zstdSeqs[*seq_offset].litLength += literalLen;
            continue;
        }

        /* get matchPos */
        offset = qzstd_readLE16(ip);
        ip += 2;

        /* get match length */
        length = token & ML_MASK;
        if (length == ML_MASK) {
            unsigned s;
            do {
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        if (length != 0) {
            length += LZ4MINMATCH;
            matchlen = (unsigned short)length;

            /* update ZSTD_Sequence */
            zstdSeqs[*seq_offset].offset = offset;
            zstdSeqs[*seq_offset].litLength += literalLen;
            zstdSeqs[*seq_offset].matchLength = matchlen;

            ++(*seq_offset);

            assert(*seq_offset <= ZSTD_SEQUENCES_SIZE);
        } else {
            if (literalLen > 0) {
                /* When match length is 0, the literalLen needs to be temporarily stored
                and processed together with the next data block. If also ip == endip, need
                to convert sequences to seqStore.*/
                zstdSeqs[*seq_offset].litLength += literalLen;
            }
        }
    }
    assert(ip == endip);
}

inline int getLz4FrameHeaderSz()
{
    return QZ_LZ4_HEADER_SIZE;
}

inline int getLz4BlkHeaderSz()
{
    return QZ_LZ4_BLK_HEADER_SIZE;
}

inline int getLZ4FooterSz()
{
    return QZ_LZ4_FOOTER_SIZE;
}

int getContentSize(unsigned char *const ptr)
{
    QzLZ4H_T *hdr = NULL;
    hdr = (QzLZ4H_T *)ptr;
    assert(hdr->magic == QZ_LZ4_MAGIC);
    return hdr->cnt_size;
}

unsigned int getBlockSize(unsigned char *const ptr)
{
    unsigned int blk_sz = *(unsigned int *)ptr;
    return blk_sz;
}

int zstdCallBack(void *external, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len, int *ExtStatus)
{
    int ret = QZ_OK;
    //copied data is used to decode
    //original data will be overwrote by ZSTD_compressSequences
    unsigned char *dest_data = (unsigned char *)malloc(*dest_len);
    assert(dest_data != NULL);
    memcpy(dest_data, dest, *dest_len);
    unsigned char *cur = dest_data;
    unsigned char *end = dest_data + *dest_len;

    ZSTD_Sequence *zstd_seqs = (ZSTD_Sequence *)calloc(ZSTD_SEQUENCES_SIZE,
                               sizeof(ZSTD_Sequence));
    assert(zstd_seqs != NULL);

    ZSTD_CCtx *zc = (ZSTD_CCtx *)external;

    unsigned int produced = 0;
    unsigned int consumed = 0;
    unsigned int cnt_sz = 0, blk_sz = 0;    //content size and block size
    unsigned int dec_offset = 0;
    while (cur < end && *dest_len > 0) {
        //decode block header and get block size
        blk_sz = getBlockSize(cur);
        cur += getLz4BlkHeaderSz();

        //decode lz4s sequences into zstd sequences
        decLz4Block(cur, blk_sz, zstd_seqs, &dec_offset);
        cur += blk_sz;

        cnt_sz = 0;
        for (unsigned int i = 0; i < dec_offset + 1 ; i++) {
            cnt_sz += (zstd_seqs[i].litLength + zstd_seqs[i].matchLength) ;
        }
        assert(cnt_sz <= MAX_BLOCK_SIZE);

        // compress sequence to zstd frame
        int compressed_sz = ZSTD_compressSequences(zc,
                            dest + produced,
                            ZSTD_compressBound(cnt_sz),
                            zstd_seqs,
                            dec_offset + 1,
                            src + consumed,
                            cnt_sz);

        if (compressed_sz < 0) {
            ret = QZ_POST_PROCESS_ERROR;
            *ExtStatus = compressed_sz;
            QZ_ERROR("%s : ZSTD API ZSTD_compressSequences failed with error code, %d, %s\n",
                     ZSTD_ERROR_TYPE, *ExtStatus, DECODE_ZSTD_ERROR_CODE(*ExtStatus));
            goto done;
        }
        //reuse zstd_seqs
        memset(zstd_seqs, 0, ZSTD_SEQUENCES_SIZE * sizeof(ZSTD_Sequence));
        dec_offset = 0;
        produced += compressed_sz;
        consumed += cnt_sz;
    }

    *dest_len = produced;

done:
    free(dest_data);
    free(zstd_seqs);
    return ret;
}

int qzZstdGetDefaults(QzSessionParamsLZ4S_T *defaults)
{
    if (defaults == NULL) {
        QZ_ERROR("%s : QzSessionParams ptr is empty\n", QZSTD_ERROR_TYPE);
        return QZSTD_ERROR;
    }

    QZ_MEMCPY(defaults,
              &sess_params_zstd_default,
              sizeof(QzSessionParamsLZ4S_T),
              sizeof(QzSessionParamsLZ4S_T));
    return QZSTD_OK;
}

int compressFile(int in_file, int out_file)
{

    long input_file_size = 0;
    long output_file_size = 0;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    unsigned int max_dst_buffer_size = 0;
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;
    unsigned int block_size = ZSRC_BUFF_LEN;
    long file_remaining = 0;
    unsigned int bytes_read = 0;
    int rc = QZSTD_OK;
    int is_compress = 1;
    uint64_t callback_error_code = 0;

    //initial zstd contex
    ZSTD_CCtx *zc = ZSTD_createCCtx();
    if (zc == NULL) {
        QZ_ERROR("%s : ZSTD_createCCtx failed\n", QZSTD_ERROR_TYPE);
        return QZSTD_ERROR;
    }
    ZSTD_CCtx_setParameter(zc, ZSTD_c_blockDelimiters,
                           ZSTD_sf_explicitBlockDelimiters);
    g_sess_params.qzCallback_external = (void *)zc;

    /* Different mini_match would use different LZ4MINMATCH to decode
    * lz4s sequence. note that when it is mini_match is 4, the LZ4MINMATCH
    * should be 3. if mini match is 3, then LZ4MINMATCH should be 2*/
    LZ4MINMATCH = g_sess_params.lz4s_mini_match == 4 ? 3 : 2;

    /* Align zstd minmatch to the QAT minmatch */
    ZSTD_CCtx_setParameter(
        zc, ZSTD_c_minMatch,
        g_sess_params.lz4s_mini_match >= 4 ? 4 : 3
    );

    //setup session
    int ret = qzInit(&qzstd_g_sess, g_sess_params.common_params.sw_backup);
    if (ret != QZ_OK && ret != QZ_DUPLICATE) {
        QZ_ERROR("%s : qzInit failed with error code %d\n", QZ_ERROR_TYPE, ret);
        return QZSTD_ERROR;
    }

    ret = qzSetupSessionLZ4S(&qzstd_g_sess, &g_sess_params);
    if (ret != QZ_OK) {
        QZ_ERROR("%s : qzSetupSession failed with error code %d\n", QZ_ERROR_TYPE, ret);
        return QZSTD_ERROR;
    }

    //get input file size
    input_file_size = lseek(in_file, 0, SEEK_END);
    lseek(in_file, 0, SEEK_SET);

    src_buffer_size = (input_file_size > block_size) ?
                      block_size : input_file_size;

    int max_zstd_sz = ZSTD_compressBound(src_buffer_size);
    int max_lz4_sz = LZ4_compressBound(src_buffer_size);

    max_dst_buffer_size = (max_zstd_sz > max_lz4_sz) ?
                          max_zstd_sz * 2 : max_lz4_sz * 2;

    src_buffer = malloc(src_buffer_size);
    assert(src_buffer != NULL);
    dst_buffer = malloc(max_dst_buffer_size);
    assert(dst_buffer != NULL);

    file_remaining = input_file_size;
    struct timeval time_s;
    struct timeval time_e;
    double time = 0.0f;

    do {
        bytes_read = read(in_file, src_buffer, src_buffer_size);
        QZ_PRINT("Reading input file (%u Bytes)\n", bytes_read);
        puts("compressing...");

        dst_buffer_size = max_dst_buffer_size;
        gettimeofday(&time_s, NULL);
        if (bytes_read <
            g_sess_params.common_params.input_sz_thrshold) { //goto sw compress
            dst_buffer_size = ZSTD_compressCCtx(zc, dst_buffer, dst_buffer_size, src_buffer,
                                                bytes_read, 1);
            if ((int)dst_buffer_size <= 0) {
                QZ_ERROR("%s : ZSTD API ZSTD_compressCCtx failed with error code, %d, %s\n",
                         ZSTD_ERROR_TYPE, dst_buffer_size, DECODE_ZSTD_ERROR_CODE(dst_buffer_size));
                rc = QZSTD_ERROR;
                goto done;
            }
        } else {
            //compress by qat
            int ret = qzCompressExt(&qzstd_g_sess, src_buffer,
                                    (unsigned int *)(&bytes_read),
                                    dst_buffer, &dst_buffer_size, 1, &callback_error_code);
            if (QZ_BUF_ERROR == ret && 0 != bytes_read) {
                if (-1 == lseek(in_file, bytes_read - src_buffer_size, SEEK_CUR)) {
                    QZ_ERROR("%s : failed to re-seek input file\n", QZSTD_ERROR_TYPE);
                    rc = QZSTD_ERROR;
                    goto done;
                }
            } else if (QZ_POST_PROCESS_ERROR == ret) {
                QZ_ERROR("%s : ZSTD API ZSTD_compressSequences failed with error code, %d, %s\n",
                         ZSTD_ERROR_TYPE, ret, DECODE_ZSTD_ERROR_CODE((size_t)callback_error_code));
                rc = QZSTD_ERROR;
                goto done;
            } else if (QZ_OK != ret) {
                QZ_ERROR("%s : qzCompress failed with error code %d\n", QZ_ERROR_TYPE, ret);
                rc = QZSTD_ERROR;
                goto done;
            }
        }
        gettimeofday(&time_e, NULL);
        time += get_time_diff(time_e, time_s);

        size_t write_size = write(out_file, dst_buffer, dst_buffer_size);
        if (write_size != dst_buffer_size) {
            QZ_ERROR("%s : failed to write output data into file\n", QZSTD_ERROR_TYPE);
            rc = QZSTD_ERROR;
            goto done;
        }
        output_file_size += write_size;

        file_remaining -= bytes_read;
        src_buffer_size = (src_buffer_size < file_remaining) ?
                          src_buffer_size : file_remaining;
    } while (file_remaining > 0);

    QzstdDisplayStats(time, input_file_size, output_file_size, is_compress);

done:
    //release resource

    if (zc != NULL) {
        ZSTD_freeCCtx(zc);
    }

    free(src_buffer);
    free(dst_buffer);
    qzTeardownSession(&qzstd_g_sess);
    qzClose(&qzstd_g_sess);

    return rc;
}

int decompressFile(int in_file, int out_file)
{
    long input_file_size = 0;
    long output_file_size = 0;
    size_t src_buffer_size = ZSTD_DStreamInSize();
    size_t dst_buffer_size = ZSTD_DStreamOutSize();
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;

    int rc = QZSTD_OK;
    int is_compress = 0;

    struct timeval time_s;
    struct timeval time_e;
    double time = 0.0f;

    //get input file size

    input_file_size = lseek(in_file, 0, SEEK_END);
    lseek(in_file, 0, SEEK_SET);

    src_buffer = malloc(src_buffer_size);
    assert(src_buffer != NULL);
    dst_buffer = malloc(dst_buffer_size);
    assert(dst_buffer != NULL);

    ZSTD_DCtx *zstd_dctx = ZSTD_createDCtx();
    assert(zstd_dctx != NULL);

    size_t bytes_read = 0;
    size_t bytes_write = 0;
    long file_remaining = 0;
    file_remaining = input_file_size;
    int ret = 0;

    do {
        bytes_read = read(in_file, src_buffer, src_buffer_size);
        QZ_PRINT("Reading input file (%lu Bytes)\n", bytes_read);
        puts("Decompressing...");

        ZSTD_inBuffer input = { src_buffer, bytes_read, 0 };
        while (input.pos < input.size) {
            ZSTD_outBuffer output = { dst_buffer, dst_buffer_size, 0 };

            gettimeofday(&time_s, NULL);
            ret = ZSTD_decompressStream(zstd_dctx, &output, &input);
            if (ret < 0) {
                QZ_ERROR("%s : ZSTD API ZSTD_decompressStream failed with error code, %d, %s\n",
                         ZSTD_ERROR_TYPE, ret, DECODE_ZSTD_ERROR_CODE(ret));
                rc = QZSTD_ERROR;
                goto done;
            }
            gettimeofday(&time_e, NULL);
            time += get_time_diff(time_e, time_s);

            bytes_write = write(out_file, dst_buffer, output.pos);
            output_file_size += bytes_write;
        }
        file_remaining -= bytes_read;
        src_buffer_size = (src_buffer_size < file_remaining) ?
                          src_buffer_size : file_remaining;
    } while (file_remaining > 0);

    if (ret != 0) {
        QZ_ERROR("%s : ZSTD API ZSTD_decompressStream failed with error code, %d, %s\n",
                 ZSTD_ERROR_TYPE, ret, DECODE_ZSTD_ERROR_CODE(ret));
        rc = QZSTD_ERROR;
        goto done;
    }

    QzstdDisplayStats(time, input_file_size, output_file_size, is_compress);

done:
    ZSTD_freeDCtx(zstd_dctx);
    free(src_buffer);
    free(dst_buffer);

    return rc;
}

void QzstdDisplayStats(double time, off_t insize, off_t outsize,
                       int is_compress)
{
    if (insize && outsize) {
        assert(0 != time);
        double size = (is_compress) ? insize : outsize;
        double throughput = (size * CHAR_BIT) / time; /* in MB (megabytes) */
        double compressionRatio = ((double)insize) / ((double)outsize);
        double spaceSavings = 1 - ((double)outsize) / ((double)insize);

        QZ_PRINT("Time taken:    %9.3lf ms\n", time / 1000);
        QZ_PRINT("Throughput:    %9.3lf Mbit/s\n", throughput);
        if (is_compress) {
            QZ_PRINT("Space Savings: %9.3lf %%\n", spaceSavings * 100.0);
            QZ_PRINT("Compression ratio: %.3lf : 1\n", compressionRatio);
        }
    }
}
