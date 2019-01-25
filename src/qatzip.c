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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <bits/types.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "icp_sal_poll.h"
#include "icp_sal_user.h"
#include "qae_mem.h"
#include "qatzip.h"
#include "qatzip_internal.h"
#include "qz_utils.h"

/*
 * Process address space name described in the config file for this device.
 */
const char *g_dev_tag = "SHIM";

#define INTER_SZ(src_sz)          (2 * (src_sz))
#define DEST_SZ(src_sz)           (((9 * (src_sz)) / 8) + 1024)
#define msleep(x)                 usleep((x) * 1000)

#define QZ_INIT_FAIL(rc)          (QZ_PARAMS == rc     || \
                                   QZ_NOSW_NO_HW == rc || \
                                   QZ_FAIL == rc)

#define IS_DEFLATE(fmt)  (QZ_DEFLATE_RAW == (fmt))
#define IS_DEFLATE_OR_GZIP(fmt) \
        (QZ_DEFLATE_RAW == (fmt) || QZ_DEFLATE_GZIP == (fmt))

#define GET_BUFFER_SLEEP_NSEC   10

QzSessionParams_T g_sess_params_default = {
    .huffman_hdr       = QZ_HUFF_HDR_DEFAULT,
    .direction         = QZ_DIRECTION_DEFAULT,
    .data_fmt          = QZ_DATA_FORMAT_DEFAULT,
    .comp_lvl          = QZ_COMP_LEVEL_DEFAULT,
    .comp_algorithm    = QZ_COMP_ALGOL_DEFAULT,
    .poll_sleep        = QZ_POLL_SLEEP_DEFAULT,
    .max_forks         = QZ_MAX_FORK_DEFAULT,
    .sw_backup         = QZ_SW_BACKUP_DEFAULT,
    .hw_buff_sz        = QZ_HW_BUFF_SZ,
    .strm_buff_sz      = QZ_STRM_BUFF_SZ_DEFAULT,
    .input_sz_thrshold = QZ_COMP_THRESHOLD_DEFAULT,
    .req_cnt_thrshold  = QZ_REQ_THRESHOLD_DEFAULT,
    .wait_cnt_thrshold = QZ_WAIT_CNT_THRESHOLD_DEFAULT
};

processData_T g_process = {
    .qz_init_status = QZ_NONE,
    .pcie_count = -1
};
pthread_mutex_t g_lock;

__thread ThreadData_T g_thread = {
    .ppid = 0,
};

static int setInstance(unsigned int dev_id, QzInstanceList_T *new_instance,
                       QzHardware_T *qat_hw)
{
    if (dev_id > QAT_MAX_DEVICES ||
        NULL == new_instance ||
        NULL == qat_hw ||
        NULL != new_instance->next) {
        return QZ_PARAMS;
    }

    QzInstanceList_T *instances = &qat_hw->devices[dev_id];
    /* first instance */
    if (NULL == instances->next) {
        qat_hw->dev_num++;
    }

    while (instances->next) {
        instances = instances->next;
    }
    instances->next = new_instance;

    if (dev_id > qat_hw->max_dev_id) {
        qat_hw->max_dev_id = dev_id;
    }

    return QZ_OK;
}

static QzInstanceList_T *getInstance(unsigned int dev_id, QzHardware_T *qat_hw)
{
    if (dev_id > QAT_MAX_DEVICES || NULL == qat_hw) {
        return NULL;
    }

    QzInstanceList_T *instances = &qat_hw->devices[dev_id];
    QzInstanceList_T *first_instance = instances->next;
    int i;

    /* no instance */
    if (NULL == first_instance) {
        goto exit;
    }

    instances->next = first_instance->next;

    /* last instance */
    if (NULL == instances->next && qat_hw->dev_num > 0) {
        qat_hw->dev_num--;
        if (qat_hw->max_dev_id > 0 && dev_id == qat_hw->max_dev_id) {
            for (i = qat_hw->max_dev_id - 1; i >= 0; i--) {
                if (qat_hw->devices[i].next) {
                    qat_hw->max_dev_id = i;
                    break;
                }
            }
        }
    }

exit:
    return first_instance;
}

static void clearDevices(QzHardware_T *qat_hw)
{
    if (NULL == qat_hw || 0 == qat_hw->dev_num) {
        return;
    }

    for (int i = 0; i < qat_hw->max_dev_id; i++) {
        QzInstanceList_T *inst = getInstance(i, qat_hw);
        while (inst) {
            free(inst);
            inst = getInstance(i, qat_hw);
        }
    }
}

static void dcCallback(void *cbtag, CpaStatus stat)
{
    long tag, i, j;

    tag = (long)cbtag;
    j = GET_LOWER_16BITS(tag);
    i = tag >> 16;

    if (g_process.qz_inst[i].stream[j].src1 !=
        g_process.qz_inst[i].stream[j].src2) {
        goto print_err;
    }

    if (g_process.qz_inst[i].stream[j].sink1 !=
        g_process.qz_inst[i].stream[j].sink2) {
        goto print_err;
    }

    if (g_process.qz_inst[i].stream[j].src2 !=
        (g_process.qz_inst[i].stream[j].sink1 + 1)) {
        goto print_err;
    }

    g_process.qz_inst[i].stream[j].sink1++;
    g_process.qz_inst[i].stream[j].job_status = stat;
    goto done;

print_err:
    QZ_ERROR("FLOW ERROR IN CBi %ld %ld\n", i, j);
done:
    return;
}

static int qzGrabInstance(int hint)
{
    int i, rc;

    if (QZ_NONE == g_process.qz_init_status) {
        return -1;
    }

    if (hint >= g_process.num_instances) {
        hint = g_process.num_instances - 1;
    }

    if (hint < 0) {
        hint = 0;
    }

    /*check hint first*/
    rc = __sync_lock_test_and_set(&(g_process.qz_inst[hint].lock), 1);
    if (0 == rc) {
        return hint;
    }

    /*otherwise loop through all of them*/
    for (i = 0; i < g_process.num_instances; i++) {
        rc = __sync_lock_test_and_set(&(g_process.qz_inst[i].lock), 1);
        if (0 ==  rc) {
            return i;
        }
    }

    return -1;
}

static int getUnusedBuffer(unsigned long i, int j)
{
    int k;
    Cpa16U max;

    max = g_process.qz_inst[i].dest_count;
    if (j < 0) {
        j = 0;
    } else if (j >= max) {
        j = GET_LOWER_16BITS(max);
    }

    for (k = j; k < max; k++) {
        if ((g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].src2) &&
            (g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].sink1) &&
            (g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].sink2)) {
            return k;
        }
    }

    for (k = 0; k < max; k++) {
        if ((g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].src2) &&
            (g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].sink1) &&
            (g_process.qz_inst[i].stream[k].src1 ==
             g_process.qz_inst[i].stream[k].sink2)) {
            return k;
        }
    }

    return -1;
}

static void qzReleaseInstance(int i)
{
    __sync_lock_release(&(g_process.qz_inst[i].lock));
}

static void init_timers(void)
{
    int i;

    for (i = 1; i < MAX_THREAD_TMR; i++) {
        g_thread.timer[i].tv_sec = 0;
        g_thread.timer[i].tv_usec = 0;
    }
    gettimeofday(&g_thread.timer[0], NULL);
}

static int qz_sessParamsCheck(QzSessionParams_T *params)
{
    if (!params) {
        return FAILURE;
    }

    if (params->huffman_hdr > QZ_STATIC_HDR                   ||
        params->direction > QZ_DIR_BOTH                       ||
        params->comp_lvl < 1                                  ||
        params->comp_lvl > 9                                  ||
        params->comp_algorithm != QZ_DEFLATE                  ||
        params->sw_backup > 1                                 ||
        params->hw_buff_sz < QZ_HW_BUFF_MIN_SZ                ||
        params->hw_buff_sz > QZ_HW_BUFF_MAX_SZ                ||
        params->strm_buff_sz < QZ_STRM_BUFF_MIN_SZ            ||
        params->strm_buff_sz > QZ_STRM_BUFF_MAX_SZ            ||
        (params->hw_buff_sz & (params->hw_buff_sz - 1))       ||
        params->input_sz_thrshold < QZ_COMP_THRESHOLD_MINIMUM ||
        params->input_sz_thrshold > QZ_HW_BUFF_MAX_SZ         ||
        params->req_cnt_thrshold < QZ_REQ_THRESHOLD_MINIMUM   ||
        params->req_cnt_thrshold > QZ_REQ_THRESHOLD_MAXINUM) {
        return FAILURE;
    }

    return SUCCESS;
}

static void stopQat(void)
{
    int i;
    CpaStatus status = CPA_STATUS_SUCCESS;

    if (QZ_NONE == g_process.qz_init_status) {
        return;
    }

    QZ_DEBUG("Call stopQat.\n");
    if (NULL != g_process.dc_inst_handle && NULL != g_process.qz_inst) {
        for (i = 0; i < g_process.num_instances; i++) {
            status = cpaDcStopInstance(g_process.dc_inst_handle[i]);
            if (CPA_STATUS_SUCCESS != status) {
                QZ_ERROR("Stop instance failed, status=%d\n", status);
            }
        }

        free(g_process.dc_inst_handle);
        g_process.dc_inst_handle = NULL;
        free(g_process.qz_inst);
        g_process.qz_inst = NULL;
    }

    (void)icp_sal_userStop();
    g_process.num_instances = (Cpa16U)0;
    g_process.qz_init_status = QZ_NONE;
}

static void freeQzMemEntries(void)
{
    extern QzMem_T *g_qz_mem;
    if (NULL != g_qz_mem) {
        free(g_qz_mem);
        g_qz_mem = NULL;
    }
}

static void exitFunc(void)
{
    stopQat();
    freeQzMemEntries();
#ifdef QATZIP_DEBUG
    dumpThreadInfo();
#endif
}

static unsigned int getWaitCnt(QzSession_T *sess)
{
    QzSess_T *qz_sess;

    if (sess->internal != NULL) {
        qz_sess = (QzSess_T *)sess->internal;
        return qz_sess->sess_params.wait_cnt_thrshold;
    } else {
        return g_sess_params_default.wait_cnt_thrshold;
    }
}

