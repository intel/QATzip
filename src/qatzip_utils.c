/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2018 Intel Corporation. All rights reserved.
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

#include <stdlib.h>
#include <qz_utils.h>

static QatThread_T g_qat_thread;

static void doInsertThread(unsigned int th_id,
                           ThreadList_T **thd_list,
                           unsigned int *num_thd,
                           pthread_mutex_t *lock,
                           Serv_T serv_type,
                           Engine_T engine_type)
{
    ThreadList_T *node;
    ThreadList_T *prev_node;

    if (0 != pthread_mutex_lock(lock)) {
        return;
    }

    for (prev_node = node = *thd_list; node; node = node->next) {
        if (node->thread_id == th_id) {
            break;
        }
        prev_node = node;
    }

    if (!node) {
        node = (ThreadList_T *)calloc(1, sizeof(*node));
        if (!node) {
            QZ_ERROR("[ERROR]: alloc memory failed in file(%s) line(%d)\n",
                     __FILE__, __LINE__);
            goto done;
        }

        node->thread_id = th_id;
        ++*num_thd;
        if (prev_node) {
            prev_node->next = node;
        } else {
            *thd_list = node;
        }
    }

    if (SW == engine_type) {
        if (COMPRESSION == serv_type) {
            ++node->comp_sw_count;
        } else if (DECOMPRESSION == serv_type) {
            ++node->decomp_sw_count;
        }
    } else if (HW == engine_type) {
        if (COMPRESSION == serv_type) {
            ++node->comp_hw_count;
        } else if (DECOMPRESSION == serv_type) {
            ++node->decomp_hw_count;
        }
    }

done:
    (void)pthread_mutex_unlock(lock);
}

void insertThread(unsigned int th_id,
                  Serv_T serv_type,
                  Engine_T engine_type)
{
    QatThread_T *th_list = &g_qat_thread;
    if (COMPRESSION == serv_type) {
        doInsertThread(th_id,
                       &th_list->comp_th_list,
                       &th_list->num_comp_th,
                       &th_list->comp_lock,
                       serv_type,
                       engine_type);
    } else if (DECOMPRESSION == serv_type) {
        doInsertThread(th_id,
                       &th_list->decomp_th_list,
                       &th_list->num_decomp_th,
                       &th_list->decomp_lock,
                       serv_type,
                       engine_type);
    }
}

static void doDumpThreadInfo(ThreadList_T *node,
                             unsigned int num_node,
                             Serv_T type)
{
    unsigned int i;
    char *serv_title;

    if (num_node > 0) {
        i = 0;
        if (COMPRESSION == type) {
            serv_title = "Compression";
        } else {
            serv_title = "Decompression";
        }

        QZ_PRINT("[INFO]: %s num_th %u\n",
                 serv_title, num_node);
        while (node) {
            QZ_PRINT("th_id: %u comp_hw_count: %u comp_sw_count: %u "
                     "decomp_hw_count: %u decomp_sw_count: %u\n",
                     node->thread_id,
                     node->comp_hw_count,
                     node->comp_sw_count,
                     node->decomp_hw_count,
                     node->decomp_sw_count);
            i++;
            node = node->next;
            if (i == num_node) {
                break;
            }
        }

        if (node) {
            QZ_ERROR("[ERROR]: there's node left in the list\n");
        }
        QZ_PRINT("\n");
    }
}

void dumpThreadInfo(void)
{
    QatThread_T *th_list = &g_qat_thread;
    doDumpThreadInfo(th_list->comp_th_list,
                     th_list->num_comp_th,
                     COMPRESSION);
    doDumpThreadInfo(th_list->decomp_th_list,
                     th_list->num_decomp_th,
                     DECOMPRESSION);
}

void initDebugLock(void)
{
    pthread_mutex_init(&g_qat_thread.comp_lock, NULL);
    pthread_mutex_init(&g_qat_thread.decomp_lock, NULL);
}
