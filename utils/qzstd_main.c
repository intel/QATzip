/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
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
#include "qzstd.h"

extern QzSessionParams_T g_sess_params;

int main(int argc, char **argv)
{
    int decompress = 0;
    char *output_name = NULL;

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
            g_sess_params.hw_buff_sz = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            // make sure chunk size smaller than 128kB to fit zstd block size limitation.
            if (g_sess_params.hw_buff_sz > 128 * KB) {
                QZ_ERROR("%s : block size can't bigger than 128 KB\n", QZSTD_ERROR_TYPE);
                return QZSTD_ERROR;
            }
            break;
        case 'h':
            qzstd_help();
            return QZSTD_OK;
        case 'L':
            g_sess_params.comp_lvl = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            break;
        case 'o':
            output_name = optarg;
            break;
        case 'r':
            g_sess_params.req_cnt_thrshold = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            break;
        case 'P':
            if (strcmp(optarg, "busy") == 0) {
                g_sess_params.is_busy_polling = QZ_BUSY_POLLING;
            } else {
                QZ_ERROR("%s : set wrong polling mode: %s\n", QZSTD_ERROR_TYPE, optarg);
                return QZSTD_ERROR;
            }
            break;
        case 'm':
            g_sess_params.lz4s_mini_match = GET_LOWER_32BITS(strtoul(optarg, &stop, 0));
            if (g_sess_params.lz4s_mini_match != 3 &&
                g_sess_params.lz4s_mini_match != 4) {
                printf("Error! mini_match can only set 3 or 4!\n");
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

    if (!decompress) {
        int rc = compressFile(argv[optind], output_name);
        if (rc != QZSTD_OK) {
            return QZSTD_ERROR;
        }
    } else {
        int rc = decompressFile(argv[optind], output_name);
        if (rc != QZSTD_OK) {
            return QZSTD_ERROR;
        }
    }

    return QZSTD_OK;
}