#define BACKOUT                                                    \
    stopQat();                                                     \
    if (1 == sw_backup) {                                          \
        g_process.qz_init_status = QZ_NO_HW;                       \
        QZ_ERROR("g_process.qz_init_status = QZ_NO_HW\n");         \
    } else if (0 == sw_backup) {                                   \
        g_process.qz_init_status = QZ_NOSW_NO_HW;                  \
        QZ_ERROR("g_process.qz_init_status = QZ_NOSW_NO_HW\n");    \
    }                                                              \
    rc = g_process.qz_init_status;                                 \
    goto done;

#define QZ_HW_BACKOUT                                              \
    if(qat_hw) {                                                   \
        clearDevices(qat_hw);                                      \
        free(qat_hw);                                              \
    }                                                              \
    BACKOUT;


/* Initialize the QAT hardware, get the QAT instance for current
 * process
 */
int qzInit(QzSession_T *sess, unsigned char sw_backup)
{
    CpaStatus status;
    int rc = QZ_FAIL, i;
    unsigned int dev_id = 0;
    QzHardware_T *qat_hw = NULL;
    unsigned int instance_found = 0;
    extern QzMem_T *g_qz_mem;
    extern size_t g_mem_entries;
    static unsigned int waiting = 0;
    static unsigned int wait_cnt = 0;

    if (sess == NULL) {
        return QZ_PARAMS;
    }

    if (sw_backup > 1) {
        return QZ_PARAMS;
    }

    if (0 == g_process.pcie_count ||
        QZ_OK == g_process.qz_init_status) {
        return QZ_DUPLICATE;
    }

    if (waiting && wait_cnt > 0) {
        wait_cnt--;
        return QZ_DUPLICATE;
    }
    waiting = 0;

    if (0 != pthread_mutex_lock(&g_lock)) {
        return QZ_FAIL;
    }

    if (QZ_OK == g_process.qz_init_status) {
        if (0 != pthread_mutex_unlock(&g_lock)) {
            return QZ_FAIL;
        }
        return QZ_DUPLICATE;
    }

    g_thread.pid = getpid();
    g_thread.ppid = getppid();
    init_timers();
    g_process.sw_backup = sw_backup;

    status = icp_adf_get_numDevices(&g_process.pcie_count);
    if (CPA_STATUS_SUCCESS != status) {
        g_process.pcie_count = 0;
    }

    if (0 == g_process.pcie_count) {
        QZ_ERROR("Error no hardware, switch to SW if permitted\n", status);
        BACKOUT;
    }

    status = icp_sal_userStartMultiProcess(g_dev_tag, CPA_FALSE);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error userStarMultiProcess(%d), switch to SW if permitted\n",
                 status);
        waiting = 1;
        wait_cnt = getWaitCnt(sess);
        BACKOUT;
    }

    status = cpaDcGetNumInstances(&g_process.num_instances);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error in cpaDcGetNumInstances status = %d\n", status);
        BACKOUT;
    }
    QZ_DEBUG("Number of instance: %u\n", g_process.num_instances);

    g_process.dc_inst_handle =
        malloc(g_process.num_instances * sizeof(CpaInstanceHandle));
    g_process.qz_inst = calloc(g_process.num_instances, sizeof(QzInstance_T));
    if (NULL == g_process.dc_inst_handle || NULL == g_process.qz_inst) {
        QZ_ERROR("malloc failed\n");
        BACKOUT;
    }

    status = cpaDcGetInstances(g_process.num_instances, g_process.dc_inst_handle);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error in cpaDcGetInstances status = %d\n", status);
        BACKOUT;
    }

    qat_hw = calloc(1, sizeof(QzHardware_T));
    if (NULL == qat_hw) {
        QZ_ERROR("malloc failed\n");
        BACKOUT;
    }
    for (i = 0; i < g_process.num_instances; i++) {
        QzInstanceList_T *new_inst = calloc(1, sizeof(QzInstanceList_T));
        if (NULL == new_inst) {
            QZ_ERROR("malloc failed\n");
            QZ_HW_BACKOUT;
        }

        status = cpaDcInstanceGetInfo2(g_process.dc_inst_handle[i],
                                       &new_inst->instance.instance_info);
        if (CPA_STATUS_SUCCESS != status) {
            QZ_ERROR("Error in cpaDcInstanceGetInfo2 status = %d\n", status);
            QZ_HW_BACKOUT;
        }

        status = cpaDcQueryCapabilities(g_process.dc_inst_handle[i],
                                        &new_inst->instance.instance_cap);
        if (CPA_STATUS_SUCCESS != status) {
            QZ_ERROR("Error in cpaDcQueryCapabilities status = %d\n", status);
            QZ_HW_BACKOUT;
        }

        new_inst->instance.lock = 0;
        new_inst->instance.heartbeat = (time_t)0;
        new_inst->instance.mem_setup = 0;
        new_inst->instance.cpa_sess_setup = 0;
        new_inst->instance.num_retries = 0;
        new_inst->dc_inst_handle = g_process.dc_inst_handle[i];

        dev_id = new_inst->instance.instance_info.physInstId.packageId;
        if (QZ_OK != setInstance(dev_id, new_inst, qat_hw)) {
            QZ_ERROR("Insert instance on device %d failed\n", dev_id);
            QZ_HW_BACKOUT;
        }
    }

    /* shuffle instance */
    for (i = 0; instance_found < g_process.num_instances; i++) {
        dev_id = i % (qat_hw->max_dev_id + 1);
        QzInstanceList_T *new_inst = getInstance(dev_id, qat_hw);
        if (NULL == new_inst) {
            continue;
        }

        QZ_MEMCPY(&g_process.qz_inst[instance_found], &new_inst->instance,
                  sizeof(QzInstance_T), sizeof(QzInstance_T));
        g_process.dc_inst_handle[instance_found] = new_inst->dc_inst_handle;
        free(new_inst);
        instance_found++;
    }
    clearDevices(qat_hw);
    free(qat_hw);

    /* init qzMem entries*/
    if (NULL == g_qz_mem) {
        if (QZ_OK != qzGetMaxHugePages() || \
            0 == g_mem_entries) {
            BACKOUT;
        }

        g_qz_mem = (QzMem_T *)calloc(g_mem_entries, sizeof(QzMem_T));
        if (NULL == g_qz_mem) {
            BACKOUT;
        }
    }

    rc = atexit(exitFunc);
    if (QZ_OK != rc) {
        QZ_ERROR("Error in register exit hander rc = %d\n", rc);
        BACKOUT;
    }

    rc = g_process.qz_init_status = QZ_OK;

done:
    initDebugLock();
    if (0 != pthread_mutex_unlock(&g_lock)) {
        return QZ_FAIL;
    }

    return rc;
}

/* Free up the DMAable memory buffers used by QAT
 * internally, those buffers are source buffer,
 * intermeidate buffer and destination buffer
 */
static void cleanUpInstMem(int i)
{
    int j;

    if (0 == g_process.qz_inst[i].mem_setup) {
        return;
    }

    for (j = 0; j < g_process.qz_inst[i].intermediate_cnt; j++) {
        qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData);
        qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData);
        qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers);
        qzFree(g_process.qz_inst[i].intermediate_buffers[j]);
    }

    /*intermediate buffers*/
    if (NULL != g_process.qz_inst[i].intermediate_buffers) {
        free(g_process.qz_inst[i].intermediate_buffers);
        g_process.qz_inst[i].intermediate_buffers = NULL;
    }

    /*src buffers*/
    for (j = 0; j < g_process.qz_inst[i].src_count; j++) {
        qzFree(g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData);
        qzFree(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData);
        qzFree(g_process.qz_inst[i].src_buffers[j]->pBuffers);
        qzFree(g_process.qz_inst[i].src_buffers[j]);
    }

    if (NULL != g_process.qz_inst[i].src_buffers) {
        free(g_process.qz_inst[i].src_buffers);
        g_process.qz_inst[i].src_buffers = NULL;
    }

    /*dest buffers*/
    for (j = 0; j < g_process.qz_inst[i].dest_count; j++) {
        qzFree(g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData);
        qzFree(g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData);
        qzFree(g_process.qz_inst[i].dest_buffers[j]->pBuffers);
        qzFree(g_process.qz_inst[i].dest_buffers[j]);
    }

    if (NULL != g_process.qz_inst[i].dest_buffers) {
        free(g_process.qz_inst[i].dest_buffers);
        g_process.qz_inst[i].dest_buffers = NULL;
    }

    /*stream buffer*/
    if (NULL != g_process.qz_inst[i].stream) {
        free(g_process.qz_inst[i].stream);
        g_process.qz_inst[i].stream = NULL;
    }

    qzFree(g_process.qz_inst[i].cpaSess);
    g_process.qz_inst[i].mem_setup = 0;
}

#define QZ_INST_MEM_CHECK(ptr, i)                                    \
    if (NULL == (ptr)) {                                             \
        cleanUpInstMem((i));                                         \
        rc = sw_backup ? QZ_LOW_MEM : QZ_NOSW_LOW_MEM;               \
        goto done_inst;                                              \
    }

#define QZ_INST_MEM_STATUS_CHECK(status)                             \
    if (CPA_STATUS_SUCCESS != status) {                              \
        rc = sw_backup ? QZ_NO_INST_ATTACH : QZ_NOSW_NO_INST_ATTACH; \
        goto done_inst;                                              \
    }

/* Allocate the DMAable memory buffers used by QAT
 * internally, those buffers are source buffer,
 * intermeidate buffer and destination buffer
 */
