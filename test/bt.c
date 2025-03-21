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
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "qatzip.h"

#define DEFAULT_BUF_LEN (256*1024)

void usage(void)
{
    printf("-f fit decompress buffers to size (default full buffers)\n");
    printf("-s skip specific number of bytes\n");
    printf("-S start test at specific position of source buffer\n");
    printf("-E finish test at specific position of source buffer\n");
    printf("-c corpus flag for data in source buffer: 0 - iterative, 1 - random, 2 - 'A'\n");
    return;
}

int main(int argc, char *argv[])
{
    unsigned int i, j, rc, d_len, s_len, s2_len, src_sz, dest_sz;

    unsigned char *src, *dest, *src2;
    QzSession_T sess = {0};
    int c;
    int fit = 0;
    int start = 1;
    int skip = 1;
    int end = DEFAULT_BUF_LEN;
    int corpus = 0; // 0 = iterative, 1 = random
    srand((time(NULL) & 0xffffffff));

    while ((c = getopt(argc, argv, "fS:s:E:c:")) != -1) {
        switch (c) {
        case 'f':
            fit = 1;
            break;
        case 's':
            skip = atoi(optarg);
            if (skip <= 1) {
                usage();
                return -1;
            }
            printf("Will skip by %d\n", skip);
            break;
        case 'S':
            start = atoi(optarg);
            printf("Will start at %d\n", start);
            break;
        case 'E':
            end  = atoi(optarg);
            printf("Will end at %d\n", end);
            break;
        case 'c':
            corpus  = atoi(optarg);
            printf("corpus = ");
            if (corpus == 0) {
                printf("iterative\n");
            } else if (corpus == 1) {
                printf("randon\n");
            }
            if (corpus == 2) {
                printf("\"A\"\n");
            }
            break;
        default:
            usage();
            return -1;
        }
    }

    src_sz = DEFAULT_BUF_LEN;
    dest_sz = (9 * src_sz / 8) + 1024;
    src = qzMalloc(src_sz, 0, 1);
    dest = qzMalloc(dest_sz, 0, 1);
    src2 = qzMalloc(src_sz, 0, 1);
    if (NULL == src || NULL == dest || NULL == src2) {
        return -1;
    }
    i = DEFAULT_BUF_LEN;
    if (corpus == 0) {
        for (j = 0; j < i; j++) {
            src[j] = (char)(j % 200);
        }
    } else if (corpus == 1) {
        for (j = 0; j < i; j++) {
            src[j] = (char)rand() % 255;
        }
    } else {
        for (j = 0; j < i; j++) {
            src[j] = (char)'A';
        }
    }


    printf("src = 0x%lx, src2 = 0x%lx,  dest = 0x%lx\n", (long unsigned int)src,
           (long unsigned int)src2, (long unsigned int)dest);
    printf("fit = %d\n", fit);
    for (i = start; i < end; i += skip) {
        s_len = i;
        d_len = dest_sz;
        rc = qzCompress(&sess, src, &s_len, dest, &d_len, 1);
        // printf( "rc = %d, src = %d, dest = %d\n",
        //  rc, s_len, d_len );
        if (rc == 0) {
            if (1 == fit) {
                s2_len = s_len;
            } else {
                s2_len = DEFAULT_BUF_LEN;
            }
            rc = qzDecompress(&sess, dest, &d_len, src2, &s2_len);
            // printf( "rc2 = %d, src = %d, dest = %d\n",
            //  rc2, d_len, s2_len );
            if (rc == 0) {
                if (s2_len != s_len) {
                    printf("mismatch orig\t%d\tcomp %d\tdec %d\t %d\n",
                           s_len, d_len, s2_len, (s_len - s2_len));
                    printf("\t\t%d\t%d\t%d\n", (s_len % (64 * 1024)),
                           (d_len % (64 * 1024)), (s2_len % (64 * 1024)));
                    goto error;
                }
                if (memcmp(src, src2, s_len) != 0) {
                    printf("memcmp mismatch - len = %d\t%d\n", s_len,
                           (s_len % (64 * 1024)));
                    goto error;
                }
            } else {
                printf("return from decomress is %d\t len = %d\t%d\n", rc,
                       s_len, (s_len % (64 * 1024)));
                goto error;
            }
        } else {
            goto error;
        }
    }
    return 0;
error:
    qzTeardownSession(&sess);
    qzFree(src);
    qzFree(dest);
    qzFree(src2);
    return rc;
}
