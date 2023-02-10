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

/*
sample zstd-qat application
*/

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "qzstd.h"

extern QzSessionParamsLZ4S_T g_sess_params;

int main(int argc, char **argv)
{
    int decompress = 0;
    struct stat in_file_state;
    char out_path [PATH_MAX] = {0};
    char in_path[PATH_MAX] = {0};
    char *out_filename = NULL;
    char *in_filename = NULL;
    int in_file = -1;
    int out_file = -1;
    char *tmp = NULL;
    char *suffix = NULL;

    if (qzZstdGetDefaults(&g_sess_params) != QZ_OK) {
        return QZSTD_ERROR;
    }

    const char *optPatten = "dhC:L:o:r:P:m:";
    char *stop = NULL;
    int ch;
    while ((ch = getopt(argc, argv, optPatten)) >= 0) {
        switch (ch) {
        case 'd':
            decompress = 1;
            break;
        case 'C':
            g_sess_params.common_params.hw_buff_sz = GET_LOWER_32BITS(strtoul(optarg, &stop,
                    0));
            // make sure chunk size smaller than 128kB to fit zstd block size limitation.
            if (g_sess_params.common_params.hw_buff_sz > 128 * KB) {
                QZ_ERROR("%s : block size can't bigger than 128 KB\n", QZSTD_ERROR_TYPE);
                return QZSTD_ERROR;
            }
            break;
        case 'h':
            qzstd_help();
            return QZSTD_OK;
        case 'L':
            g_sess_params.common_params.comp_lvl = GET_LOWER_32BITS(strtoul(optarg, &stop,
                                                   0));
            break;
        case 'o':
            out_filename = optarg;
            break;
        case 'r':
            g_sess_params.common_params.req_cnt_thrshold = GET_LOWER_32BITS(strtoul(optarg,
                    &stop, 0));
            break;
        case 'P':
            if (strcmp(optarg, "busy") == 0) {
                g_sess_params.common_params.polling_mode = QZ_BUSY_POLLING;
            } else {
                QZ_ERROR("%s : set wrong polling mode: %s\n", QZSTD_ERROR_TYPE, optarg);
                return QZSTD_ERROR;
            }
            break;
        case 'm':
            g_sess_params.lz4s_mini_match = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (g_sess_params.lz4s_mini_match != 3 &&
                g_sess_params.lz4s_mini_match != 4) {
                QZ_ERROR("%s : mini_match can only set 3 or 4!\n", QZSTD_ERROR_TYPE);
                return QZSTD_ERROR;
            }
            break;
        default:
            qzstd_help();
            return QZSTD_OK;
        }
    }

    int arg_count = argc - optind;
    if (arg_count == 0 || arg_count > 1) {
        QZ_ERROR("%s : only support one file as input\n", QZSTD_ERROR_TYPE);
        return QZSTD_ERROR;
    }

    /* argv[optind] is the input file name, need to check the input filename */
    in_filename = argv[optind];
    in_filename = realpath(in_filename, in_path);
    if (!in_filename) {
        QZ_ERROR("Please check input file name %s\n", in_filename);
        return QZSTD_ERROR;
    }

    /*
     * For compression, if the output file name is not be specified
     * we need to suffix the input filename with .zst as the output filename
     */
    if (!decompress && !out_filename) {
        tmp = strrchr(in_filename, '/');
        assert(tmp);
        strncpy(out_path, tmp + 1, PATH_MAX - 1);
        strcat(out_path, ".zst");
        out_filename = &out_path[0];
    }

    /* check the input filename suffix for decompression,
     * if output file name is not specified, we need to unsuffix the
     * input filename as the output filename
     */
    if (decompress) {
        suffix = strrchr(in_filename, '.');
        if (suffix == NULL) {
            QZ_ERROR("%s : unsupported file format\n", QZSTD_ERROR_TYPE);
            return QZSTD_ERROR;
        }
        if (strcmp(suffix, ".zst")) {
            QZ_ERROR("%s : unsupported file format\n", QZSTD_ERROR_TYPE);
            return QZSTD_ERROR;
        }

        if (!out_filename) {
            tmp = strrchr(in_filename, '/');
            assert(tmp);
            strncpy(out_path, tmp + 1, PATH_MAX - 1);
            suffix = strrchr(out_path, '.');
            assert(suffix);
            suffix[0] = '\0';
            out_filename = &out_path[0];
        }
    }

    in_file = open(in_filename, O_RDONLY);
    if (in_file < 0) {
        perror("Cannot open input file");
        return QZSTD_ERROR;
    }

    if (fstat(in_file, &in_file_state)) {
        perror("Cannot get file stat");
        close(in_file);
        return QZSTD_ERROR;
    }

    out_file = open(out_filename, O_CREAT | O_WRONLY,
                    in_file_state.st_mode);
    if (out_file == -1) {
        perror("Cannot open output file");
        close(in_file);
        return QZSTD_ERROR;
    }

    if (!decompress) {
        int rc = compressFile(in_file, out_file);
        if (rc != QZSTD_OK) {
            close(in_file);
            close(out_file);
            return QZSTD_ERROR;
        }
    } else {
        int rc = decompressFile(in_file, out_file);
        if (rc != QZSTD_OK) {
            close(in_file);
            close(out_file);
            return QZSTD_ERROR;
        }
    }

    close(in_file);
    close(out_file);
    return QZSTD_OK;
}