static int getInstMem(int i, QzSessionParams_T *params)
{
    int j;
    CpaStatus status;
    CpaStatus rc;
    unsigned int src_sz;
    unsigned int inter_sz;
    unsigned int dest_sz;
    unsigned char sw_backup;

    rc = QZ_OK;
    src_sz = params->hw_buff_sz;
    inter_sz = INTER_SZ(src_sz);
    dest_sz = DEST_SZ(src_sz);
    sw_backup = params->sw_backup;

    QZ_DEBUG("getInstMem: Setting up memory for inst %d\n", i);
    status = cpaDcBufferListGetMetaSize(g_process.dc_inst_handle[i], 1,
                                        &(g_process.qz_inst[i].buff_meta_size));
    QZ_INST_MEM_STATUS_CHECK(status);

    status = cpaDcGetNumIntermediateBuffers(g_process.dc_inst_handle[i],
                                            &(g_process.qz_inst[i].intermediate_cnt));
    QZ_INST_MEM_STATUS_CHECK(status);

    g_process.qz_inst[i].intermediate_buffers =
        malloc((size_t)(g_process.qz_inst[i].intermediate_cnt * sizeof(
                            CpaBufferList *)));
    QZ_INST_MEM_CHECK(g_process.qz_inst[i].intermediate_buffers, i);

    for (j = 0; j < g_process.qz_inst[i].intermediate_cnt; j++) {
        g_process.qz_inst[i].intermediate_buffers[j] = (CpaBufferList *)
                qzMalloc(sizeof(CpaBufferList), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].intermediate_buffers[j], i);

        if (0 != g_process.qz_inst[i].buff_meta_size) {
            g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData =
                qzMalloc((size_t)(g_process.qz_inst[i].buff_meta_size), NODE_0, PINNED_MEM);
            QZ_INST_MEM_CHECK(
                g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData,
                i);
        }

        g_process.qz_inst[i].intermediate_buffers[j]->pBuffers = (CpaFlatBuffer *)
                qzMalloc(sizeof(CpaFlatBuffer), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers, i);

        g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData = (Cpa8U *)
                qzMalloc(inter_sz, NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData,
                          i);

        g_process.qz_inst[i].intermediate_buffers[j]->numBuffers = (Cpa32U)1;
        g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->dataLenInBytes =
            inter_sz;
    }

    g_process.qz_inst[i].src_count = NUM_BUFF;
    g_process.qz_inst[i].dest_count = NUM_BUFF;

    g_process.qz_inst[i].src_buffers = malloc((size_t)(
                                           g_process.qz_inst[i].src_count *
                                           sizeof(CpaBufferList *)));
    QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers, i);

    g_process.qz_inst[i].dest_buffers = malloc(g_process.qz_inst[i].dest_count *
                                        sizeof(CpaBufferList *));
    QZ_INST_MEM_CHECK(g_process.qz_inst[i].dest_buffers, i);

    g_process.qz_inst[i].stream = calloc(g_process.qz_inst[i].dest_count,
                                         sizeof(QzCpaStream_T));
    QZ_INST_MEM_CHECK(g_process.qz_inst[i].stream, i);

    for (j = 0; j < g_process.qz_inst[i].dest_count; j++) {
        g_process.qz_inst[i].stream[j].seq   = 0;
        g_process.qz_inst[i].stream[j].src1  = 0;
        g_process.qz_inst[i].stream[j].src2  = 0;
        g_process.qz_inst[i].stream[j].sink1 = 0;
        g_process.qz_inst[i].stream[j].sink2 = 0;

        g_process.qz_inst[i].src_buffers[j] = (CpaBufferList *)
                                              qzMalloc(sizeof(CpaBufferList), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers[j], i);

        if (0 != g_process.qz_inst[i].buff_meta_size) {
            g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData =
                qzMalloc(g_process.qz_inst[i].buff_meta_size, NODE_0, PINNED_MEM);
            QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData, i);
        }

        g_process.qz_inst[i].src_buffers[j]->pBuffers = (CpaFlatBuffer *)
                qzMalloc(sizeof(CpaFlatBuffer), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers, i);

        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = (Cpa8U *)
                qzMalloc(src_sz, NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData, i);

        g_process.qz_inst[i].src_buffers[j]->numBuffers = (Cpa32U)1;
        g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_sz;
    }

    for (j = 0; j < g_process.qz_inst[i].dest_count; j++) {
        g_process.qz_inst[i].dest_buffers[j] = (CpaBufferList *)
                                               qzMalloc(sizeof(CpaBufferList), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].dest_buffers[j], i);

        if (0 != g_process.qz_inst[i].buff_meta_size) {
            g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData =
                qzMalloc(g_process.qz_inst[i].buff_meta_size, NODE_0, PINNED_MEM);
            QZ_INST_MEM_CHECK(g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData, i);
        }

        g_process.qz_inst[i].dest_buffers[j]->pBuffers = (CpaFlatBuffer *)
                qzMalloc(sizeof(CpaFlatBuffer), NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].dest_buffers, i);

        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = (Cpa8U *)
                qzMalloc(dest_sz, NODE_0, PINNED_MEM);
        QZ_INST_MEM_CHECK(g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData, i);

        g_process.qz_inst[i].dest_buffers[j]->numBuffers = (Cpa32U)1;
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes = dest_sz;
    }

    status = cpaDcSetAddressTranslation(g_process.dc_inst_handle[i],
                                        qaeVirtToPhysNUMA);
    QZ_INST_MEM_STATUS_CHECK(status);

    g_process.qz_inst[i].inst_start_status =
        cpaDcStartInstance(g_process.dc_inst_handle[i],
                           g_process.qz_inst[i].intermediate_cnt,
                           g_process.qz_inst[i].intermediate_buffers);
    QZ_INST_MEM_STATUS_CHECK(g_process.qz_inst[i].inst_start_status);

    g_process.qz_inst[i].mem_setup = 1;

done_inst:
    return rc;
}

/* Initialize the QAT session parameters associate with current
 * process's QAT instance, the session parameters include various
 * configurations for compression/decompression request
 */
int qzSetupSession(QzSession_T *sess, QzSessionParams_T *params)
{
    QzSess_T *qz_sess;

    if (NULL == sess) {
        return QZ_PARAMS;
    }

    sess->hw_session_stat = QZ_FAIL;
    if (sess->internal == NULL) {
        sess->internal = calloc(1, sizeof(QzSess_T));
        if (NULL == sess->internal) {
            sess->hw_session_stat = QZ_NOSW_LOW_MEM;
            return QZ_NOSW_LOW_MEM;
        }
        qz_sess = (QzSess_T *)sess->internal;
        qz_sess->inst_hint = -1;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qz_sess->inst_hint = -1;
    qz_sess->seq = 0;
    qz_sess->seq_in = 0;
    if (NULL == params) {
        /*right now this always succeeds*/
        (void)qzGetDefaults(&(qz_sess->sess_params));
    } else {
        if (qz_sessParamsCheck(params) != SUCCESS) {
            return QZ_PARAMS;
        }
        QZ_MEMCPY(&(qz_sess->sess_params),
                  params,
                  sizeof(QzSessionParams_T),
                  sizeof(QzSessionParams_T));
    }

    qz_sess->force_sw = 0;
    qz_sess->inflate_strm = NULL;
    qz_sess->inflate_stat = InflateNull;

    /*set up cpaDc Session params*/
    qz_sess->session_setup_data.compLevel = qz_sess->sess_params.comp_lvl;
    qz_sess->session_setup_data.compType = CPA_DC_DEFLATE;
    if (qz_sess->sess_params.huffman_hdr == QZ_DYNAMIC_HDR) {
        qz_sess->session_setup_data.huffType = CPA_DC_HT_FULL_DYNAMIC;
    } else {
        qz_sess->session_setup_data.huffType = CPA_DC_HT_STATIC;
    }

    qz_sess->session_setup_data.fileType = CPA_DC_FT_ASCII;
    qz_sess->session_setup_data.autoSelectBestHuffmanTree =
        CPA_DC_ASB_UNCOMP_STATIC_DYNAMIC_WITH_STORED_HDRS;

    switch (qz_sess->sess_params.direction) {
    case QZ_DIR_COMPRESS:
        qz_sess->session_setup_data.sessDirection = CPA_DC_DIR_COMPRESS;
        break;
    case QZ_DIR_DECOMPRESS:
        qz_sess->session_setup_data.sessDirection = CPA_DC_DIR_DECOMPRESS;
        break;
    default:
        qz_sess->session_setup_data.sessDirection = CPA_DC_DIR_COMBINED;
    }

    qz_sess->session_setup_data.sessState = CPA_DC_STATELESS;
    qz_sess->session_setup_data.deflateWindowSize = (Cpa32U)7;
    qz_sess->session_setup_data.checksum = CPA_DC_CRC32;

    if (g_process.qz_init_status == QZ_NONE) {
        sess->hw_session_stat = QZ_NONE;
    } else if (g_process.qz_init_status != QZ_OK) {
        /*hw not present*/
        if (qz_sess->sess_params.sw_backup == 1) {
            sess->hw_session_stat = QZ_NO_HW;
        } else {
            sess->hw_session_stat = QZ_NOSW_NO_HW;
        }
    } else {
        sess->hw_session_stat = QZ_OK;
    }

    return sess->hw_session_stat;
}

/* Set up the QAT session associate with current process's
 * QAT instance
 */
int qzSetupHW(QzSession_T *sess, int i)
{
    QzSess_T *qz_sess;
    int rc = QZ_OK;

    if (g_process.qz_init_status != QZ_OK) {
        /*hw not present*/
        return g_process.qz_init_status;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qz_sess->inst_hint = i;
    qz_sess->seq = 0;
    qz_sess->seq_in = 0;

    if (0 ==  g_process.qz_inst[i].mem_setup) {
        rc = getInstMem(i, &(qz_sess->sess_params));
        if (QZ_OK != rc) {
            goto done_sess;
        }
    }

    if (0 ==  g_process.qz_inst[i].cpa_sess_setup) {
        /*setup and start DC session*/
        QZ_DEBUG("setup and start DC session %d\n", i);

        if (CPA_FALSE == g_process.qz_inst[i].instance_cap.dynamicHuffman) {
            qz_sess->session_setup_data.huffType = CPA_DC_HT_STATIC;
        }

        qz_sess->sess_status =
            cpaDcGetSessionSize(g_process.dc_inst_handle[i],
                                &qz_sess->session_setup_data,
                                &qz_sess->session_size,
                                &qz_sess->ctx_size);
        if (CPA_STATUS_SUCCESS == qz_sess->sess_status) {
            g_process.qz_inst[i].cpaSess = qzMalloc((size_t)(qz_sess->session_size),
                                                    NODE_0, PINNED_MEM);
            if (NULL ==  g_process.qz_inst[i].cpaSess) {
                rc = qz_sess->sess_params.sw_backup ? QZ_LOW_MEM : QZ_NOSW_LOW_MEM;
                goto done_sess;
            }
        } else {
            rc = QZ_FAIL;
            goto done_sess;
        }

        QZ_DEBUG("cpaDcInitSession %d\n", i);
        qz_sess->sess_status =
            cpaDcInitSession(g_process.dc_inst_handle[i],
                             g_process.qz_inst[i].cpaSess,
                             &qz_sess->session_setup_data,
                             NULL,
                             dcCallback);
        if (qz_sess->sess_status != CPA_STATUS_SUCCESS) {
            rc = QZ_FAIL;
        }
    }

    if (rc == QZ_OK) {
        g_process.qz_inst[i].cpa_sess_setup = 1;
    }

done_sess:
    return rc;
}

/* The internal function to send the comrpession request
 * to the QAT hardware
 */
static void *doCompressIn(void *in)
{
    unsigned long tag;
    int i, j;
    unsigned int done = 0;
    unsigned int remaining;
    unsigned int src_send_sz;
    unsigned char *src_ptr, *dest_ptr;
    unsigned int src_sz;
    CpaStatus rc;
    int src_pinned, dest_pinned;
    QzDataFormat_T data_fmt;
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    CpaDcOpData opData = (const CpaDcOpData) {0};
    struct timespec my_time;

    my_time.tv_sec = 0;
    my_time.tv_nsec = GET_BUFFER_SLEEP_NSEC;
    opData.inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData.outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    QZ_DEBUG("Always enable CnV\n");
    opData.compressAndVerify = CPA_TRUE;

    i = qz_sess->inst_hint;
    j = -1;
    src_ptr = qz_sess->src + qz_sess->qz_in_len;
    dest_ptr = qz_sess->next_dest;
    src_pinned = qzMemFindAddr(src_ptr);
    dest_pinned = qzMemFindAddr(dest_ptr);
    remaining = *qz_sess->src_sz - qz_sess->qz_in_len;
    src_sz = qz_sess->sess_params.hw_buff_sz;
    data_fmt = qz_sess->sess_params.data_fmt;
    opData.flushFlag = IS_DEFLATE(data_fmt) ? CPA_DC_FLUSH_FULL :
                       CPA_DC_FLUSH_FINAL;
    QZ_DEBUG("doCompressIn: Need to g_process %ld bytes\n", remaining);

    while (!done) {
        do {
            j = getUnusedBuffer(i, j);
            if (-1 == j) {
                nanosleep(&my_time, NULL);
            }
        } while (-1 == j);
        QZ_DEBUG("getUnusedBuffer returned %d\n", j);

        g_process.qz_inst[i].stream[j].src1++; /*this buffer is in use*/
        src_send_sz = (remaining < src_sz) ? remaining : src_sz;
        if (IS_DEFLATE(data_fmt) &&
            1 == qz_sess->last &&
            remaining <= src_sz) {
            opData.flushFlag = CPA_DC_FLUSH_FINAL;
        }
        g_process.qz_inst[i].stream[j].seq = qz_sess->seq; /*this buffer is in use*/
        QZ_DEBUG("sending seq number %d %d %ld, opData.flushFlag %d\n", i, j,
                 qz_sess->seq, opData.flushFlag);
        qz_sess->seq++;
        qz_sess->submitted++;
        /*send to compression engine here*/
        g_process.qz_inst[i].stream[j].src2++; /*this buffer is in use*/
        /*set up src dest buffers*/
        g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes
            = *(qz_sess->dest_sz);

        if (0 == src_pinned) {
            QZ_DEBUG("memory copy in doCompressIn\n");
            QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                      src_ptr,
                      src_send_sz,
                      src_send_sz);
            g_process.qz_inst[i].stream[j].src_pinned = 0;
        } else {
            QZ_DEBUG("changing src_ptr to 0x%lx\n", (unsigned long)src_ptr);
            g_process.qz_inst[i].stream[j].src_pinned = 1;
            g_process.qz_inst[i].stream[j].orig_src =
                g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;
            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
        }

        /*using zerocopy for the first request while dest buffer is pinned*/
        if (dest_pinned && (0 == g_process.qz_inst[i].stream[j].seq)) {
            g_process.qz_inst[i].stream[j].orig_dest =
                g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                dest_ptr + outputHeaderSz(data_fmt);
            g_process.qz_inst[i].stream[j].dest_pinned = 1;
        }

        g_process.qz_inst[i].stream[j].res.checksum = 0;
        do {
            tag = (i << 16) | j;
            QZ_DEBUG("Comp Sending %u bytes ,opData.flushFlag = %d, i = %ld j = %d seq = %ld tag = %ld\n",
                     g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes, opData.flushFlag,
                     i, j, g_process.qz_inst[i].stream[j].seq, tag);
            rc = cpaDcCompressData2(g_process.dc_inst_handle[i],
                                    g_process.qz_inst[i].cpaSess,
                                    g_process.qz_inst[i].src_buffers[j],
                                    g_process.qz_inst[i].dest_buffers[j],
                                    &opData,
                                    &g_process.qz_inst[i].stream[j].res,
                                    (void *)(tag));
            if (CPA_STATUS_RETRY == rc) {
                g_process.qz_inst[i].num_retries++;
                usleep(qz_sess->sess_params.poll_sleep);
            }

            if (g_process.qz_inst[i].num_retries > MAX_NUM_RETRY) {
                QZ_ERROR("instance %d retry count:%d exceed the max count: %d\n",
                         i, g_process.qz_inst[i].num_retries, MAX_NUM_RETRY);
                goto err_exit;
            }
        } while (rc == CPA_STATUS_RETRY);

        if (CPA_STATUS_SUCCESS != rc) {
            QZ_ERROR("Error in cpaDcCompressData: %d\n", rc);
            goto err_exit;
        }

        QZ_DEBUG("remaining = %u, src_send_sz = %u, seq = %ld\n", remaining,
                 src_send_sz,  qz_sess->seq);
        g_process.qz_inst[i].num_retries = 0;
        src_ptr += src_send_sz;
        remaining -= src_send_sz;

        if (qz_sess->stop_submitting) {
            remaining = 0;
        }

        if (0 == remaining) {
            done = 1;
            qz_sess->last_submitted = 1;
        }
    }

    return ((void *)NULL);

err_exit:
    /*roll back last submit*/
    qz_sess->last_submitted = 1;
    qz_sess->submitted -= 1;
    g_process.qz_inst[i].stream[j].src1 -= 1;
    g_process.qz_inst[i].stream[j].src2 -= 1;
    qz_sess->seq -= 1;
    sess->thd_sess_stat = QZ_FAIL;
    if (1 == g_process.qz_inst[i].stream[j].dest_pinned &&
        (0 == g_process.qz_inst[i].stream[j].seq)) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_pinned = 0;
    }
    if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_src;
        g_process.qz_inst[i].stream[j].src_pinned = 0;
    }
    return ((void *)NULL);
}

/* The internal function to g_process the comrpession response
 * from the QAT hardware
 */
static void *doCompressOut(void *in)
{
    int i = 0, j = 0;
    int good = -1;
    CpaDcRqResults *resl;
    CpaStatus sts;
    unsigned int sleep_cnt = 0;
    QzSession_T *sess = (QzSession_T *) in;
    QzSess_T *qz_sess = (QzSess_T *) sess->internal;
    long dest_avail_len = (long)(*qz_sess->dest_sz - qz_sess->qz_out_len);
    int dest_pinned = qzMemFindAddr(qz_sess->next_dest);
    i = qz_sess->inst_hint;
    QzDataFormat_T data_fmt = qz_sess->sess_params.data_fmt;

    while ((qz_sess->last_submitted == 0) ||
           (qz_sess->processed < qz_sess->submitted)) {

        /*Poll for responses*/
        good = 0;
        sts = icp_sal_DcPollInstance(g_process.dc_inst_handle[i], 1);
        if (CPA_STATUS_FAIL == sts) {
            QZ_ERROR("Error in DcPoll: %d\n", sts);
            sess->thd_sess_stat = QZ_FAIL;
            goto err_exit;
        }

        /*fake a retrieve*/
        for (j = 0; j <  g_process.qz_inst[i].dest_count; j++) {
            if ((g_process.qz_inst[i].stream[j].seq ==
                 qz_sess->seq_in)                    &&
                (g_process.qz_inst[i].stream[j].src1 ==
                 g_process.qz_inst[i].stream[j].src2) &&
                (g_process.qz_inst[i].stream[j].sink1 ==
                 g_process.qz_inst[i].stream[j].src1)  &&
                (g_process.qz_inst[i].stream[j].sink1 ==
                 g_process.qz_inst[i].stream[j].sink2 + 1)) {

                good = 1;
                QZ_DEBUG("doCompressOut: Processing seqnumber %2.2d "
                         "%2.2d %4.4ld, PID: %p, TID: %p\n",
                         i, j, g_process.qz_inst[i].stream[j].seq,
                         getpid(), pthread_self());

                resl = &g_process.qz_inst[i].stream[j].res;
                if (CPA_STATUS_SUCCESS != g_process.qz_inst[i].stream[j].job_status &&
                    CPA_DC_VERIFY_ERROR != resl->status) {
                    QZ_ERROR("Error(%d) in callback: %ld, %ld, ReqStatus: %d\n",
                             g_process.qz_inst[i].stream[j].job_status, i, j,
                             g_process.qz_inst[i].stream[j].res.status);

                    qz_sess->processed++;
                    sess->thd_sess_stat = QZ_FAIL;
                    g_process.qz_inst[i].stream[j].sink2++;
                    goto err_exit;
                }

                assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
                qz_sess->seq_in++;
                QZ_DEBUG("\tconsumed = %d, produced = %d, seq_in = %ld\n",
                         resl->consumed, resl->produced, g_process.qz_inst[i].stream[j].seq);

                if (CPA_DC_VERIFY_ERROR == resl->status) {
                    long src_len;
                    int src_location;
                    long block_count;
                    int out_len;
                    // detected a CnV error
                    src_len = g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes;

                    {
                        // Create the stored block(s) for
                        // length of uncompressed soure data
                        //
                        src_location = 0;
                        // stored block must have a length <= 65535 bytes
                        // so the number of blocks to create must be determined
                        block_count = src_len / STORED_BLK_MAX_LEN;
                        if ((block_count * STORED_BLK_MAX_LEN) < src_len) {
                            block_count++;
                        }
                        QZ_DEBUG("CnV error detected.Creating %ld stored blocks for a total len of %ld\n",
                                 block_count, src_len);
                        //
                        // Need to check if there is enough space in the
                        // destination buffer.  stored byte imposes a 5 byte header
                        //
                        out_len = (outputHeaderSz(data_fmt) + (block_count * STORED_BLK_HDR_SZ) +
                                   src_len + outputFooterSz(data_fmt));
                        dest_avail_len -= out_len;
                        if (dest_avail_len < 0) {
                            QZ_ERROR("do_compress_out: inadequate output buffer length for stored block: %ld\n",
                                     (long)(*qz_sess->dest_sz));
                            sess->thd_sess_stat = QZ_BUF_ERROR;
                            g_process.qz_inst[i].stream[j].sink2++;
                            qz_sess->processed++;
                            goto err_exit;
                        }
                        resl->produced = (block_count * STORED_BLK_HDR_SZ) +
                                         src_len; // size of stored block
                        resl->consumed = src_len;  // size of stored block
                        qz_sess->qz_in_len += resl->consumed;
                        qz_sess->qz_out_len += out_len;
                        //
                        // set up gzip header location in dest buffer
                        // Since this is going to be a stored block,
                        // we already know how big it will be
                        //
                        QZ_DEBUG("Setting produced to %d\n", resl->produced);
                        outputHeaderGen(qz_sess->next_dest, resl, data_fmt);
                        qz_sess->next_dest += outputHeaderSz(data_fmt);
                        //
                        // process each stored block
                        //
                        resl->checksum = 0;
                        while (src_len > 0) {
                            int this_block_len;
                            this_block_len = src_len;
                            if (this_block_len > STORED_BLK_MAX_LEN) {
                                this_block_len = STORED_BLK_MAX_LEN;
                            }
                            src_len -= this_block_len;
                            // create store block header here
                            if (src_len == 0) {
                                //set bfinal bit + block type
                                *(unsigned char *)(qz_sess->next_dest) = 0x01;
                                QZ_DEBUG("Creating the final block\n");
                            } else {
                                // clear bfinal bit + block type
                                *(unsigned char *)(qz_sess->next_dest) = 0x00;
                                QZ_DEBUG("Creating the block without final bit\n");
                            }
                            //
                            //Add length and XOR of length
                            //to stored block header
                            //
                            qz_sess->next_dest++;
                            *(unsigned short *)(qz_sess->next_dest) = this_block_len;
                            qz_sess->next_dest += 2;
                            *(unsigned short *)(qz_sess->next_dest) = ~this_block_len;
                            qz_sess->next_dest += 2;
                            // copy the source data
                            QZ_MEMCPY(qz_sess->next_dest,
                                      &g_process.qz_inst[i].src_buffers[j]->pBuffers->pData[src_location],
                                      this_block_len,
                                      this_block_len);
                            // update the crc
                            resl->checksum = crc32(resl->checksum,
                                                   &g_process.qz_inst[i].src_buffers[j]->pBuffers->pData[src_location],
                                                   this_block_len);
                            // jump to next source data location
                            qz_sess->next_dest += this_block_len;
                            src_location += this_block_len;
                            QZ_DEBUG("src_len :%u this_block_len: %d this block checksum: %x\n",
                                     src_len, this_block_len, resl->checksum);
                        }
                        QZ_DEBUG("\tgzip checksum = 0x%x\n", resl->checksum);
                        QZ_DEBUG("\tlen = 0x%x\n", resl->produced);
                        //Append footer
                        outputFooterGen(qz_sess, resl, data_fmt);
                        qz_sess->next_dest += stdGzipFooterSz();
                        if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
                            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                                g_process.qz_inst[i].stream[j].orig_src;
                            g_process.qz_inst[i].stream[j].src_pinned = 0;
                        }

                        if (dest_pinned && (0 == g_process.qz_inst[i].stream[j].seq)) {
                            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                                g_process.qz_inst[i].stream[j].orig_dest;
                            g_process.qz_inst[i].stream[j].dest_pinned = 0;
                        }
                    }
                } else {
                    dest_avail_len -= (outputHeaderSz(data_fmt) + resl->produced + outputFooterSz(
                                           data_fmt));
                    if (dest_avail_len < 0) {
                        QZ_DEBUG("doCompressOut: inadequate output buffer length: %ld, outlen: %ld\n",
                                 (long)(*qz_sess->dest_sz), qz_sess->qz_out_len);
                        sess->thd_sess_stat = QZ_BUF_ERROR;
                        g_process.qz_inst[i].stream[j].sink2++;
                        qz_sess->processed++;
                        qz_sess->stop_submitting = 1;
                        continue;
                    }

                    outputHeaderGen(qz_sess->next_dest, resl, data_fmt);
                    qz_sess->next_dest += outputHeaderSz(data_fmt);
                    qz_sess->qz_out_len += outputHeaderSz(data_fmt);

                    if (dest_pinned && (0 == g_process.qz_inst[i].stream[j].seq)) {
                        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_dest;
                        g_process.qz_inst[i].stream[j].dest_pinned = 0;
                    } else {
                        QZ_MEMCPY(qz_sess->next_dest,
                                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                                  resl->produced,
                                  resl->produced);
                    }
                    qz_sess->next_dest += resl->produced;
                    qz_sess->qz_in_len += resl->consumed;

                    if (NULL != qz_sess->crc32) {
                        if (0 == *(qz_sess->crc32)) {
                            *(qz_sess->crc32) = resl->checksum;
                            QZ_DEBUG("crc32 1st blk is 0x%lX \n", *(qz_sess->crc32));
                        } else {
                            QZ_DEBUG("crc32 input 0x%lX, ", *(qz_sess->crc32));
                            *(qz_sess->crc32) =
                                crc32_combine(*(qz_sess->crc32), resl->checksum, resl->consumed);
                            QZ_DEBUG("Result 0x%lX, checksum 0x%X, consumed %u, produced %u\n",
                                     *(qz_sess->crc32), resl->checksum, resl->consumed, resl->produced);
                        }
                    }

                    qz_sess->qz_out_len += resl->produced;
                    outputFooterGen(qz_sess, resl, data_fmt);
                    qz_sess->next_dest += outputFooterSz(data_fmt);
                    qz_sess->qz_out_len += outputFooterSz(data_fmt);

                    if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
                        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_src;
                        g_process.qz_inst[i].stream[j].src_pinned = 0;
                    }
                }

                g_process.qz_inst[i].stream[j].sink2++;
                qz_sess->processed++;
                break;
            }
        }

        if (good == 0) {
            QZ_DEBUG("comp sleep for %u usec...\n", qz_sess->sess_params.poll_sleep);
            usleep(qz_sess->sess_params.poll_sleep);
            sleep_cnt++;
        }

    }

    QZ_DEBUG("Comp sleep_cnt: %u\n", sleep_cnt);
    if (qz_sess->stop_submitting || qz_sess->last_submitted) {
        qz_sess->last_processed = 1;
    } else {
        qz_sess->last_processed = 0;
    }

    return NULL;

err_exit:
    qz_sess->stop_submitting = 1;
    for (j = 0; j < g_process.qz_inst[i].dest_count; j++) {
        if (1 == g_process.qz_inst[i].stream[j].dest_pinned &&
            (0 == g_process.qz_inst[i].stream[j].seq)) {
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                g_process.qz_inst[i].stream[j].orig_dest;
            g_process.qz_inst[i].stream[j].dest_pinned = 0;
        }
        if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                g_process.qz_inst[i].stream[j].orig_src;
            g_process.qz_inst[i].stream[j].src_pinned = 0;
        }
    }
    qz_sess->last_processed = 1;
    return NULL;
}


static int checkSessionState(QzSession_T *sess)
{
    int rc;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;

    if (qz_sess->stop_submitting) {
        return QZ_FAIL;
    }

    switch (sess->thd_sess_stat) {
    case QZ_FAIL:
    case QZ_NOSW_LOW_MEM:
        rc = QZ_FAIL;
        break;
    case QZ_OK:
    case QZ_LOW_MEM:
    case QZ_FORCE_SW:
        rc = QZ_OK;
        break;
    case QZ_BUF_ERROR:
    case QZ_DATA_ERROR:
        rc = sess->thd_sess_stat;
        break;
    default:
        rc = QZ_FAIL;
    }

    return rc;
}

unsigned char getSwBackup(QzSession_T *sess)
{
    QzSess_T *qz_sess;

    if (sess->internal != NULL) {
        qz_sess = (QzSess_T *)sess->internal;
        return qz_sess->sess_params.sw_backup;
    } else {
        return g_sess_params_default.sw_backup;
    }
}

/* The QATzip compression API */
int qzCompress(QzSession_T *sess, const unsigned char *src,
               unsigned int *src_len, unsigned char *dest,
               unsigned int *dest_len, unsigned int last)
{
    int rc = QZ_FAIL;
    QzSess_T *qz_sess = NULL;

    if (NULL == sess || (last != 0 && last != 1)) {
        return QZ_PARAMS;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return rc;
        }
    }
    qz_sess = (QzSess_T *)(sess->internal);
    qz_sess->sess_params.data_fmt = QZ_DEFLATE_GZIP_EXT;
    return qzCompressCrc(sess, src, src_len, dest, dest_len, last, NULL);
}

int qzCompressCrc(QzSession_T *sess, const unsigned char *src,
                  unsigned int *src_len, unsigned char *dest,
                  unsigned int *dest_len, unsigned int last, unsigned long *crc)
{
    int i, reqcnt;
    unsigned int out_len;
    QzSess_T *qz_sess;
    int rc;

    if (NULL == sess     || \
        NULL == src      || \
        NULL == src_len  || \
        NULL == dest     || \
        NULL == dest_len || \
        (last != 0 && last != 1)) {
        return QZ_PARAMS;
    }


    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        return rc;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return rc;
        }
    }

    qz_sess = (QzSess_T *)(sess->internal);

    QzDataFormat_T data_fmt = qz_sess->sess_params.data_fmt;
    if (data_fmt != QZ_DEFLATE_RAW &&
        data_fmt != QZ_DEFLATE_GZIP_EXT) {
        QZ_ERROR("Unknown data formt: %d\n", data_fmt);
        return QZ_PARAMS;
    }
    QZ_DEBUG("qzCompressCrc data_fmt: %d, input crc32 is 0x%lX\n",
             data_fmt, crc ? *crc : 0);

    qz_sess->crc32 = crc;

    if (*src_len < qz_sess->sess_params.input_sz_thrshold ||
        g_process.qz_init_status == QZ_NO_HW              ||
        sess->hw_session_stat == QZ_NO_HW                 ||
        qz_sess->sess_params.comp_lvl == 9) {
        QZ_DEBUG("compression src_len=%u, sess_params.input_sz_thrshold = %u, "
                 "process.qz_init_status = %d, sess->hw_session_stat = %d, "
                 "qz_sess->sess_params.comp_lvl = %d, switch to software.\n",
                 *src_len, qz_sess->sess_params.input_sz_thrshold,
                 g_process.qz_init_status, sess->hw_session_stat,
                 qz_sess->sess_params.comp_lvl);
        goto sw_compression;
    } else if (sess->hw_session_stat != QZ_OK &&
               sess->hw_session_stat != QZ_NO_INST_ATTACH) {
        return sess->hw_session_stat;
    }

    i = qzGrabInstance(qz_sess->inst_hint);
    if (i == -1) {
        if (qz_sess->sess_params.sw_backup == 1) {
            goto sw_compression;
        } else {
            sess->hw_session_stat = QZ_NO_INST_ATTACH;
            return QZ_NOSW_NO_INST_ATTACH;
        }
        /*Make this a s/w compression*/
    }

    QZ_DEBUG("qzCompress: inst is %d\n", i);
    qz_sess->inst_hint = i;

    if (0 ==  g_process.qz_inst[i].mem_setup ||
        0 ==  g_process.qz_inst[i].cpa_sess_setup) {
        QZ_DEBUG("Getting HW resources  for inst %d\n", i);
        rc = qzSetupHW(sess, i);
        if (QZ_OK != rc) {
            qzReleaseInstance(i);
            if (QZ_LOW_MEM == rc || QZ_NO_INST_ATTACH == rc) {
                goto sw_compression;
            } else {
                return rc;
            }
        }
    }

#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), COMPRESSION, HW);
#endif
    sess->total_in = 0;
    sess->total_out = 0;
    sess->thd_sess_stat = QZ_OK;
    qz_sess->submitted = 0;
    qz_sess->processed = 0;
    qz_sess->last_submitted = 0;
    qz_sess->last_processed = 0;
    qz_sess->stop_submitting = 0;
    qz_sess->qz_in_len = 0;
    qz_sess->qz_out_len = 0;
    qz_sess->force_sw = 0;
    qz_sess->single_thread = 0;

    qz_sess->seq = 0;
    qz_sess->seq_in = 0;
    qz_sess->src = (unsigned char *)src;
    qz_sess->src_sz = src_len;
    qz_sess->dest_sz = dest_len;
    qz_sess->next_dest = (unsigned char *)dest;
    qz_sess->last = last;

    reqcnt = *src_len / qz_sess->sess_params.hw_buff_sz;
    if (*src_len % qz_sess->sess_params.hw_buff_sz) {
        reqcnt++;
    }

    if (reqcnt > qz_sess->sess_params.req_cnt_thrshold) {
        pthread_create(&(qz_sess->c_th_i), NULL, doCompressIn, (void *)sess);
        doCompressOut((void *)sess);
        pthread_join(qz_sess->c_th_i, NULL);
    } else {
        doCompressIn((void *)sess);
        doCompressOut((void *)sess);
    }

    qzReleaseInstance(i);
    out_len = qz_sess->next_dest - dest;
    QZ_DEBUG("PRoduced %d bytes\n", out_len);
    *dest_len = out_len;

    sess->total_in = qz_sess->qz_in_len;
    sess->total_out = qz_sess->qz_out_len;
    *src_len = GET_LOWER_32BITS(sess->total_in);
    QZ_DEBUG("\n********* total_in = %lu total_out = %lu src_len = %u dest_len = %u\n",
             sess->total_in, sess->total_out, *src_len, *dest_len);
    assert(*dest_len == sess->total_out);

    return sess->thd_sess_stat;

sw_compression:
    return qzSWCompress(sess, src, src_len, dest, dest_len, last);
}

/*To handle compression expansion*/
static void swapDataBuffer(unsigned long i, int j)
{
    Cpa8U *p_tmp_data;

    p_tmp_data = g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;
    g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;
    g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = p_tmp_data;
}

static int checkHeader(QzSess_T *qz_sess, unsigned char *src,
                       long src_avail_len, long dest_avail_len,
                       QzGzH_T *hdr)
{
    unsigned char *src_ptr = src;
    long src_send_sz, dest_recv_sz;

    if ((src_avail_len <= 0) || (dest_avail_len <= 0)) {
        QZ_DEBUG("doDecompressOut: insufficient %s buffer length\n",
                 (dest_avail_len <= 0) ? "destation" : "source");
        return QZ_BUF_ERROR;
    }

    if (QZ_OK != qzGzipHeaderExt(src_ptr, hdr)) {
        return QZ_FAIL;
    }

    if (1 == qz_sess->force_sw) {
        return QZ_FORCE_SW;
    }

    src_send_sz = (long)(hdr->extra.qz_e.dest_sz);
    dest_recv_sz = (long)(hdr->extra.qz_e.src_sz);
    if ((src_send_sz > DEST_SZ(qz_sess->sess_params.hw_buff_sz)) ||
        (dest_recv_sz > qz_sess->sess_params.hw_buff_sz)) {
        if (1 == qz_sess->sess_params.sw_backup) {
            qz_sess->force_sw = 1;
            return QZ_LOW_MEM;
        } else {
            return QZ_NOSW_LOW_MEM;
        }
    }

    if (src_send_sz > src_avail_len) {
        QZ_DEBUG("doDecompressOut: incomplete source buffer\n");
        return QZ_DATA_ERROR;
    } else if (dest_recv_sz > dest_avail_len) {
        QZ_DEBUG("doDecompressOut: insufficient destination buffer length\n");
        return QZ_BUF_ERROR;
    }

    return QZ_OK;
}

/* The internal function to send the decomrpession request
 * to the QAT hardware
 */
static void *doDecompressIn(void *in)
{
    unsigned long i, tag;
    int rc;
    int j;
    unsigned int done = 0;
    unsigned int remaining;
    unsigned int src_send_sz;
    unsigned int dest_receive_sz;
    long src_avail_len, dest_avail_len;
    long tmp_src_avail_len, tmp_dest_avail_len;
    unsigned char *src_ptr;
    unsigned char *dest_ptr;
    int src_pinned = 0;
    int dest_pinned = 0;
    QzGzH_T hdr = {{0}, 0};
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    StdGzF_T *qzFooter = NULL;
    struct timespec my_time;

    my_time.tv_sec = 0;
    my_time.tv_nsec = GET_BUFFER_SLEEP_NSEC;
    i = qz_sess->inst_hint;
    j = -1;
    src_ptr = qz_sess->src + qz_sess->qz_in_len;
    dest_ptr = qz_sess->next_dest;
    src_pinned = qzMemFindAddr(src_ptr);
    dest_pinned = qzMemFindAddr(dest_ptr);
    remaining = *qz_sess->src_sz - qz_sess->qz_in_len;
    src_avail_len = remaining;
    dest_avail_len = (long)(*qz_sess->dest_sz - qz_sess->qz_out_len);
    QZ_DEBUG("doDecompressIn: Need to g_process %ld bytes\n", remaining);

    while (!done) {

        QZ_DEBUG("src_avail_len is %ld, dest_avail_len is %ld\n",
                 src_avail_len, dest_avail_len);
        sess->thd_sess_stat = checkHeader(qz_sess,
                                          src_ptr,
                                          src_avail_len,
                                          dest_avail_len,
                                          &hdr);
        switch (sess->thd_sess_stat) {
        case QZ_NOSW_LOW_MEM:
        case QZ_DATA_ERROR:
        case QZ_BUF_ERROR:
        case QZ_FAIL:
            remaining = 0;
            break;

        case QZ_LOW_MEM:
        case QZ_FORCE_SW:
            tmp_src_avail_len = src_avail_len;
            tmp_dest_avail_len = dest_avail_len;
            rc = qzSWDecompress(sess,
                                src_ptr,
                                (unsigned int *)&tmp_src_avail_len,
                                dest_ptr,
                                (unsigned int *)&tmp_dest_avail_len);
            if (rc != QZ_OK) {
                sess->thd_sess_stat = rc;
                remaining = 0;
                break;
            }

            sess->total_in  += qz_sess->inflate_strm->total_in;
            sess->total_out += qz_sess->inflate_strm->total_out;
            src_ptr         += qz_sess->inflate_strm->total_in;
            dest_ptr        += qz_sess->inflate_strm->total_out;
            src_avail_len   -= qz_sess->inflate_strm->total_in;
            dest_avail_len  -= qz_sess->inflate_strm->total_out;
            remaining       -= qz_sess->inflate_strm->total_in;
            break;

        case QZ_OK:
            /*QZip decompression*/
            do {
                j = getUnusedBuffer(i, j);

                if (qz_sess->single_thread) {
                    if ((-1 == j) ||
                        ((0 == qz_sess->seq % qz_sess->sess_params.req_cnt_thrshold) &&
                         (qz_sess->seq > qz_sess->seq_in))) {
                        return ((void *) NULL);
                    }
                } else {
                    if (-1 == j) {
                        nanosleep(&my_time, NULL);
                    }
                }
            } while (-1 == j);

            QZ_DEBUG("getUnusedBuffer returned %d\n", j);

            g_process.qz_inst[i].stream[j].src1++;/*this buffer is in use*/
            swapDataBuffer(i, j);
            src_ptr += qzGzipHeaderSz();
            remaining -= qzGzipHeaderSz();
            src_send_sz = hdr.extra.qz_e.dest_sz;
            dest_receive_sz = hdr.extra.qz_e.src_sz;

            g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
                dest_receive_sz;
            QZ_DEBUG("doDecompressIn: Sending %ld bytes starting at 0x%lx\n",
                     src_send_sz, (unsigned long)src_ptr);

            /*this buffer is in use*/
            g_process.qz_inst[i].stream[j].seq = qz_sess->seq;
            qz_sess->seq++;
            QZ_DEBUG("sending seq number %d %d %ld\n", i, j, qz_sess->seq);

            qzFooter = (StdGzF_T *)(src_ptr + src_send_sz);
            g_process.qz_inst[i].stream[j].gzip_footer_checksum = qzFooter->crc32;
            g_process.qz_inst[i].stream[j].gzip_footer_orgdatalen = qzFooter->i_size;
            qz_sess->submitted++;
            /*send to compression engine here*/
            g_process.qz_inst[i].stream[j].src2++;/*this buffer is in use*/

            /*set up src dest buffers*/
            if (0 == src_pinned) {
                QZ_DEBUG("memory copy in doDecompressIn\n");
                QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                          src_ptr,
                          src_send_sz,
                          src_send_sz);
                g_process.qz_inst[i].stream[j].src_pinned = 0;
            } else {
                g_process.qz_inst[i].stream[j].src_pinned = 1;
                g_process.qz_inst[i].stream[j].orig_src =
                    g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;
                g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
            }

            if (0 == dest_pinned) {
                g_process.qz_inst[i].stream[j].dest_pinned = 0;
            } else {
                g_process.qz_inst[i].stream[j].dest_pinned = 1;
                g_process.qz_inst[i].stream[j].orig_dest =
                    g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;
                g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = dest_ptr;
            }

            g_process.qz_inst[i].stream[j].res.checksum = 0;
            do {
                tag = (i << 16) | j;
                QZ_DEBUG("Decomp Sending i = %ld j = %d seq = %ld tag = %ld\n",
                         i, j, g_process.qz_inst[i].stream[j].seq, tag);

                rc = cpaDcDecompressData(g_process.dc_inst_handle[i],
                                         g_process.qz_inst[i].cpaSess,
                                         g_process.qz_inst[i].src_buffers[j],
                                         g_process.qz_inst[i].dest_buffers[j],
                                         &g_process.qz_inst[i].stream[j].res,
                                         CPA_DC_FLUSH_FINAL,
                                         (void *)(tag));
                QZ_DEBUG("mw>> %s():  DcDecompressData() rc = %d\n", __func__, rc);
                if (CPA_STATUS_RETRY == rc) {
                    g_process.qz_inst[i].num_retries++;
                    usleep(qz_sess->sess_params.poll_sleep);
                }

                if (g_process.qz_inst[i].num_retries > MAX_NUM_RETRY) {
                    QZ_ERROR("instance %d retry count:%d exceed the max count: %d\n",
                             i, g_process.qz_inst[i].num_retries, MAX_NUM_RETRY);
                    goto err_exit;
                }
            } while (rc == CPA_STATUS_RETRY);

            if (CPA_STATUS_SUCCESS != rc) {
                QZ_ERROR("Error in cpaDcCompressData: %d\n", rc);
                goto err_exit;
            }

            g_process.qz_inst[i].num_retries = 0;
            src_avail_len -= (qzGzipHeaderSz() + src_send_sz + stdGzipFooterSz());
            dest_avail_len -= dest_receive_sz;

            if (dest_pinned) {
                dest_ptr += dest_receive_sz;
            }

            src_ptr += (src_send_sz + stdGzipFooterSz());
            remaining -= (src_send_sz + stdGzipFooterSz());
            break;

        default:
            /*Exception handler*/
            remaining = 0;
            break;
        }

        if (qz_sess->stop_submitting) {
            remaining = 0;
        }

        QZ_DEBUG("src_ptr is %p, remaining is %ld\n", src_ptr, remaining);
        if (0 == remaining) {
            done = 1;
            qz_sess->last_submitted = 1;
        }
    }

    return ((void *)NULL);

err_exit:
    if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_src;
        g_process.qz_inst[i].stream[j].src_pinned = 0;
    }
    if (1 == g_process.qz_inst[i].stream[j].dest_pinned) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_pinned = 0;
    }
    /*roll back last submit*/
    qz_sess->last_submitted = 1;
    qz_sess->submitted -= 1;
    g_process.qz_inst[i].stream[j].src1 -= 1;
    g_process.qz_inst[i].stream[j].src2 -= 1;
    qz_sess->seq -= 1;
    sess->thd_sess_stat = QZ_FAIL;
    return ((void *)NULL);
}

/* The internal function to g_process the decomrpession response
 * from the QAT hardware
 */
static void *doDecompressOut(void *in)
{
    int i = 0, j = 0, si = 0, good;
    CpaDcRqResults *resl;
    CpaStatus sts;
    unsigned int sleep_cnt = 0;
    unsigned int done = 0;
    unsigned int src_send_sz;
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;

    QZ_DEBUG("mw>> function %s() called\n", __func__);
    fflush(stdout);
    i = qz_sess->inst_hint;

    while (!done) {
        /*Poll for responses*/
        good = 0;
        sts = icp_sal_DcPollInstance(g_process.dc_inst_handle[i], 1);
        if (CPA_STATUS_FAIL == sts) {
            QZ_ERROR("Error in DcPoll: %d\n", sts);
            sess->thd_sess_stat = QZ_FAIL;
            goto err_exit;
        }

        /*fake a retrieve*/
        for (j = 0; j <  g_process.qz_inst[i].dest_count; j++) {
            if ((g_process.qz_inst[i].stream[j].seq ==
                 qz_sess->seq_in) &&
                (g_process.qz_inst[i].stream[j].src1 ==
                 g_process.qz_inst[i].stream[j].src2) &&
                (g_process.qz_inst[i].stream[j].sink1 ==
                 g_process.qz_inst[i].stream[j].src1) &&
                (g_process.qz_inst[i].stream[j].sink1 ==
                 g_process.qz_inst[i].stream[j].sink2 + 1)) {
                good = 1;

                QZ_DEBUG("doDecompressOut: Processing seqnumber %2.2d %2.2d %4.4ld\n",
                         i, j, g_process.qz_inst[i].stream[j].seq);

                if (CPA_STATUS_SUCCESS != g_process.qz_inst[i].stream[j].job_status) {
                    QZ_ERROR("Error(%d) in callback: %ld, %ld, ReqStatus: %d\n",
                             g_process.qz_inst[i].stream[j].job_status, i, j,
                             g_process.qz_inst[i].stream[j].res.status);
                    goto err_exit;
                }

                assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
                qz_sess->seq_in++;
                resl = &g_process.qz_inst[i].stream[j].res;
                QZ_DEBUG("\tconsumed = %d, produced = %d, seq_in = %ld, src_send_sz = %ld\n",
                         resl->consumed, resl->produced, g_process.qz_inst[i].stream[j].seq,
                         g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes);

                if (0 == g_process.qz_inst[i].stream[j].dest_pinned) {
                    QZ_DEBUG("memory copy in doDecompressOut\n");
                    QZ_MEMCPY(qz_sess->next_dest,
                              g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                              resl->produced,
                              resl->produced);
                } else {
                    g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                        g_process.qz_inst[i].stream[j].orig_dest;
                    g_process.qz_inst[i].stream[j].dest_pinned = 0;
                }

                if (1 == g_process.qz_inst[i].stream[j].src_pinned) {
                    g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                        g_process.qz_inst[i].stream[j].orig_src;
                    g_process.qz_inst[i].stream[j].src_pinned = 0;
                }

                if (resl->checksum != g_process.qz_inst[i].stream[j].gzip_footer_checksum ||
                    resl->produced != g_process.qz_inst[i].stream[j].gzip_footer_orgdatalen) {
                    QZ_ERROR("Error in check footer, inst %ld, stream %ld\n", i, j);
                    QZ_DEBUG("resp checksum: %x data checksum %x\n",
                             resl->checksum,
                             g_process.qz_inst[i].stream[j].gzip_footer_checksum);
                    QZ_DEBUG("resp produced :%d data produced: %d\n",
                             resl->produced,
                             g_process.qz_inst[i].stream[j].gzip_footer_orgdatalen);
                    sess->thd_sess_stat = QZ_DATA_ERROR;
                    g_process.qz_inst[i].stream[j].sink2++;
                    qz_sess->processed++;
                    goto err_check_footer;
                }

                src_send_sz = g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes;
                qz_sess->next_dest += resl->produced;
                qz_sess->qz_in_len += (qzGzipHeaderSz() + src_send_sz + stdGzipFooterSz());
                qz_sess->qz_out_len += resl->produced;

                QZ_DEBUG("qz_sess->next_dest = %p\n", qz_sess->next_dest);

                swapDataBuffer(i, j); /*swap pdata back after decompress*/
                g_process.qz_inst[i].stream[j].sink2++;
                qz_sess->processed++;
                break;
            }
        }

        if (qz_sess->single_thread) {
            done = (qz_sess->processed == qz_sess->submitted);
        } else {
            done = (qz_sess->last_submitted) && (qz_sess->processed == qz_sess->submitted);
        }

        if (good == 0) {
            QZ_DEBUG("decomp sleep for %u usec...\n", qz_sess->sess_params.poll_sleep);
            usleep(qz_sess->sess_params.poll_sleep);
            sleep_cnt++;
        }
    }

    QZ_DEBUG("Decomp sleep_cnt: %u\n", sleep_cnt);
    qz_sess->last_processed = qz_sess->last_submitted ? 1 : 0;
    return NULL;

err_exit:
    for (si = 0; si < g_process.qz_inst[i].dest_count; si++) {
        if (1 == g_process.qz_inst[i].stream[si].src_pinned) {
            g_process.qz_inst[i].src_buffers[si]->pBuffers->pData =
                g_process.qz_inst[i].stream[si].orig_src;
            g_process.qz_inst[i].stream[si].src_pinned = 0;
        }
        if (1 == g_process.qz_inst[i].stream[si].dest_pinned) {
            g_process.qz_inst[i].dest_buffers[si]->pBuffers->pData =
                g_process.qz_inst[i].stream[si].orig_dest;
            g_process.qz_inst[i].stream[si].dest_pinned = 0;
        }
    }
err_check_footer:
    qz_sess->stop_submitting = 1;
    qz_sess->last_processed = 1;
    swapDataBuffer(i, j);
    return ((void *)NULL);
}

/* The internal function to process the single thread decompress
 * in qzDecompress()
 */
static void *doQzDecompressSingleThread(void *in)
{
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    qz_sess->last_processed = 0;
    while (!qz_sess->last_processed) {
        doDecompressIn((void *)sess);
        doDecompressOut((void *)sess);
    }
    return NULL;
}

/* The QATzip decompression API */
int qzDecompress(QzSession_T *sess, const unsigned char *src,
                 unsigned int *src_len, unsigned char *dest,
                 unsigned int *dest_len)
{
    int rc;
    int i, reqcnt;
    QzSess_T *qz_sess;
    QzGzH_T *hdr = (QzGzH_T *)src;

    if (NULL == sess                 || \
        NULL == src                  || \
        NULL == src_len              || \
        NULL == dest                 || \
        NULL == dest_len) {
        return QZ_PARAMS;
    }

    if (0 == *src_len) {
        *dest_len = 0;
        return QZ_OK;
    }

    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        return rc;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        rc = qzSetupSession(sess, NULL);
        if (QZ_SETUP_SESSION_FAIL(rc)) {
            return rc;
        }
    }

    qz_sess = (QzSess_T *)(sess->internal);

    QzDataFormat_T data_fmt = qz_sess->sess_params.data_fmt;
    if (data_fmt != QZ_DEFLATE_RAW &&
        data_fmt != QZ_DEFLATE_GZIP &&
        data_fmt != QZ_DEFLATE_GZIP_EXT) {
        QZ_ERROR("Unknown data formt: %d\n", data_fmt);
        return QZ_PARAMS;
    }

    QZ_DEBUG("qzDecompress data_fmt: %d\n", data_fmt);
    if (hdr->extra.qz_e.src_sz < qz_sess->sess_params.input_sz_thrshold ||
        g_process.qz_init_status == QZ_NO_HW                            ||
        sess->hw_session_stat == QZ_NO_HW                               ||
        isStdGzipHeader(src)                                            ||
        qz_sess->inflate_stat == InflateOK                              ||
        data_fmt != QZ_DEFLATE_GZIP_EXT) {
        QZ_DEBUG("decompression src_len=%u, hdr->extra.qz_e.src_sz = %u, "
                 "g_process.qz_init_status = %d, sess->hw_session_stat = %d, "
                 "isStdGzipHeader = %d, switch to software.\n",
                 *src_len,  hdr->extra.qz_e.src_sz,
                 g_process.qz_init_status, sess->hw_session_stat,
                 isStdGzipHeader(src));
        goto sw_decompression;
    } else if (sess->hw_session_stat != QZ_OK &&
               sess->hw_session_stat != QZ_NO_INST_ATTACH) {
        return sess->hw_session_stat;
    }

    i = qzGrabInstance(qz_sess->inst_hint);
    if (i == -1) {
        if (qz_sess->sess_params.sw_backup == 1) {
            goto sw_decompression;
        } else {
            sess->hw_session_stat = QZ_NO_INST_ATTACH;
            return QZ_NOSW_NO_INST_ATTACH;
        }
        /*Make this a s/w compression*/
    }
    QZ_DEBUG("qzDecompress: inst is %d\n", i);
    qz_sess->inst_hint = i;

    if (0 ==  g_process.qz_inst[i].mem_setup ||
        0 ==  g_process.qz_inst[i].cpa_sess_setup) {
        QZ_DEBUG("Getting HW resources for inst %d\n", i);
        rc = qzSetupHW(sess, i);
        if (QZ_OK != rc) {
            qzReleaseInstance(i);
            if (QZ_LOW_MEM == rc || QZ_NO_INST_ATTACH == rc) {
                goto sw_decompression;
            } else {
                return rc;
            }
        }
    }

#ifdef QATZIP_DEBUG
    insertThread((unsigned int)pthread_self(), DECOMPRESSION, HW);
#endif
    sess->total_in = 0;
    sess->total_out = 0;
    sess->thd_sess_stat = QZ_OK;
    qz_sess->submitted = 0;
    qz_sess->processed = 0;
    qz_sess->last_submitted = 0;
    qz_sess->last_processed = 0;
    qz_sess->stop_submitting = 0;
    qz_sess->qz_in_len = 0;
    qz_sess->qz_out_len = 0;
    qz_sess->force_sw = 0;
    qz_sess->single_thread = 0;

    qz_sess->src = (unsigned char *)src;
    qz_sess->src_sz = src_len;
    qz_sess->dest_sz = dest_len;
    qz_sess->next_dest = (unsigned char *)dest;

    reqcnt = *src_len / (qz_sess->sess_params.hw_buff_sz / 2);
    if (*src_len % (qz_sess->sess_params.hw_buff_sz / 2)) {
        reqcnt++;
    }

    if (reqcnt > qz_sess->sess_params.req_cnt_thrshold) {
        qz_sess->single_thread = 0;
        pthread_create(&(qz_sess->c_th_i), NULL, doDecompressIn, (void *)sess);
        doDecompressOut((void *)sess);
        pthread_join(qz_sess->c_th_i, NULL);
    } else {
        qz_sess->single_thread = 1;
        doQzDecompressSingleThread((void *)sess);
    }

    qzReleaseInstance(i);

    QZ_DEBUG("PRoduced %d bytes\n", sess->total_out);
    rc = checkSessionState(sess);

    sess->total_in += qz_sess->qz_in_len;
    sess->total_out += qz_sess->qz_out_len;
    *src_len = GET_LOWER_32BITS(sess->total_in);
    *dest_len = GET_LOWER_32BITS(sess->total_out);

    QZ_DEBUG("total_in=%lu total_out=%lu src_len=%u dest_len=%u rc=%d src_len=%d dest_len=%d\n",
             sess->total_in, sess->total_out, *src_len, *dest_len, rc, *src_len, *dest_len);
    return rc;

sw_decompression:
    return qzSWDecompressMultiGzip(sess, src, src_len, dest, dest_len);
}

int qzTeardownSession(QzSession_T *sess)
{
    if (sess == NULL) {
        return QZ_PARAMS;
    }

    if (NULL != sess->internal) {
        QzSess_T *qz_sess = (QzSess_T *) sess->internal;
        if (NULL != qz_sess->inflate_strm) {
            inflateEnd(qz_sess->inflate_strm);
            free(qz_sess->inflate_strm);
            qz_sess->inflate_strm = NULL;
        }

        free(sess->internal);
        sess->internal = NULL;
    }

    return QZ_OK;
}

void removeSession(int i)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    if (0 == g_process.qz_inst[i].cpa_sess_setup) {
        return;
    }

    /* Remove session */
    if ((NULL != g_process.dc_inst_handle[i]) &&
        (NULL != g_process.qz_inst[i].cpaSess)) {
        status = cpaDcRemoveSession(g_process.dc_inst_handle[i],
                                    g_process.qz_inst[i].cpaSess);
        if (CPA_STATUS_SUCCESS == status) {
            /* Deallocate session memory */
            qzFree(g_process.qz_inst[i].cpaSess);
            g_process.qz_inst[i].cpaSess = NULL;
            g_process.qz_inst[i].cpa_sess_setup = 0;
        } else {
            QZ_ERROR("ERROR in Remove Instance %d session\n", i);
        }
    }
}

int qzClose(QzSession_T *sess)
{
    int i;

    if (sess == NULL) {
        return QZ_PARAMS;
    }

    if (0 != pthread_mutex_lock(&g_lock)) {
        return QZ_FAIL;
    }

    for (i = 0; i <  g_process.num_instances; i++) {
        removeSession(i);
        cleanUpInstMem(i);
    }

    stopQat();
    if (0 != pthread_mutex_unlock(&g_lock)) {
        return QZ_FAIL;
    }

    return QZ_OK;
}

int qzGetStatus(QzSession_T *sess, QzStatus_T *status)
{
    if (sess == NULL || status == NULL) {
        return QZ_PARAMS;
    }

    return QZ_OK;
}

int qzSetDefaults(QzSessionParams_T *defaults)
{
    int ret = QZ_PARAMS;

    if (qz_sessParamsCheck(defaults) == SUCCESS) {
        QZ_MEMCPY(&g_sess_params_default,
                  defaults,
                  sizeof(QzSessionParams_T),
                  sizeof(QzSessionParams_T));
        ret = QZ_OK;
    }

    return ret;
}

int qzGetDefaults(QzSessionParams_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    QZ_MEMCPY(defaults,
              &g_sess_params_default,
              sizeof(QzSessionParams_T),
              sizeof(QzSessionParams_T));
    return QZ_OK;
}

unsigned int qzMaxCompressedLength(unsigned int src_sz)
{
    unsigned int dest_sz = 0;
    unsigned int qz_header_footer_sz = qzGzipHeaderSz() + stdGzipFooterSz();

    unsigned int chunk_cnt = src_sz / QZ_HW_BUFF_SZ;
    unsigned int max_chunk_sz = ((9 * QZ_HW_BUFF_SZ + 7) / 8) + QZ_SKID_PAD_SZ +
                                qz_header_footer_sz;
    dest_sz =  max_chunk_sz * chunk_cnt;

    unsigned int last_chunk_sz = src_sz % QZ_HW_BUFF_SZ;
    if (last_chunk_sz) {
        dest_sz += ((9 * last_chunk_sz + 7) / 8) + QZ_SKID_PAD_SZ + qz_header_footer_sz;
    }
    QZ_DEBUG("src_sz is %u, dest_sz is %u\n", src_sz, (unsigned int)dest_sz);

    return dest_sz;
}
