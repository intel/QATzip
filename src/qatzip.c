/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
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
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <bits/types.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#define XXH_NAMESPACE QATZIP_
#include "xxhash.h"

#ifdef HAVE_QAT_HEADERS
#include <qat/cpa.h>
#include <qat/cpa_dc.h>
#include <qat/icp_sal_poll.h>
#include <qat/icp_sal_user.h>
#include <qat/qae_mem.h>
#else
#include <cpa.h>
#include <cpa_dc.h>
#include <icp_sal_poll.h>
#include <icp_sal_user.h>
#include <qae_mem.h>
#endif
#include "qatzip.h"
#include "qatzip_internal.h"
#include "qz_utils.h"

/*
 * Process address space name described in the config file for this device.
 */
const char *g_dev_tag = "SHIM";

const unsigned int g_polling_interval[] = { 10, 10, 20, 30, 60, 100, 200, 400,
                                            600, 1000, 2000, 4000, 8000, 16000,
                                            32000, 64000
                                          };

#define INTER_SZ(src_sz)          (2 * (src_sz))
#define msleep(x)                 usleep((x) * 1000)

#define POLLING_LIST_NUM          (sizeof(g_polling_interval) \
                                    / sizeof(unsigned int))
#define MAX_GRAB_RETRY            (10)

#define IS_DEFLATE(fmt)  (DEFLATE_RAW == (fmt))
#define IS_DEFLATE_OR_GZIP(fmt) \
        (DEFLATE_RAW == (fmt) || DEFLATE_GZIP == (fmt))

#define GET_BUFFER_SLEEP_NSEC   10
#define QAT_SECTION_NAME_SIZE   32

#define RESTORE_DEST_CPASTREAM_BUFFER(i, j)                                 \
do {                                                                        \
    if (g_process.qz_inst[i].stream[j].dest_need_reset) {                   \
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =             \
            g_process.qz_inst[i].stream[j].orig_dest;                       \
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;                 \
    }                                                                       \
}while (0);

#define RESTORE_SRC_CPASTREAM_BUFFER(i, j)                                  \
do {                                                                        \
    if (g_process.qz_inst[i].stream[j].src_need_reset) {                    \
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =              \
            g_process.qz_inst[i].stream[j].orig_src;                        \
        g_process.qz_inst[i].stream[j].src_need_reset = 0;                  \
    }                                                                       \
} while (0);

#define RESTORE_CPASTREAM_BUFFER(i, j)                                      \
do {                                                                        \
    RESTORE_SRC_CPASTREAM_BUFFER(i, j)                                      \
    RESTORE_DEST_CPASTREAM_BUFFER(i, j)                                     \
    qz_sess->processed++;                                                   \
    qz_sess->stop_submitting = 1;                                           \
    g_process.qz_inst[i].stream[j].sink2++;                                 \
} while (0);

#define RESTORE_SWAP_CPASTREAM_BUFFER(i, j)                                 \
do {                                                                        \
    RESTORE_SRC_CPASTREAM_BUFFER(i, j)                                      \
    RESTORE_DEST_CPASTREAM_BUFFER(i, j)                                     \
    swapDataBuffer(i, j);                                                   \
    qz_sess->processed++;                                                   \
    qz_sess->stop_submitting = 1;                                           \
    g_process.qz_inst[i].stream[j].sink2++;                                 \
} while (0);

/******************************************************
* If enable new compression algorithm, extend new params
* structure in API. please add default here.
******************************************************/
QzSessionParamsInternal_T g_sess_params_internal_default = {
    .huffman_hdr       = QZ_HUFF_HDR_DEFAULT,
    .direction         = QZ_DIRECTION_DEFAULT,
    .data_fmt          = DATA_FORMAT_DEFAULT,
    .comp_lvl          = QZ_COMP_LEVEL_DEFAULT,
    .comp_algorithm    = QZ_COMP_ALGOL_DEFAULT,
    .max_forks         = QZ_MAX_FORK_DEFAULT,
    .sw_backup         = QZ_SW_BACKUP_DEFAULT,
    .hw_buff_sz        = QZ_HW_BUFF_SZ,
    .strm_buff_sz      = QZ_STRM_BUFF_SZ_DEFAULT,
    .input_sz_thrshold = QZ_COMP_THRESHOLD_DEFAULT,
    .req_cnt_thrshold  = QZ_REQ_THRESHOLD_DEFAULT,
    .wait_cnt_thrshold = QZ_WAIT_CNT_THRESHOLD_DEFAULT,
    .polling_mode      = QZ_PERIODICAL_POLLING,
    .lz4s_mini_match   = 3,
    .qzCallback        = NULL,
    .qzCallback_external = NULL
};

processData_T g_process = {
    .qz_init_status = QZ_NONE,
    .qat_available = QZ_NONE
};
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

__thread ThreadData_T g_thread = {
    .ppid = 0,
};

static int setInstance(unsigned int dev_id, QzInstanceList_T *new_instance,
                       QzHardware_T *qat_hw)
{
    if (dev_id >= QAT_MAX_DEVICES ||
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
    if (dev_id >= QAT_MAX_DEVICES || NULL == qat_hw) {
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

/*
 * Check the capabilities of instance to ensure that it
 * supports the data format. For LZ4 or LZ4s if
 * QAT API Version is less than 3.1, will return QZ_FAIL.
 */
static inline int qzCheckInstCap(CpaDcInstanceCapabilities *inst_cap,
                                 DataFormatInternal_T data_fmt)
{
    assert(inst_cap != NULL);

    switch (data_fmt) {
    case LZ4_FH:
#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
        if (!inst_cap->statelessLZ4Compression ||
            !inst_cap->statelessLZ4Decompression ||
            !inst_cap->checksumXXHash32) {
            return QZ_FAIL;
        }
#else
        QZ_ERROR("QAT driver does not support lz4 algorithm\n");
        return QZ_FAIL;
#endif
        break;
    case LZ4S_BK:
#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
        if (!inst_cap->checksumXXHash32 ||
            !inst_cap->statelessLZ4SCompression) {
            return QZ_FAIL;
        }
#else
        QZ_ERROR("QAT driver does not support lz4s algorithm\n");
        return QZ_FAIL;
#endif
        break;
    case DEFLATE_4B:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_RAW:
    default:
        if (!inst_cap->statelessDeflateCompression ||
            !inst_cap->statelessDeflateDecompression ||
            !inst_cap->checksumCRC32) {
            return QZ_FAIL;
        }
        break;
    }
    return QZ_OK;
}

static int qzGrabInstance(int hint, DataFormatInternal_T data_fmt)
{
    int i, j, rc, f;

    if (QZ_NONE == g_process.qz_init_status) {
        return -1;
    }

    if (hint >= g_process.num_instances || hint < 0) {
        hint = 0;
    }

    /*otherwise loop through all of them*/

    f = 0;
    for (j = 0; j < MAX_GRAB_RETRY; j++) {
        for (i = 0; i < g_process.num_instances; i++) {
            if (f == 0) { i = hint; f = 1; } ;
            /* Before locking the instance, we need to ensure that
             * the instance supports the data format.
             */
            if (QZ_OK != qzCheckInstCap(&g_process.qz_inst[i].instance_cap,
                                        data_fmt)) {
                continue;
            }
            rc = __sync_lock_test_and_set(&(g_process.qz_inst[i].lock), 1);
            if (0 ==  rc) {
                return i;
            }
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

static void stopQat(void)
{
    int i;
    CpaStatus status = CPA_STATUS_SUCCESS;

    // Those all show No HW, stopQAT do nothing
    if (CPA_FALSE == g_process.qat_available ||
        QZ_NONE == g_process.qz_init_status  ||
        QZ_NO_HW == g_process.qz_init_status ||
        QZ_NOSW_NO_HW == g_process.qz_init_status) {
        goto reset;
    }

    // scenario: it's called from inside qzinit, Hw init failed
    if (QZ_NO_INST_ATTACH == g_process.qz_init_status) {
        if (NULL != g_process.dc_inst_handle) {
            free(g_process.dc_inst_handle);
            g_process.dc_inst_handle = NULL;
        }
        if (NULL != g_process.qz_inst) {
            free(g_process.qz_inst);
            g_process.qz_inst = NULL;
        }
        (void)icp_sal_userStop();
        goto reset;
    }

    QZ_DEBUG("Call stopQat.\n");

    // scenario: qzinit succeed. outside qzinit.
    if (QZ_OK == g_process.qz_init_status) {
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
    } else {
        QZ_ERROR("qz init status is invalid, status=%d\n",
                 g_process.qz_init_status);
        goto reset;
    }

reset:
    g_process.num_instances = (Cpa16U)0;
    g_process.qz_init_status = QZ_NONE;
    g_process.qat_available = QZ_NONE;
}

static void exitFunc(void) __attribute__((destructor));

static void exitFunc(void)
{
    int i = 0;

    for (i = 0; i <  g_process.num_instances; i++) {
        removeSession(i);
        cleanUpInstMem(i);
    }

    streamBufferCleanup();

    stopQat();
    qzMemDestory();
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
        return g_sess_params_internal_default.wait_cnt_thrshold;
    }
}

#define BACKOUT(hw_status)                                         \
    stopQat();                                                     \
    if (1 == sw_backup) {                                          \
        g_process.qz_init_status = QZ_NO_HW;                       \
        QZ_ERROR("g_process.qz_init_status = QZ_NO_HW\n");         \
        rc = QZ_OK;                                                \
    } else if (0 == sw_backup) {                                   \
        g_process.qz_init_status = QZ_NOSW_NO_HW;                  \
        QZ_ERROR("g_process.qz_init_status = QZ_NOSW_NO_HW\n");    \
        rc = hw_status;                                            \
    }                                                              \
    goto done;

#define QZ_HW_BACKOUT(hw_status)                                   \
    if(qat_hw) {                                                   \
        clearDevices(qat_hw);                                      \
        free(qat_hw);                                              \
    }                                                              \
    BACKOUT(hw_status);

const char *getSectionName(void)
{
    static char section_name[QAT_SECTION_NAME_SIZE];
    int len;
    char *pre_section_name;
#if __GLIBC_PREREQ(2, 17)
    pre_section_name = secure_getenv("QAT_SECTION_NAME");
#else
    pre_section_name = getenv("QAT_SECTION_NAME");
#endif

    if (!pre_section_name || !(len = strlen(pre_section_name))) {
        pre_section_name = (char *)g_dev_tag;
    } else if (len >= QAT_SECTION_NAME_SIZE) {
        QZ_ERROR("The length of QAT_SECTION_NAME exceeds the limit.\n");
    }
    strncpy(section_name, pre_section_name, QAT_SECTION_NAME_SIZE - 1);
    section_name[QAT_SECTION_NAME_SIZE - 1] = '\0';
    return section_name;
}

/* Initialize the QAT hardware, get the QAT instance for current
 * process.
 * Note: return value dosen't same as qz_init_status, because return
 * value align limitation.
 *
 * After qzInit, there only are three status to qz_init_status
 *      1. QZ_OK
 *      2. QZ_NO_HW
 *      3. QZ_NOSW_NO_HW
 */
int qzInit(QzSession_T *sess, unsigned char sw_backup)
{
    CpaStatus status;
    int rc = QZ_NOSW_NO_HW, i;
    unsigned int dev_id = 0;
    QzHardware_T *qat_hw = NULL;
    unsigned int instance_found = 0;
    static atomic_int waiting = 0;
    static atomic_int wait_cnt = 0;
#ifdef ADF_PCI_API
    Cpa32U pcie_count = 0;
#endif

    if (unlikely(sess == NULL)) {
        return QZ_PARAMS;
    }

    if (unlikely(sw_backup > 1)) {
        return QZ_PARAMS;
    }

    if (CPA_FALSE == g_process.qat_available ||
        QZ_OK == g_process.qz_init_status) {
        return QZ_DUPLICATE;
    }

    if (unlikely(0 != pthread_mutex_lock(&g_lock))) {
        return QZ_NOSW_NO_HW;
    }

    if (QZ_OK == g_process.qz_init_status) {
        if (unlikely(0 != pthread_mutex_unlock(&g_lock))) {
            return QZ_NOSW_NO_HW;
        }
        if (g_process.sw_backup != sw_backup) {
            g_process.sw_backup = sw_backup;
        }

        return QZ_DUPLICATE;
    }

    g_thread.pid = getpid();
    g_thread.ppid = getppid();
    init_timers();
    g_process.sw_backup = sw_backup;

    if (waiting && wait_cnt > 0) {
        wait_cnt--;
        BACKOUT(QZ_NOSW_NO_HW);
    }
    waiting = 0;
    // Start HW initilization. it could be first call qzinit or
    // Before HW init failed, which mean qz_init_status may be
    // QZ_NOSW_NO_HW or QZ_NO_HW

#ifdef SAL_DEV_API
    g_process.qat_available = icp_sal_userIsQatAvailable();
    if (CPA_FALSE == g_process.qat_available) {
        QZ_ERROR("Error no hardware, switch to SW if permitted\n");
        BACKOUT(QZ_NOSW_NO_HW);
    }
#else
    status = icp_adf_get_numDevices(&pcie_count);
    if (CPA_STATUS_SUCCESS != status) {
        g_process.qat_available = CPA_FALSE;
    }

    if (pcie_count >= 1) {
        g_process.qat_available = CPA_TRUE;
    } else {
        g_process.qat_available = CPA_FALSE;
    }

    if (CPA_FALSE == g_process.qat_available) {
        QZ_ERROR("Error no hardware, switch to SW if permitted status = %d\n", status);
        BACKOUT(QZ_NOSW_NO_HW);
    }
#endif

    status = icp_sal_userStartMultiProcess(getSectionName(), CPA_FALSE);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error userStarMultiProcess(%d), switch to SW if permitted\n",
                 status);
        waiting = 1;
        wait_cnt = getWaitCnt(sess);
        BACKOUT(QZ_NOSW_NO_HW);
    }

    // This status only show inside qzinit. And will replace to others
    // when qzinit finished.
    g_process.qz_init_status = QZ_NO_INST_ATTACH;

    status = cpaDcGetNumInstances(&g_process.num_instances);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error in cpaDcGetNumInstances status = %d\n", status);
        BACKOUT(QZ_NOSW_NO_INST_ATTACH);
    }
    QZ_DEBUG("Number of instance: %u\n", g_process.num_instances);

    g_process.dc_inst_handle =
        malloc(g_process.num_instances * sizeof(CpaInstanceHandle));
    g_process.qz_inst = calloc(g_process.num_instances, sizeof(QzInstance_T));
    if (unlikely(NULL == g_process.dc_inst_handle || NULL == g_process.qz_inst)) {
        QZ_ERROR("malloc failed\n");
        BACKOUT(QZ_NOSW_LOW_MEM);
    }

    status = cpaDcGetInstances(g_process.num_instances, g_process.dc_inst_handle);
    if (CPA_STATUS_SUCCESS != status) {
        QZ_ERROR("Error in cpaDcGetInstances status = %d\n", status);
        BACKOUT(QZ_NOSW_NO_INST_ATTACH);
    }

    qat_hw = calloc(1, sizeof(QzHardware_T));
    if (unlikely(NULL == qat_hw)) {
        QZ_ERROR("malloc failed\n");
        BACKOUT(QZ_NOSW_LOW_MEM);
    }
    for (i = 0; i < g_process.num_instances; i++) {
        QzInstanceList_T *new_inst = calloc(1, sizeof(QzInstanceList_T));
        if (unlikely(NULL == new_inst)) {
            QZ_ERROR("malloc failed\n");
            QZ_HW_BACKOUT(QZ_NOSW_LOW_MEM);
        }

        status = cpaDcInstanceGetInfo2(g_process.dc_inst_handle[i],
                                       &new_inst->instance.instance_info);
        if (CPA_STATUS_SUCCESS != status) {
            QZ_ERROR("Error in cpaDcInstanceGetInfo2 status = %d\n", status);
            free(new_inst);
            QZ_HW_BACKOUT(QZ_NOSW_NO_HW);
        }

        status = cpaDcQueryCapabilities(g_process.dc_inst_handle[i],
                                        &new_inst->instance.instance_cap);
        if (CPA_STATUS_SUCCESS != status) {
            QZ_ERROR("Error in cpaDcQueryCapabilities status = %d\n", status);
            free(new_inst);
            QZ_HW_BACKOUT(QZ_NOSW_NO_HW);
        }

        new_inst->instance.lock = 0;
        new_inst->instance.heartbeat = (time_t)0;
        new_inst->instance.mem_setup = 0;
        new_inst->instance.cpa_sess_setup = 0;
        new_inst->instance.num_retries = 0;
        new_inst->dc_inst_handle = g_process.dc_inst_handle[i];

        dev_id = new_inst->instance.instance_info.physInstId.packageId;
        if (unlikely(QZ_OK != setInstance(dev_id, new_inst, qat_hw))) {
            QZ_ERROR("Insert instance on device %d failed\n", dev_id);
            QZ_HW_BACKOUT(QZ_NOSW_NO_INST_ATTACH);
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

    rc = g_process.qz_init_status = QZ_OK;

done:
    initDebugLock();
    if (unlikely(0 != pthread_mutex_unlock(&g_lock))) {
        return QZ_NOSW_NO_HW;
    }

    return rc;
}

/* Free up the DMAable memory buffers used by QAT
 * internally, those buffers are source buffer,
 * intermeidate buffer and destination buffer
 */
void cleanUpInstMem(int i)
{
    int j;

    /*intermediate buffers*/
    if (NULL != g_process.qz_inst[i].intermediate_buffers) {
        for (j = 0; j < g_process.qz_inst[i].intermediate_cnt; j++) {
            if (NULL != g_process.qz_inst[i].intermediate_buffers[j]) {
                if (NULL != g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData) {
                    qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData);
                    g_process.qz_inst[i].intermediate_buffers[j]->pPrivateMetaData = NULL;
                }
                if (NULL != g_process.qz_inst[i].intermediate_buffers[j]->pBuffers) {
                    if (NULL != g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData) {
                        qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData);
                        g_process.qz_inst[i].intermediate_buffers[j]->pBuffers->pData = NULL;
                    }
                    qzFree(g_process.qz_inst[i].intermediate_buffers[j]->pBuffers);
                    g_process.qz_inst[i].intermediate_buffers[j]->pBuffers = NULL;
                }
                qzFree(g_process.qz_inst[i].intermediate_buffers[j]);
                g_process.qz_inst[i].intermediate_buffers[j] = NULL;
            }
        }
        free(g_process.qz_inst[i].intermediate_buffers);
        g_process.qz_inst[i].intermediate_buffers = NULL;
    }

    /*src buffers*/
    if (NULL != g_process.qz_inst[i].src_buffers) {
        for (j = 0; j < g_process.qz_inst[i].src_count; j++) {
            if (NULL != g_process.qz_inst[i].src_buffers[j]) {
                if (NULL != g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData) {
                    qzFree(g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData);
                    g_process.qz_inst[i].src_buffers[j]->pPrivateMetaData = NULL;
                }
                if (NULL != g_process.qz_inst[i].src_buffers[j]->pBuffers) {
                    if (NULL != g_process.qz_inst[i].src_buffers[j]->pBuffers->pData) {
                        qzFree(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData);
                        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = NULL;
                    }
                    qzFree(g_process.qz_inst[i].src_buffers[j]->pBuffers);
                    g_process.qz_inst[i].src_buffers[j]->pBuffers = NULL;
                }
                qzFree(g_process.qz_inst[i].src_buffers[j]);
                g_process.qz_inst[i].src_buffers[j] = NULL;
            }
        }
        free(g_process.qz_inst[i].src_buffers);
        g_process.qz_inst[i].src_buffers = NULL;
    }

    /*dest buffers*/
    if (NULL != g_process.qz_inst[i].dest_buffers) {
        for (j = 0; j < g_process.qz_inst[i].dest_count; j++) {
            if (NULL != g_process.qz_inst[i].dest_buffers[j]) {
                if (NULL != g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData) {
                    qzFree(g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData);
                    g_process.qz_inst[i].dest_buffers[j]->pPrivateMetaData = NULL;
                }
                if (NULL != g_process.qz_inst[i].dest_buffers[j]->pBuffers) {
                    if (NULL != g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData) {
                        qzFree(g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData);
                        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = NULL;
                    }
                    qzFree(g_process.qz_inst[i].dest_buffers[j]->pBuffers);
                    g_process.qz_inst[i].dest_buffers[j]->pBuffers = NULL;
                }
                qzFree(g_process.qz_inst[i].dest_buffers[j]);
                g_process.qz_inst[i].dest_buffers[j] = NULL;
            }
        }
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

#define QZ_INST_MEM_STATUS_CHECK(status, i)                          \
    if (CPA_STATUS_SUCCESS != status) {                              \
        cleanUpInstMem((i));                                         \
        rc = sw_backup ? QZ_NO_INST_ATTACH : QZ_NOSW_NO_INST_ATTACH; \
        goto done_inst;                                              \
    }

/* Allocate the DMAable memory buffers used by QAT
 * internally, those buffers are source buffer,
 * intermeidate buffer and destination buffer
 */
static int getInstMem(int i, QzSessionParamsInternal_T *params)
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
    QZ_INST_MEM_STATUS_CHECK(status, i);

    status = cpaDcGetNumIntermediateBuffers(g_process.dc_inst_handle[i],
                                            &(g_process.qz_inst[i].intermediate_cnt));
    QZ_INST_MEM_STATUS_CHECK(status, i);

    g_process.qz_inst[i].intermediate_buffers =
        calloc(1, (size_t)(g_process.qz_inst[i].intermediate_cnt * sizeof(
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
    if (params->hw_buff_sz <= 8 * 1024) {
        g_process.qz_inst[i].src_count = NUM_BUFF_8K;
        g_process.qz_inst[i].dest_count = NUM_BUFF_8K;
    }

    g_process.qz_inst[i].src_buffers = calloc(1, (size_t)(
                                           g_process.qz_inst[i].src_count *
                                           sizeof(CpaBufferList *)));
    QZ_INST_MEM_CHECK(g_process.qz_inst[i].src_buffers, i);

    g_process.qz_inst[i].dest_buffers = calloc(1, g_process.qz_inst[i].dest_count *
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
        /* The orig_src points internal pre-allocated pinned buffer. */
        g_process.qz_inst[i].stream[j].orig_src =
            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData;

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
        /* The orig_dest points internal pre-allocated pinned buffer. */
        g_process.qz_inst[i].stream[j].orig_dest =
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData;

        g_process.qz_inst[i].dest_buffers[j]->numBuffers = (Cpa32U)1;
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes = dest_sz;
    }

    status = cpaDcSetAddressTranslation(g_process.dc_inst_handle[i],
                                        qaeVirtToPhysNUMA);
    QZ_INST_MEM_STATUS_CHECK(status, i);

    g_process.qz_inst[i].inst_start_status =
        cpaDcStartInstance(g_process.dc_inst_handle[i],
                           g_process.qz_inst[i].intermediate_cnt,
                           g_process.qz_inst[i].intermediate_buffers);
    QZ_INST_MEM_STATUS_CHECK(g_process.qz_inst[i].inst_start_status, i);

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
    int rc = QZ_OK;
    QzSess_T *qz_sess;
    QzSessionParams_T temp = {0};

    if (unlikely(NULL == sess)) {
        return QZ_PARAMS;
    }

    if (NULL == params) {
        rc = qzGetDefaults(&temp);
        params = &temp;
    }

    if (qzCheckParams(params) != QZ_OK) {
        return QZ_PARAMS;
    }

    sess->hw_session_stat = QZ_FAIL;
    if (sess->internal == NULL) {
        sess->internal = calloc(1, sizeof(QzSess_T));
        if (unlikely(NULL == sess->internal)) {
            sess->hw_session_stat = QZ_NOSW_LOW_MEM;
            return QZ_NOSW_LOW_MEM;
        }
    } else {
        return QZ_DUPLICATE;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qzSetParams(params, &qz_sess->sess_params);

    rc = qzSetupSessionInternal(sess);
    if (rc < 0) {
        free(sess->internal);
        sess->internal = NULL;
        return rc;
    }

    return rc;
}

int qzSetupSessionDeflate(QzSession_T *sess,  QzSessionParamsDeflate_T *params)
{
    int rc = QZ_OK;
    QzSess_T *qz_sess;
    QzSessionParamsDeflate_T temp = {{0}};

    if (unlikely(NULL == sess)) {
        return QZ_PARAMS;
    }

    if (NULL == params) {
        rc = qzGetDefaultsDeflate(&temp);
        params = &temp;
    }

    if (qzCheckParamsDeflate(params) != QZ_OK) {
        return QZ_PARAMS;
    }

    sess->hw_session_stat = QZ_FAIL;
    if (sess->internal == NULL) {
        sess->internal = calloc(1, sizeof(QzSess_T));
        if (unlikely(NULL == sess->internal)) {
            sess->hw_session_stat = QZ_NOSW_LOW_MEM;
            return QZ_NOSW_LOW_MEM;
        }
    } else {
        return QZ_DUPLICATE;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qzSetParamsDeflate(params, &qz_sess->sess_params);

    rc = qzSetupSessionInternal(sess);
    if (rc < 0) {
        free(sess->internal);
        sess->internal = NULL;
        return rc;
    }

    return rc;

}

int qzSetupSessionLZ4(QzSession_T *sess,  QzSessionParamsLZ4_T *params)
{
    int rc = QZ_OK;
    QzSess_T *qz_sess;
    QzSessionParamsLZ4_T temp = {{0}};

    if (unlikely(NULL == sess)) {
        return QZ_PARAMS;
    }

    if (NULL == params) {
        rc = qzGetDefaultsLZ4(&temp);
        params = &temp;
    }

    if (qzCheckParamsLZ4(params) != QZ_OK) {
        return QZ_PARAMS;
    }

    sess->hw_session_stat = QZ_FAIL;
    if (sess->internal == NULL) {
        sess->internal = calloc(1, sizeof(QzSess_T));
        if (unlikely(NULL == sess->internal)) {
            sess->hw_session_stat = QZ_NOSW_LOW_MEM;
            return QZ_NOSW_LOW_MEM;
        }
    } else {
        return QZ_DUPLICATE;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qzSetParamsLZ4(params, &qz_sess->sess_params);

    rc = qzSetupSessionInternal(sess);
    if (rc < 0) {
        free(sess->internal);
        sess->internal = NULL;
        return rc;
    }

    return rc;
}

int qzSetupSessionLZ4S(QzSession_T *sess,  QzSessionParamsLZ4S_T *params)
{
    int rc = QZ_OK;
    QzSess_T *qz_sess;
    QzSessionParamsLZ4S_T temp = {{0}};

    if (unlikely(NULL == sess)) {
        return QZ_PARAMS;
    }

    if (NULL == params) {
        rc = qzGetDefaultsLZ4S(&temp);
        params = &temp;
    }

    if (qzCheckParamsLZ4S(params) != QZ_OK) {
        return QZ_PARAMS;
    }

    sess->hw_session_stat = QZ_FAIL;
    if (sess->internal == NULL) {
        sess->internal = calloc(1, sizeof(QzSess_T));
        if (unlikely(NULL == sess->internal)) {
            sess->hw_session_stat = QZ_NOSW_LOW_MEM;
            return QZ_NOSW_LOW_MEM;
        }
    } else {
        return QZ_DUPLICATE;
    }

    qz_sess = (QzSess_T *)sess->internal;
    qzSetParamsLZ4S(params, &qz_sess->sess_params);

    rc = qzSetupSessionInternal(sess);
    if (rc < 0) {
        free(sess->internal);
        sess->internal = NULL;
        return rc;
    }

    return rc;
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
        g_process.qz_inst[i].session_setup_data = qz_sess->session_setup_data;
    }

    if (rc == QZ_OK) {
        g_process.qz_inst[i].cpa_sess_setup = 1;
    }

done_sess:
    return rc;
}

/*
 * Update cpa session of instance i, it will remove cpa session first,
 * and reallocate memory and re-initialize cpa session based on
 * session setup data.
 */
int qzUpdateCpaSession(QzSession_T *sess, int i)
{
    QzSess_T *qz_sess;
    CpaStatus ret = CPA_STATUS_SUCCESS;

    assert(sess);

    g_process.qz_inst[i].cpa_sess_setup = 0;
    qz_sess = (QzSess_T *)sess->internal;
    assert(qz_sess);

    /* Remove cpa session */
    ret = cpaDcRemoveSession(g_process.dc_inst_handle[i],
                             g_process.qz_inst[i].cpaSess);
    if (ret != CPA_STATUS_SUCCESS) {
        QZ_ERROR("qzUpdateCpaSession: remove session failed\n");
        return QZ_FAIL;
    }

    /* free memory of capSess */
    qzFree(g_process.qz_inst[i].cpaSess);

    /* As the session setup data has been updated, need to get the size of
     * session again. */
    ret = cpaDcGetSessionSize(g_process.dc_inst_handle[i],
                              &qz_sess->session_setup_data,
                              &qz_sess->session_size,
                              &qz_sess->ctx_size);
    if (ret != CPA_STATUS_SUCCESS) {
        QZ_ERROR("qzUpdateCpaSession: get session size failed\n");
        return QZ_FAIL;
    }

    g_process.qz_inst[i].cpaSess = qzMalloc((size_t)(qz_sess->session_size),
                                            NODE_0, PINNED_MEM);
    if (!g_process.qz_inst[i].cpaSess) {
        QZ_ERROR("qzUpdateCpaSession: allocate session failed\n");
        return QZ_FAIL;
    }

    ret = cpaDcInitSession(g_process.dc_inst_handle[i],
                           g_process.qz_inst[i].cpaSess,
                           &qz_sess->session_setup_data,
                           NULL,
                           dcCallback);
    if (ret != CPA_STATUS_SUCCESS) {
        QZ_ERROR("qzUpdateCpaSession: init session failed\n");
        free(g_process.qz_inst[i].cpaSess);
        g_process.qz_inst[i].cpaSess = NULL;
        return QZ_FAIL;
    }
    g_process.qz_inst[i].session_setup_data = qz_sess->session_setup_data;

    g_process.qz_inst[i].cpa_sess_setup = 1;
    return QZ_OK;
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
    unsigned int src_sz, dest_sz;
    CpaStatus rc;
    int src_mem_type, dest_mem_type;
    DataFormatInternal_T data_fmt;
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    CpaDcOpData *opData = NULL;
    struct timespec my_time;

    my_time.tv_sec = 0;
    my_time.tv_nsec = GET_BUFFER_SLEEP_NSEC;
    QZ_DEBUG("Always enable CnV\n");

    i = qz_sess->inst_hint;
    j = -1;
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;
    src_ptr = qz_sess->src + qz_sess->qz_in_len;
    dest_ptr = qz_sess->next_dest;
    src_mem_type = qzMemFindAddr(src_ptr);
    dest_mem_type = qzMemFindAddr(dest_ptr);
    remaining = *qz_sess->src_sz - qz_sess->qz_in_len;
    src_sz = qz_sess->sess_params.hw_buff_sz;
    dest_sz = *qz_sess->dest_sz;
    data_fmt = qz_sess->sess_params.data_fmt;
    QZ_DEBUG("doCompressIn: Need to g_process %u bytes\n", remaining);

    while (!done) {
        do {
            j = getUnusedBuffer(i, j);
            if (unlikely(-1 == j)) {
                nanosleep(&my_time, NULL);
            }
        } while (-1 == j);
        QZ_DEBUG("getUnusedBuffer returned %d\n", j);

        g_process.qz_inst[i].stream[j].src1++; /*this buffer is in use*/
        src_send_sz = (remaining < src_sz) ? remaining : src_sz;

        //setup opData
        opData = &g_process.qz_inst[i].stream[j].opData;
        opData->inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
        opData->outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
        opData->compressAndVerify = CPA_TRUE;
        opData->flushFlag = IS_DEFLATE(data_fmt) ? CPA_DC_FLUSH_FULL :
                            CPA_DC_FLUSH_FINAL;
        if (unlikely(IS_DEFLATE(data_fmt) &&
                     1 == qz_sess->last &&
                     remaining <= src_sz)) {
            opData->flushFlag = CPA_DC_FLUSH_FINAL;
        }

        g_process.qz_inst[i].stream[j].seq = qz_sess->seq; /*this buffer is in use*/
        QZ_DEBUG("sending seq number %d %d %ld, opData.flushFlag %d\n", i, j,
                 qz_sess->seq, opData->flushFlag);
        qz_sess->seq++;
        qz_sess->submitted++;
        /*send to compression engine here*/
        g_process.qz_inst[i].stream[j].src2++; /*this buffer is in use*/
        /*set up src dest buffers*/
        g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
        if (dest_sz > DEST_SZ(qz_sess->sess_params.hw_buff_sz)) {
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
                DEST_SZ((unsigned long)(qz_sess->sess_params.hw_buff_sz)) - outputHeaderSz(
                    data_fmt);
        } else {
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
                dest_sz - outputHeaderSz(data_fmt);
        }

        if (!need_cont_mem) {
            QZ_DEBUG("Compress SVM Enabled in doCompressIn\n");
        }
        if ((COMMON_MEM == src_mem_type) && need_cont_mem) {
            QZ_DEBUG("memory copy in doCompressIn\n");
            QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                      src_ptr,
                      src_send_sz,
                      remaining);
            g_process.qz_inst[i].stream[j].src_need_reset = 0;
        } else {
            QZ_DEBUG("changing src_ptr to 0x%lx\n", (unsigned long)src_ptr);
            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
            g_process.qz_inst[i].stream[j].src_need_reset = 1;
        }

        /*using zerocopy for the first request while dest buffer is pinned*/
        if (unlikely((!need_cont_mem || dest_mem_type) &&
                     (0 == g_process.qz_inst[i].stream[j].seq))) {
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                dest_ptr + outputHeaderSz(data_fmt);
            g_process.qz_inst[i].stream[j].dest_need_reset = 1;
        }

        g_process.qz_inst[i].stream[j].res.checksum = 0;
        do {
            tag = ((unsigned long)i << 16) | (unsigned long)j;
            QZ_DEBUG("Comp Sending %u bytes ,opData.flushFlag = %d, i = %d j = %d seq = %ld tag = %ld\n",
                     g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes,
                     opData->flushFlag,
                     i, j, g_process.qz_inst[i].stream[j].seq, tag);
            rc = cpaDcCompressData2(g_process.dc_inst_handle[i],
                                    g_process.qz_inst[i].cpaSess,
                                    g_process.qz_inst[i].src_buffers[j],
                                    g_process.qz_inst[i].dest_buffers[j],
                                    opData,
                                    &g_process.qz_inst[i].stream[j].res,
                                    (void *)(tag));
            if (unlikely(CPA_STATUS_RETRY == rc)) {
                g_process.qz_inst[i].num_retries++;
                usleep(g_polling_interval[qz_sess->polling_idx]);
            }

            if (unlikely(g_process.qz_inst[i].num_retries > MAX_NUM_RETRY)) {
                QZ_ERROR("instance %d retry count:%d exceed the max count: %d\n",
                         i, g_process.qz_inst[i].num_retries, MAX_NUM_RETRY);
                goto err_exit;
            }
        } while (rc == CPA_STATUS_RETRY);

        if (unlikely(CPA_STATUS_SUCCESS != rc)) {
            QZ_ERROR("Error in cpaDcCompressData: %d\n", rc);
            goto err_exit;
        }

        QZ_DEBUG("remaining = %u, src_send_sz = %u, seq = %ld\n", remaining,
                 src_send_sz,  qz_sess->seq);
        g_process.qz_inst[i].num_retries = 0;
        src_ptr += src_send_sz;
        remaining -= src_send_sz;

        if (unlikely(qz_sess->stop_submitting)) {
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
    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
    }
    if (g_process.qz_inst[i].stream[j].src_need_reset) {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_src;
        g_process.qz_inst[i].stream[j].src_need_reset = 0;
    }
    return ((void *)NULL);
}

/*
 * Store the uncompressed data to qz_sess->next_dest.
 */
static int qzLZ4StoredBlocks(QzSess_T *qz_sess, const unsigned char *src,
                             unsigned int src_len, long int *dest_len)
{
    unsigned int block_size = 0;
    unsigned int block_count = 0;
    unsigned int out_len = 0;
    XXH32_state_t *xxhash_state;
    int src_location;
    int this_block_len;
    CpaDcRqResults resl;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;

    //set block size to STORED_BLK_MAX_LEN(64K)
    block_size = STORED_BLK_MAX_LEN;
    block_count = src_len / block_size +
               (((src_len % block_size) > 0) ? 1 : 0);
    out_len = (outputHeaderSz(data_fmt) +
               (block_count * QZ_LZ4_STORED_HEADER_SIZE) +
               src_len + outputFooterSz(data_fmt));
    *dest_len -= out_len;
    if (*dest_len < 0) {
        QZ_ERROR("do_compress_out: inadequate output buffer length for stored block: %ld\n",
                 *dest_len);
        return QZ_BUF_ERROR;
    }
    resl.produced = (block_count * QZ_LZ4_STORED_HEADER_SIZE) + src_len;
    resl.consumed = src_len;
    qz_sess->qz_in_len += resl.consumed;
    qz_sess->qz_out_len += out_len;

    outputHeaderGen(qz_sess->next_dest, &resl, data_fmt);
    qz_sess->next_dest += outputHeaderSz(data_fmt);

    xxhash_state = XXH32_createState();
    if (!xxhash_state) {
        QZ_ERROR("qzLZ4StoredBlocks: XXH32_createState create state fail\n");
        return QZ_FAIL;
    }
    (void)XXH32_reset(xxhash_state, 0);

    resl.checksum = 0;
    src_location = 0;
    while (src_len > 0) {
        this_block_len = src_len > block_size ?
                         block_size : src_len;

        //create stored block header
        *(unsigned int *)(qz_sess->next_dest) =
            this_block_len | QZ_LZ4_STOREDBLOCK_FLAG;
        qz_sess->next_dest += 4;

        // copy the source data
        QZ_MEMCPY(qz_sess->next_dest, &src[src_location],
                  this_block_len, src_len);

        // update the xxhash
        (void)XXH32_update(xxhash_state,  &src[src_location],
                           this_block_len);

        // jump to next source data location
        qz_sess->next_dest += this_block_len;
        src_location += this_block_len;
        src_len -= this_block_len;
        QZ_DEBUG("src_len :%u this_block_len: %d this block checksum: %x\n",
                 src_len, this_block_len, resl.checksum);
    }
    resl.checksum = XXH32_digest(xxhash_state);
    QZ_DEBUG("\tchecksum = 0x%x\n", resl.checksum);
    QZ_DEBUG("\tlen = 0x%x\n", resl.produced);

    //Append footer
    outputFooterGen(qz_sess, &resl, data_fmt);
    qz_sess->next_dest += outputFooterSz(data_fmt);
    XXH32_freeState(xxhash_state);
    return QZ_OK;
}

/*
 * Store the uncompressed data to qz_sess->next_dest.
 */
static int qzDeflateStoredBlocks(QzSess_T *qz_sess, const unsigned char *src,
                                 unsigned int src_len, long int *dest_len)
{
    long block_count;
    long out_len;
    int src_location;
    CpaDcRqResults resl;
    unsigned int block_size = STORED_BLK_MAX_LEN;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;

    block_count = src_len / block_size +
               (((src_len % block_size) > 0) ? 1 : 0);
    out_len = (outputHeaderSz(data_fmt) + (block_count * STORED_BLK_HDR_SZ) +
               src_len + outputFooterSz(data_fmt));
    *dest_len -= out_len;
    if (*dest_len < 0) {
        QZ_ERROR("do_compress_out: inadequate output buffer length for stored block: %ld\n",
                 *dest_len);
        return QZ_BUF_ERROR;
    }

    resl.produced = (block_count * STORED_BLK_HDR_SZ) + src_len;
    resl.consumed = src_len;
    qz_sess->qz_in_len += resl.consumed;
    qz_sess->qz_out_len += out_len;

    outputHeaderGen(qz_sess->next_dest, &resl, data_fmt);
    qz_sess->next_dest += outputHeaderSz(data_fmt);

    resl.checksum = 0;
    src_location = 0;
    while (src_len > 0) {
        int this_block_len;
        this_block_len = src_len;
        if (this_block_len > block_size) {
            this_block_len = block_size;
        }
        // create store block header here
        if (src_len == this_block_len) {
            // set bfinal bit + block type
            *(unsigned char *)(qz_sess->next_dest) = 0x01;
            QZ_DEBUG("Creating the final block\n");
        } else {
            // clear bfinal bit + block type
            *(unsigned char *)(qz_sess->next_dest) = 0x00;
            QZ_DEBUG("Creating the block without final bit\n");
        }
        //
        // Add length and XOR of length
        // to stored block header
        //
        qz_sess->next_dest++;
        *(unsigned short *)(qz_sess->next_dest) = this_block_len;
        qz_sess->next_dest += 2;
        *(unsigned short *)(qz_sess->next_dest) = ~this_block_len;
        qz_sess->next_dest += 2;
        // copy the source data
        QZ_MEMCPY(qz_sess->next_dest,
                  &src[src_location],
                  this_block_len,
                  src_len);
        // update the crc
        resl.checksum = crc32(resl.checksum,
                              &src[src_location],
                              this_block_len);
        // jump to next source data location
        qz_sess->next_dest += this_block_len;
        src_location += this_block_len;
        src_len -= this_block_len;
        QZ_DEBUG("src_len :%u this_block_len: %d this block checksum: %x\n",
                 src_len, this_block_len, resl.checksum);
    }
    QZ_DEBUG("\tgzip checksum = 0x%x\n", resl.checksum);
    QZ_DEBUG("\tlen = 0x%x\n", resl.produced);
    //Append footer
    outputFooterGen(qz_sess, &resl, data_fmt);
    qz_sess->next_dest += outputFooterSz(data_fmt);

    return QZ_OK;
}

static int qzStoredBlocks(QzSess_T *qz_sess, const unsigned char *src,
                          unsigned int src_len, long int *dest_len)
{
    int ret = 0;
    assert(qz_sess != NULL);
    assert(src != NULL);
    assert(src_len > 0);
    assert(*dest_len > 0);

    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    switch (data_fmt) {
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_RAW:
    case DEFLATE_4B:
        ret = qzDeflateStoredBlocks(qz_sess, src, src_len, dest_len);
        break;
    case LZ4_FH:
        ret = qzLZ4StoredBlocks(qz_sess, src, src_len, dest_len);
        break;
    default:
        break;
    }
    return ret;
}

/* The internal function to g_process the comrpession response
 * from the QAT hardware
 */

static void *doCompressOut(void *in)
{
    int i = 0, j = 0;
    int ret = 0;
    int good = -1;
    CpaDcRqResults *resl;
    CpaStatus sts;
    unsigned int sleep_cnt = 0;
    QzSession_T *sess = (QzSession_T *) in;
    QzSess_T *qz_sess = (QzSess_T *) sess->internal;
    long dest_avail_len = (long)(*qz_sess->dest_sz - qz_sess->qz_out_len);
    i = qz_sess->inst_hint;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    QzPollingMode_T polling_mode = qz_sess->sess_params.polling_mode;

    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;

    while ((qz_sess->last_submitted == 0) ||
           (qz_sess->processed < qz_sess->submitted)) {

        /*Poll for responses*/
        good = 0;
        sts = icp_sal_DcPollInstance(g_process.dc_inst_handle[i], 0);
        if (unlikely(CPA_STATUS_FAIL == sts)) {
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
                         "%2.2d %4.4ld, PID: %d, TID: %lu\n",
                         i, j, g_process.qz_inst[i].stream[j].seq,
                         getpid(), pthread_self());

                assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
                qz_sess->seq_in++;

                resl = &g_process.qz_inst[i].stream[j].res;
                if (unlikely(CPA_STATUS_SUCCESS != g_process.qz_inst[i].stream[j].job_status &&
                             CPA_DC_VERIFY_ERROR != resl->status)) {
                    QZ_ERROR("Error(%d) in callback: %d, %d, ReqStatus: %d\n",
                             g_process.qz_inst[i].stream[j].job_status, i, j,
                             g_process.qz_inst[i].stream[j].res.status);
                    RESTORE_CPASTREAM_BUFFER(i, j);
                    sess->thd_sess_stat = QZ_FAIL;
                    continue;
                }

                if (unlikely(QZ_FAIL == sess->thd_sess_stat ||
                             QZ_BUF_ERROR == sess->thd_sess_stat)) {
                    // There is an error in previous process,
                    // we just restore buffer here.
                    RESTORE_CPASTREAM_BUFFER(i, j);
                    continue;
                }

                QZ_DEBUG("\tconsumed = %d, produced = %d, seq_in = %ld\n",
                         resl->consumed, resl->produced, g_process.qz_inst[i].stream[j].seq);

                if (unlikely(CPA_DC_VERIFY_ERROR == resl->status)) {
                    // for lz4s data format, it does not support stored block,
                    // we need polling out all response in the ring before
                    // exiting this funciton.
                    if (LZ4S_BK == data_fmt) {
                        QZ_ERROR("doCompressOut: lz4s does not support stored block\n");
                        RESTORE_CPASTREAM_BUFFER(i, j);
                        sess->thd_sess_stat = QZ_FAIL;
                        continue;
                    }

                    ret = qzStoredBlocks(qz_sess,
                                         g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                                         g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes,
                                         &dest_avail_len);
                    if (ret != QZ_OK) {
                        QZ_ERROR("do_compress_out: inadequate output buffer length for stored block: %ld\n",
                                 (long)(*qz_sess->dest_sz));
                        RESTORE_CPASTREAM_BUFFER(i, j);
                        sess->thd_sess_stat = ret;
                        continue;
                    }
                    if (g_process.qz_inst[i].stream[j].src_need_reset) {
                        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_src;
                        g_process.qz_inst[i].stream[j].src_need_reset = 0;
                    }

                    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
                        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_dest;
                        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
                    }
                } else {
                    dest_avail_len -= (outputHeaderSz(data_fmt) + resl->produced + outputFooterSz(
                                           data_fmt));
                    if (unlikely(dest_avail_len < 0)) {
                        QZ_DEBUG("doCompressOut: inadequate output buffer length: %ld, outlen: %ld\n",
                                 (long)(*qz_sess->dest_sz), qz_sess->qz_out_len);
                        RESTORE_CPASTREAM_BUFFER(i, j);
                        sess->thd_sess_stat = QZ_BUF_ERROR;
                        continue;
                    }

                    outputHeaderGen(qz_sess->next_dest, resl, data_fmt);
                    qz_sess->next_dest += outputHeaderSz(data_fmt);
                    qz_sess->qz_out_len += outputHeaderSz(data_fmt);

                    if (!need_cont_mem) {
                        QZ_DEBUG("Compress SVM Enabled in doCompressOut\n");
                    }
                    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
                        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_dest;
                        g_process.qz_inst[i].stream[j].dest_need_reset = 0;
                    } else {
                        QZ_MEMCPY(qz_sess->next_dest,
                                  g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                                  *qz_sess->dest_sz - qz_sess->qz_out_len,
                                  resl->produced);
                    }
                    qz_sess->next_dest += resl->produced;
                    qz_sess->qz_in_len += resl->consumed;

                    if (likely(NULL != qz_sess->crc32 &&
                               (data_fmt == DEFLATE_GZIP_EXT ||
                                data_fmt == DEFLATE_GZIP ||
                                data_fmt == DEFLATE_4B  ||
                                data_fmt == DEFLATE_RAW))) {
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

                    if (g_process.qz_inst[i].stream[j].src_need_reset) {
                        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                            g_process.qz_inst[i].stream[j].orig_src;
                        g_process.qz_inst[i].stream[j].src_need_reset = 0;
                    }
                }

                g_process.qz_inst[i].stream[j].sink2++;
                qz_sess->processed++;
                break;
            }
        }

        if (QZ_PERIODICAL_POLLING == polling_mode) {
            if (0 == good) {
                qz_sess->polling_idx = (qz_sess->polling_idx >= POLLING_LIST_NUM - 1) ?
                                       (POLLING_LIST_NUM - 1) :
                                       (qz_sess->polling_idx + 1);

                QZ_DEBUG("comp sleep for %d usec...\n",
                         g_polling_interval[qz_sess->polling_idx]);
                usleep(g_polling_interval[qz_sess->polling_idx]);
                sleep_cnt++;
            } else {
                qz_sess->polling_idx = (qz_sess->polling_idx == 0) ? (0) :
                                       (qz_sess->polling_idx - 1);
            }
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

        if (g_process.qz_inst[i].stream[j].dest_need_reset) {
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                g_process.qz_inst[i].stream[j].orig_dest;
            g_process.qz_inst[i].stream[j].dest_need_reset = 0;
        }

        if (g_process.qz_inst[i].stream[j].src_need_reset) {
            g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                g_process.qz_inst[i].stream[j].orig_src;
            g_process.qz_inst[i].stream[j].src_need_reset = 0;
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
    case QZ_LOW_DEST_MEM:
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
        return g_sess_params_internal_default.sw_backup;
    }
}

/* The QATzip compression API */
int qzCompress(QzSession_T *sess, const unsigned char *src,
               unsigned int *src_len, unsigned char *dest,
               unsigned int *dest_len, unsigned int last)
{
    return qzCompressExt(sess, src, src_len, dest, dest_len, last, NULL);
}

int qzCompressExt(QzSession_T *sess, const unsigned char *src,
                  unsigned int *src_len, unsigned char *dest,
                  unsigned int *dest_len, unsigned int last, uint64_t *ext_rc)
{
    if (NULL == sess || (last != 0 && last != 1)) {
        if (NULL != src_len) {
            *src_len = 0;
        }
        if (NULL != dest_len) {
            *dest_len = 0;
        }
        return QZ_PARAMS;
    }

    return qzCompressCrcExt(sess, src, src_len, dest, dest_len, last, NULL, ext_rc);
}

int qzCompressCrc(QzSession_T *sess, const unsigned char *src,
                  unsigned int *src_len, unsigned char *dest,
                  unsigned int *dest_len, unsigned int last,
                  unsigned long *crc)
{
    return qzCompressCrcExt(sess, src, src_len, dest, dest_len, last, crc, NULL);
}

int qzCompressCrcExt(QzSession_T *sess, const unsigned char *src,
                     unsigned int *src_len, unsigned char *dest,
                     unsigned int *dest_len, unsigned int last,
                     unsigned long *crc, uint64_t *ext_rc)
{
    int i, reqcnt;
    unsigned int out_len;
    QzSess_T *qz_sess;
    int rc;

    if (unlikely(NULL == sess     || \
                 NULL == src      || \
                 NULL == src_len  || \
                 NULL == dest     || \
                 NULL == dest_len || \
                 (last != 0 && last != 1))) {
        if (NULL != src_len) {
            *src_len = 0;
        }
        if (NULL != dest_len) {
            *dest_len = 0;
        }
        return QZ_PARAMS;
    }


    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        *src_len = 0;
        *dest_len = 0;
        return rc;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        if (g_sess_params_internal_default.data_fmt == LZ4_FH) {
            rc = qzSetupSessionLZ4(sess, NULL);
        } else if (g_sess_params_internal_default.data_fmt == LZ4S_BK) {
            rc = qzSetupSessionLZ4S(sess, NULL);
        } else {
            rc = qzSetupSessionDeflate(sess, NULL);
        }
        if (unlikely(QZ_SETUP_SESSION_FAIL(rc))) {
            *src_len = 0;
            *dest_len = 0;
            return rc;
        }
    }

    qz_sess = (QzSess_T *)(sess->internal);

    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    if (unlikely(data_fmt != DEFLATE_4B &&
                 data_fmt != DEFLATE_RAW &&
                 data_fmt != DEFLATE_GZIP &&
                 data_fmt != DEFLATE_GZIP_EXT &&
                 data_fmt != LZ4_FH &&
                 data_fmt != LZ4S_BK)) {
        QZ_ERROR("Unknown data format: %d\n", data_fmt);
        *src_len = 0;
        *dest_len = 0;
        return QZ_PARAMS;
    }
    QZ_DEBUG("qzCompressCrc data_fmt: %d, input crc32 is 0x%lX\n",
             data_fmt, crc ? *crc : 0);

    qz_sess->crc32 = crc;

    if (*src_len < qz_sess->sess_params.input_sz_thrshold
         || g_process.qz_init_status == QZ_NO_HW
         || sess->hw_session_stat == QZ_NO_HW
#if !((CPA_DC_API_VERSION_NUM_MAJOR >= 3) && (CPA_DC_API_VERSION_NUM_MINOR >= 0))
         || qz_sess->sess_params.comp_lvl == 9
#endif
       ) {
        QZ_DEBUG("compression src_len=%u, sess_params.input_sz_thrshold = %u, "
                 "process.qz_init_status = %d, sess->hw_session_stat = %ld, "
                 " switch to software.\n",
                 *src_len, qz_sess->sess_params.input_sz_thrshold,
                 g_process.qz_init_status, sess->hw_session_stat);
        goto sw_compression;
    } else if (sess->hw_session_stat != QZ_OK &&
               sess->hw_session_stat != QZ_NO_INST_ATTACH) {
        *src_len = 0;
        *dest_len = 0;
        return sess->hw_session_stat;
    }

    i = qzGrabInstance(qz_sess->inst_hint, data_fmt);
    if (unlikely(i == -1)) {
        if (qz_sess->sess_params.sw_backup == 1) {
            goto sw_compression;
        } else {
            sess->hw_session_stat = QZ_NO_INST_ATTACH;
            *src_len = 0;
            *dest_len = 0;
            return QZ_NOSW_NO_INST_ATTACH;
        }
        /*Make this a s/w compression*/
    }

    QZ_DEBUG("qzCompress: inst is %d\n", i);
    qz_sess->inst_hint = i;

    if (likely(0 ==  g_process.qz_inst[i].mem_setup ||
               0 ==  g_process.qz_inst[i].cpa_sess_setup)) {
        QZ_DEBUG("Getting HW resources  for inst %d\n", i);
        rc = qzSetupHW(sess, i);
        if (unlikely(QZ_OK != rc)) {
            qzReleaseInstance(i);
            if (QZ_LOW_MEM == rc || QZ_NO_INST_ATTACH == rc) {
                goto sw_compression;
            } else {
                *src_len = 0;
                *dest_len = 0;
                return rc;
            }
        }
    } else if (memcmp(&g_process.qz_inst[i].session_setup_data,
                      &qz_sess->session_setup_data, sizeof(CpaDcSessionSetupData))) {
        /* session_setup_data of qz_sess is not same with instance i,
           need to update cpa session of instance i. */
        rc = qzUpdateCpaSession(sess, i);
        if (QZ_OK != rc) {
            qzReleaseInstance(i);
            *src_len = 0;
            *dest_len = 0;
            return rc;
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

    //trigger post-processing
    if (data_fmt == LZ4S_BK && qz_sess->sess_params.qzCallback) {
        if (sess->thd_sess_stat == QZ_OK ||
            (sess->thd_sess_stat == QZ_BUF_ERROR && 0 != *src_len)) {
            int error_code = 0;
            int callback_status = qz_sess->sess_params.qzCallback(
                                      qz_sess->sess_params.qzCallback_external,
                                      src, src_len, dest, dest_len,
                                      &error_code);
            if (callback_status == QZ_OK) {
                sess->total_out = *dest_len;
                if (!ext_rc) {
                    QZ_ERROR("Invaild ext_rc pointer!\n");
                } else {
                    *ext_rc = 0;
                }
            } else {
                QZ_DEBUG("Error when call lz4s post-processing callback\n");
                sess->thd_sess_stat = callback_status;
                *src_len = 0;
                *dest_len = 0;
                if (!ext_rc) {
                    QZ_ERROR("Invaild ext_rc pointer!\n");
                } else {
                    *ext_rc = (uint64_t)error_code;
                }
            }
        } else {
            QZ_DEBUG("Error lz4s compresse failed\n");
            *src_len = 0;
            *dest_len = 0;
        }
    }

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

    p_tmp_data = g_process.qz_inst[i].stream[j].orig_src;
    g_process.qz_inst[i].stream[j].orig_src =
        g_process.qz_inst[i].stream[j].orig_dest;
    g_process.qz_inst[i].stream[j].orig_dest = p_tmp_data;
}

static int checkHeader(QzSess_T *qz_sess, unsigned char *src,
                       unsigned int src_avail_len, unsigned int dest_avail_len,
                       QzGzH_T *hdr)
{
    unsigned char *src_ptr = src;
    unsigned int compressed_sz = 0;
    unsigned int uncompressed_sz = 0;
    StdGzF_T *qzFooter = NULL;
    int isEndWithFooter = 0;
    int ret = 0;
    QzLZ4H_T *qzLZ4Header = NULL;
    QzLZ4F_T *lz4Footer = NULL;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;

    if ((src_avail_len <= 0) || (dest_avail_len <= 0)) {
        QZ_DEBUG("checkHeader: insufficient %s buffer length\n",
                 (dest_avail_len <= 0) ? "destation" : "source");
        return QZ_BUF_ERROR;
    }

    if (src_avail_len < outputHeaderSz(data_fmt)) {
        QZ_DEBUG("checkHeader: incomplete source buffer\n");
        return QZ_DATA_ERROR;
    }

    if (1 == qz_sess->force_sw) {
        return QZ_FORCE_SW;
    }


    switch (data_fmt) {
    case DEFLATE_GZIP:
        qzFooter = (StdGzF_T *)(findStdGzipFooter(src_ptr, src_avail_len));

        compressed_sz = (unsigned char *)qzFooter - src_ptr - stdGzipHeaderSz();
        uncompressed_sz = qzFooter->i_size;
        if ((unsigned char *)qzFooter == src_ptr + src_avail_len - stdGzipFooterSz()) {
            isEndWithFooter = 1;
        }
        break;
    case DEFLATE_GZIP_EXT:
        if (QZ_OK != qzGzipHeaderExt(src_ptr, hdr)) {
            return QZ_FAIL;
        }
        compressed_sz = (long)(hdr->extra.qz_e.dest_sz);
        uncompressed_sz = (long)(hdr->extra.qz_e.src_sz);
        break;
    case DEFLATE_4B:
        compressed_sz = *(int *)src_ptr;
        uncompressed_sz = (qz_sess->sess_params.hw_buff_sz > dest_avail_len) ?
                          dest_avail_len : qz_sess->sess_params.hw_buff_sz;
        break;
    case LZ4_FH:
        ret = qzVerifyLZ4FrameHeader(src_ptr, src_avail_len);
        if (ret != QZ_OK) {
            return ret;
        }

        lz4Footer = (QzLZ4F_T *)findLZ4Footer(src_ptr, src_avail_len);
        if (NULL == lz4Footer) {
            QZ_DEBUG("checkHeader: incomplete source buffer\n");
            return QZ_DATA_ERROR;
        }
        qzLZ4Header = (QzLZ4H_T *)src_ptr;
        compressed_sz = (unsigned char *)lz4Footer - src_ptr - qzLZ4HeaderSz();
        uncompressed_sz = qzLZ4Header->cnt_size;
        break;
    default:
        return QZ_FAIL;
    }

    if ((compressed_sz > DEST_SZ((long)(qz_sess->sess_params.hw_buff_sz))) ||
        (uncompressed_sz > qz_sess->sess_params.hw_buff_sz)) {
        if (1 == qz_sess->sess_params.sw_backup) {
            if (DEFLATE_GZIP == data_fmt &&
                1 == isEndWithFooter) {
                return QZ_LOW_DEST_MEM;
            }
            qz_sess->force_sw = 1;
            return QZ_LOW_MEM;
        } else {
            return QZ_NOSW_LOW_MEM;
        }
    }

    if (compressed_sz + outputHeaderSz(data_fmt) + outputFooterSz(data_fmt) >
        src_avail_len) {
        QZ_DEBUG("checkHeader: incomplete source buffer\n");
        return QZ_DATA_ERROR;
    } else if (uncompressed_sz > dest_avail_len) {
        QZ_DEBUG("checkHeader: insufficient destination buffer length\n");
        return QZ_BUF_ERROR;
    }
    hdr->extra.qz_e.dest_sz = compressed_sz;
    hdr->extra.qz_e.src_sz = uncompressed_sz;
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
    unsigned int src_send_sz = 0;
    unsigned int dest_receive_sz;
    unsigned int src_avail_len, dest_avail_len;
    unsigned int tmp_src_avail_len, tmp_dest_avail_len;
    unsigned char *src_ptr;
    unsigned char *dest_ptr;
    int src_mem_type = 0;
    int dest_mem_type = 0;
    QzGzH_T hdr = {{0}, 0};
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    StdGzF_T *qzFooter = NULL;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    struct timespec my_time;

    my_time.tv_sec = 0;
    my_time.tv_nsec = GET_BUFFER_SLEEP_NSEC;
    i = qz_sess->inst_hint;
    j = -1;
    src_ptr = qz_sess->src + qz_sess->qz_in_len;
    dest_ptr = qz_sess->next_dest;
    src_mem_type = qzMemFindAddr(src_ptr);
    dest_mem_type = qzMemFindAddr(dest_ptr);
    remaining = *qz_sess->src_sz - qz_sess->qz_in_len;
    src_avail_len = remaining;
    dest_avail_len = (long)(*qz_sess->dest_sz - qz_sess->qz_out_len);
    QZ_DEBUG("doDecompressIn: Need to g_process %d bytes\n", remaining);
    CpaBoolean need_cont_mem =
        g_process.qz_inst[i].instance_info.requiresPhysicallyContiguousMemory;
    if (!need_cont_mem) {
        QZ_DEBUG("Decompress SVM Enabled in doDecompressIn\n");
    }
    while (!done) {

        QZ_DEBUG("src_avail_len is %u, dest_avail_len is %u\n",
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
        case QZ_LOW_DEST_MEM:
        case QZ_FORCE_SW:
            tmp_src_avail_len = src_avail_len;
            tmp_dest_avail_len = dest_avail_len;
            rc = qzSWDecompress(sess,
                                src_ptr,
                                &tmp_src_avail_len,
                                dest_ptr,
                                &tmp_dest_avail_len);
            if (unlikely(rc != QZ_OK)) {
                sess->thd_sess_stat = rc;
                remaining = 0;
                break;
            }
            if (data_fmt == LZ4_FH) {
                sess->total_in  += tmp_src_avail_len;
                sess->total_out += tmp_dest_avail_len;
                src_ptr         += tmp_src_avail_len;
                dest_ptr        += tmp_dest_avail_len;
                src_avail_len   -= tmp_src_avail_len;
                dest_avail_len  -= tmp_dest_avail_len;
                remaining       -= tmp_src_avail_len;
            } else {
                sess->total_in  += qz_sess->inflate_strm->total_in;
                sess->total_out += qz_sess->inflate_strm->total_out;
                src_ptr         += qz_sess->inflate_strm->total_in;
                dest_ptr        += qz_sess->inflate_strm->total_out;
                src_avail_len   -= qz_sess->inflate_strm->total_in;
                dest_avail_len  -= qz_sess->inflate_strm->total_out;
                remaining       -= qz_sess->inflate_strm->total_in;
            }
            break;

        case QZ_OK:
            /*QZip decompression*/
            do {
                j = getUnusedBuffer(i, j);

                if (qz_sess->single_thread) {
                    if (unlikely((-1 == j) ||
                                 ((0 == qz_sess->seq % qz_sess->sess_params.req_cnt_thrshold) &&
                                  (qz_sess->seq > qz_sess->seq_in)))) {
                        return ((void *) NULL);
                    }
                } else {
                    if (unlikely(-1 == j)) {
                        nanosleep(&my_time, NULL);
                    }
                }
            } while (-1 == j);

            QZ_DEBUG("getUnusedBuffer returned %d\n", j);

            g_process.qz_inst[i].stream[j].src1++;/*this buffer is in use*/
            swapDataBuffer(i, j);
            src_ptr += outputHeaderSz(data_fmt);
            remaining -= outputHeaderSz(data_fmt);
            src_send_sz = hdr.extra.qz_e.dest_sz;
            dest_receive_sz = hdr.extra.qz_e.src_sz;

            g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes = src_send_sz;
            g_process.qz_inst[i].dest_buffers[j]->pBuffers->dataLenInBytes =
                dest_receive_sz;
            QZ_DEBUG("doDecompressIn: Sending %u bytes starting at 0x%lx\n",
                     src_send_sz, (unsigned long)src_ptr);

            /*this buffer is in use*/
            g_process.qz_inst[i].stream[j].seq = qz_sess->seq;
            qz_sess->seq++;
            QZ_DEBUG("sending seq number %lu %d %ld\n", i, j, qz_sess->seq);

            if (DEFLATE_GZIP_EXT == data_fmt ||
                DEFLATE_GZIP == data_fmt) {
                qzFooter = (StdGzF_T *)(src_ptr + src_send_sz);
                g_process.qz_inst[i].stream[j].checksum = qzFooter->crc32;
                g_process.qz_inst[i].stream[j].orgdatalen = qzFooter->i_size;
            } else if (LZ4_FH == data_fmt) {
                QzLZ4F_T *lz4Footer = (QzLZ4F_T *)(src_ptr + src_send_sz); //TODO
                g_process.qz_inst[i].stream[j].checksum = lz4Footer->cnt_cksum;
                g_process.qz_inst[i].stream[j].orgdatalen = hdr.extra.qz_e.src_sz;
            }

            qz_sess->submitted++;
            /*send to compression engine here*/
            g_process.qz_inst[i].stream[j].src2++;/*this buffer is in use*/

            /*set up src dest buffers*/
            if ((COMMON_MEM == src_mem_type) && need_cont_mem) {
                QZ_DEBUG("memory copy in doDecompressIn\n");
                QZ_MEMCPY(g_process.qz_inst[i].src_buffers[j]->pBuffers->pData,
                          src_ptr,
                          src_avail_len,
                          src_send_sz);
                g_process.qz_inst[i].stream[j].src_need_reset = 0;
            } else {
                g_process.qz_inst[i].src_buffers[j]->pBuffers->pData = src_ptr;
                g_process.qz_inst[i].stream[j].src_need_reset = 1;
            }

            if ((COMMON_MEM == dest_mem_type) && need_cont_mem) {
                g_process.qz_inst[i].stream[j].dest_need_reset = 0;
            } else {
                g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData = dest_ptr;
                g_process.qz_inst[i].stream[j].dest_need_reset = 1;
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
                if (unlikely(CPA_STATUS_RETRY == rc)) {
                    g_process.qz_inst[i].num_retries++;
                    usleep(g_polling_interval[qz_sess->polling_idx]);
                }

                if (unlikely(g_process.qz_inst[i].num_retries > MAX_NUM_RETRY)) {
                    QZ_ERROR("instance %lu retry count:%d exceed the max count: %d\n",
                             i, g_process.qz_inst[i].num_retries, MAX_NUM_RETRY);
                    goto err_exit;
                }
            } while (rc == CPA_STATUS_RETRY);

            if (unlikely(CPA_STATUS_SUCCESS != rc)) {
                QZ_ERROR("Error in cpaDcCompressData: %d\n", rc);
                goto err_exit;
            }

            g_process.qz_inst[i].num_retries = 0;
            src_avail_len -= (outputHeaderSz(data_fmt) + src_send_sz +
                              outputFooterSz(data_fmt));
            dest_avail_len -= dest_receive_sz;

            dest_ptr += dest_receive_sz;

            src_ptr += (src_send_sz + outputFooterSz(data_fmt));
            remaining -= (src_send_sz + outputFooterSz(data_fmt));
            break;

        default:
            /*Exception handler*/
            remaining = 0;
            break;
        }

        if (qz_sess->stop_submitting) {
            remaining = 0;
        }

        QZ_DEBUG("src_ptr is %p, remaining is %d\n", src_ptr, remaining);
        if (0 == remaining) {
            done = 1;
            qz_sess->last_submitted = 1;
        }
    }

    return ((void *)NULL);

err_exit:
    if (g_process.qz_inst[i].stream[j].src_need_reset) {
        g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_src;
        g_process.qz_inst[i].stream[j].src_need_reset = 0;
    }
    if (g_process.qz_inst[i].stream[j].dest_need_reset) {
        g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
            g_process.qz_inst[i].stream[j].orig_dest;
        g_process.qz_inst[i].stream[j].dest_need_reset = 0;

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
/* A fix for the chunksize test performance. Without the attribute
 * cold it will lead to a performance drop in the chunksize test.
 * Will root cause it and fix it in the future version
 */
static void *__attribute__((cold)) doDecompressOut(void *in)
{
    int i = 0, j = 0, si = 0, good;
    CpaDcRqResults *resl;
    CpaStatus sts;
    unsigned int sleep_cnt = 0;
    unsigned int done = 0;
    unsigned int src_send_sz;
    unsigned int dest_avail_len;
    QzSession_T *sess = (QzSession_T *)in;
    QzSess_T *qz_sess = (QzSess_T *)sess->internal;
    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    QzPollingMode_T polling_mode = qz_sess->sess_params.polling_mode;

    QZ_DEBUG("mw>> function %s() called\n", __func__);
    i = qz_sess->inst_hint;
    dest_avail_len = *qz_sess->dest_sz - qz_sess->qz_out_len;

    while (!done) {
        /*Poll for responses*/
        good = 0;
        sts = icp_sal_DcPollInstance(g_process.dc_inst_handle[i], 0);
        if (unlikely(CPA_STATUS_FAIL == sts)) {
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

                assert(g_process.qz_inst[i].stream[j].seq == qz_sess->seq_in);
                qz_sess->seq_in++;

                if (unlikely(CPA_STATUS_SUCCESS != g_process.qz_inst[i].stream[j].job_status)) {
                    QZ_ERROR("Error(%d) in callback: %d, %d, ReqStatus: %d\n",
                             g_process.qz_inst[i].stream[j].job_status, i, j,
                             g_process.qz_inst[i].stream[j].res.status);
                    RESTORE_SWAP_CPASTREAM_BUFFER(i, j);
                    sess->thd_sess_stat = QZ_FAIL;
                    continue;
                }

                if (unlikely(QZ_FAIL == sess->thd_sess_stat)) {
                    // There is an error in previous process,
                    // we just restore buffer here.
                    RESTORE_SWAP_CPASTREAM_BUFFER(i, j);
                    continue;
                }

                resl = &g_process.qz_inst[i].stream[j].res;
                QZ_DEBUG("\tconsumed = %d, produced = %d, seq_in = %ld, src_send_sz = %u\n",
                         resl->consumed, resl->produced, g_process.qz_inst[i].stream[j].seq,
                         g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes);

                if (!g_process.qz_inst[i].stream[j].dest_need_reset) {
                    QZ_DEBUG("memory copy in doDecompressOut\n");
                    QZ_MEMCPY(qz_sess->next_dest,
                              g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData,
                              dest_avail_len,
                              resl->produced);
                } else {
                    g_process.qz_inst[i].dest_buffers[j]->pBuffers->pData =
                        g_process.qz_inst[i].stream[j].orig_dest;
                    g_process.qz_inst[i].stream[j].dest_need_reset = 0;
                }

                if (g_process.qz_inst[i].stream[j].src_need_reset) {
                    g_process.qz_inst[i].src_buffers[j]->pBuffers->pData =
                        g_process.qz_inst[i].stream[j].orig_src;
                    g_process.qz_inst[i].stream[j].src_need_reset = 0;
                }


                if (data_fmt != DEFLATE_4B) {
                    if (unlikely(resl->checksum !=
                                 g_process.qz_inst[i].stream[j].checksum ||
                                 resl->produced != g_process.qz_inst[i].stream[j].orgdatalen)) {
                        QZ_ERROR("Error in check footer, inst %d, stream %d\n", i, j);
                        QZ_DEBUG("resp checksum: %x data checksum %x\n",
                                 resl->checksum,
                                 g_process.qz_inst[i].stream[j].checksum);
                        QZ_DEBUG("resp produced :%d data produced: %d\n",
                                 resl->produced,
                                 g_process.qz_inst[i].stream[j].orgdatalen);

                        swapDataBuffer(i, j);
                        sess->thd_sess_stat = QZ_DATA_ERROR;
                        qz_sess->processed++;
                        qz_sess->stop_submitting = 1;
                        g_process.qz_inst[i].stream[j].sink2++;
                        continue;
                    }
                }

                src_send_sz = g_process.qz_inst[i].src_buffers[j]->pBuffers->dataLenInBytes;
                qz_sess->next_dest += resl->produced;
                qz_sess->qz_in_len += (outputHeaderSz(data_fmt) + src_send_sz +
                                       outputFooterSz(data_fmt));
                qz_sess->qz_out_len += resl->produced;
                dest_avail_len -= resl->produced;
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

        if (QZ_PERIODICAL_POLLING == polling_mode) {
            if (0 == good) {
                qz_sess->polling_idx = (qz_sess->polling_idx >= POLLING_LIST_NUM - 1) ?
                                       (POLLING_LIST_NUM - 1) :
                                       (qz_sess->polling_idx + 1);

                QZ_DEBUG("decomp sleep for %d usec...\n",
                         g_polling_interval[qz_sess->polling_idx]);
                usleep(g_polling_interval[qz_sess->polling_idx]);
                sleep_cnt++;
            } else {
                qz_sess->polling_idx = (qz_sess->polling_idx == 0) ? (0) :
                                       (qz_sess->polling_idx - 1);
            }
        }
    }

    QZ_DEBUG("Decomp sleep_cnt: %u\n", sleep_cnt);
    qz_sess->last_processed = qz_sess->last_submitted ? 1 : 0;
    return NULL;

err_exit:
    qz_sess->stop_submitting = 1;
    qz_sess->last_processed = 1;

    for (si = 0; si < g_process.qz_inst[i].dest_count; si++) {
        if (g_process.qz_inst[i].stream[si].src_need_reset) {
            g_process.qz_inst[i].src_buffers[si]->pBuffers->pData =
                g_process.qz_inst[i].stream[si].orig_src;
            g_process.qz_inst[i].stream[si].src_need_reset = 0;
        }
        if (g_process.qz_inst[i].stream[si].dest_need_reset) {
            g_process.qz_inst[i].dest_buffers[si]->pBuffers->pData =
                g_process.qz_inst[i].stream[si].orig_dest;
            g_process.qz_inst[i].stream[si].dest_need_reset = 0;
        }
    }
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
    return qzDecompressExt(sess, src, src_len, dest, dest_len, NULL);
}

int qzDecompressExt(QzSession_T *sess, const unsigned char *src,
                    unsigned int *src_len, unsigned char *dest,
                    unsigned int *dest_len, uint64_t *ext_rc)
{
    int rc;
    int i, reqcnt;
    QzSess_T *qz_sess;
    QzGzH_T *hdr = (QzGzH_T *)src;

    if (unlikely(NULL == sess                 || \
                 NULL == src                  || \
                 NULL == src_len              || \
                 NULL == dest                 || \
                 NULL == dest_len)) {
        if (NULL != src_len) {
            *src_len = 0;
        }
        if (NULL != dest_len) {
            *dest_len = 0;
        }
        return QZ_PARAMS;
    }

    if (0 == *src_len) {
        *dest_len = 0;
        return QZ_OK;
    }

    /*check if init called*/
    rc = qzInit(sess, getSwBackup(sess));
    if (QZ_INIT_FAIL(rc)) {
        *src_len = 0;
        *dest_len = 0;
        return rc;
    }

    /*check if setupSession called*/
    if (NULL == sess->internal || QZ_NONE == sess->hw_session_stat) {
        if (g_sess_params_internal_default.data_fmt == LZ4_FH) {
            rc = qzSetupSessionLZ4(sess, NULL);
        } else if (g_sess_params_internal_default.data_fmt == LZ4S_BK) {
            rc = qzSetupSessionLZ4S(sess, NULL);
        } else {
            rc = qzSetupSessionDeflate(sess, NULL);
        }
        if (unlikely(QZ_SETUP_SESSION_FAIL(rc))) {
            *src_len = 0;
            *dest_len = 0;
            return rc;
        }
    }

    qz_sess = (QzSess_T *)(sess->internal);

    DataFormatInternal_T data_fmt = qz_sess->sess_params.data_fmt;
    if (unlikely(data_fmt != DEFLATE_RAW &&
                 data_fmt != DEFLATE_4B &&
                 data_fmt != DEFLATE_GZIP &&
                 data_fmt != LZ4_FH &&
                 data_fmt != DEFLATE_GZIP_EXT)) {
        QZ_ERROR("Unknown/unsupported data format: %d\n", data_fmt);
        *src_len = 0;
        *dest_len = 0;
        return QZ_PARAMS;
    }

    QZ_DEBUG("qzDecompress data_fmt: %d\n", data_fmt);
    if (data_fmt == DEFLATE_RAW ||
        (data_fmt == DEFLATE_GZIP_EXT &&
         hdr->extra.qz_e.src_sz < qz_sess->sess_params.input_sz_thrshold) ||
        g_process.qz_init_status == QZ_NO_HW                            ||
        sess->hw_session_stat == QZ_NO_HW                               ||
        !(isQATProcessable(src, src_len, qz_sess))                      ||
        qz_sess->inflate_stat == InflateOK) {
        QZ_DEBUG("decompression src_len=%u, hdr->extra.qz_e.src_sz = %u, "
                 "g_process.qz_init_status = %d, sess->hw_session_stat = %ld, "
                 "isQATProcessable = %d, switch to software.\n",
                 *src_len,  hdr->extra.qz_e.src_sz,
                 g_process.qz_init_status, sess->hw_session_stat,
                 isQATProcessable(src, src_len, qz_sess));
        goto sw_decompression;
    } else if (sess->hw_session_stat != QZ_OK &&
               sess->hw_session_stat != QZ_NO_INST_ATTACH) {
        *src_len = 0;
        *dest_len = 0;
        return sess->hw_session_stat;
    }

    i = qzGrabInstance(qz_sess->inst_hint, data_fmt);
    if (unlikely(i == -1)) {
        if (qz_sess->sess_params.sw_backup == 1) {
            goto sw_decompression;
        } else {
            sess->hw_session_stat = QZ_NO_INST_ATTACH;
            *src_len = 0;
            *dest_len = 0;
            return QZ_NOSW_NO_INST_ATTACH;
        }
        /*Make this a s/w compression*/
    }
    QZ_DEBUG("qzDecompress: inst is %d\n", i);
    qz_sess->inst_hint = i;

    if (likely(0 ==  g_process.qz_inst[i].mem_setup ||
               0 ==  g_process.qz_inst[i].cpa_sess_setup)) {
        QZ_DEBUG("Getting HW resources for inst %d\n", i);
        rc = qzSetupHW(sess, i);
        if (unlikely(QZ_OK != rc)) {
            qzReleaseInstance(i);
            if (QZ_LOW_MEM == rc || QZ_NO_INST_ATTACH == rc) {
                goto sw_decompression;
            } else {
                *src_len = 0;
                *dest_len = 0;
                return rc;
            }
        }
    } else if (memcmp(&g_process.qz_inst[i].session_setup_data,
                      &qz_sess->session_setup_data, sizeof(CpaDcSessionSetupData))) {
        /* session_setup_data of qz_sess is not same with instance i,
           need to update cpa session of instance i. */
        rc = qzUpdateCpaSession(sess, i);
        if (QZ_OK != rc) {
            qzReleaseInstance(i);
            *src_len = 0;
            *dest_len = 0;
            return rc;
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

    QZ_DEBUG("PRoduced %lu bytes\n", sess->total_out);
    rc = checkSessionState(sess);

    sess->total_in += qz_sess->qz_in_len;
    sess->total_out += qz_sess->qz_out_len;
    *src_len = GET_LOWER_32BITS(sess->total_in);
    *dest_len = GET_LOWER_32BITS(sess->total_out);

    QZ_DEBUG("total_in=%lu total_out=%lu src_len=%u dest_len=%u rc=%d src_len=%d dest_len=%d\n",
             sess->total_in, sess->total_out, *src_len, *dest_len, rc, *src_len, *dest_len);
    return rc;

sw_decompression:
    return qzSWDecompressMulti(sess, src, src_len, dest, dest_len);
}

int qzTeardownSession(QzSession_T *sess)
{
    if (unlikely(sess == NULL)) {
        return QZ_PARAMS;
    }

    if (likely(NULL != sess->internal)) {
        QzSess_T *qz_sess = (QzSess_T *) sess->internal;
        if (likely(NULL != qz_sess->inflate_strm)) {
            inflateEnd(qz_sess->inflate_strm);
            free(qz_sess->inflate_strm);
            qz_sess->inflate_strm = NULL;
        }

        if (likely(NULL != qz_sess->deflate_strm)) {
            deflateEnd(qz_sess->deflate_strm);
            free(qz_sess->deflate_strm);
            qz_sess->deflate_strm = NULL;
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
    if (unlikely(sess == NULL)) {
        return QZ_PARAMS;
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
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    if (qzCheckParams(defaults) != QZ_OK) {
        return QZ_PARAMS;
    }

    qzSetParams(defaults, &g_sess_params_internal_default);

    return QZ_OK;
}

int qzSetDefaultsDeflate(QzSessionParamsDeflate_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    if (qzCheckParamsDeflate(defaults) != QZ_OK) {
        return QZ_PARAMS;
    }

    qzSetParamsDeflate(defaults, &g_sess_params_internal_default);

    return QZ_OK;
}

int qzSetDefaultsLZ4(QzSessionParamsLZ4_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    if (qzCheckParamsLZ4(defaults) != QZ_OK) {
        return QZ_PARAMS;
    }

    qzSetParamsLZ4(defaults, &g_sess_params_internal_default);

    return QZ_OK;
}

int qzSetDefaultsLZ4S(QzSessionParamsLZ4S_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    if (qzCheckParamsLZ4S(defaults) != QZ_OK) {
        return QZ_PARAMS;
    }

    qzSetParamsLZ4S(defaults, &g_sess_params_internal_default);

    return QZ_OK;
}

int qzGetDefaults(QzSessionParams_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    qzGetParams(&g_sess_params_internal_default, defaults);

    return QZ_OK;
}

int qzGetDefaultsDeflate(QzSessionParamsDeflate_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    qzGetParamsDeflate(&g_sess_params_internal_default, defaults);

    return QZ_OK;
}

int qzGetDefaultsLZ4(QzSessionParamsLZ4_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    qzGetParamsLZ4(&g_sess_params_internal_default, defaults);

    return QZ_OK;
}

int qzGetDefaultsLZ4S(QzSessionParamsLZ4S_T *defaults)
{
    if (defaults == NULL) {
        return QZ_PARAMS;
    }

    qzGetParamsLZ4S(&g_sess_params_internal_default, defaults);

    return QZ_OK;
}

static unsigned int qzDeflateBound(unsigned int src_sz, QzSession_T *sess)
{
    unsigned int dest_sz = 0;
    unsigned int chunk_cnt = 0;
    unsigned int hw_buffer_sz = 0;
    QzSess_T *qz_sess = NULL;
    CpaDcHuffType huffman_type;
    CpaStatus status = CPA_STATUS_SUCCESS;

    qz_sess = (QzSess_T *)sess->internal;

    /* Get the Huffman Tree type. */
    if (qz_sess->sess_params.huffman_hdr == QZ_DYNAMIC_HDR) {
        huffman_type = CPA_DC_HT_FULL_DYNAMIC;
    } else {
        huffman_type = CPA_DC_HT_STATIC;
    }
    status = cpaDcDeflateCompressBound(NULL, huffman_type, src_sz, &dest_sz);
    if (status != CPA_STATUS_SUCCESS) {
        return 0;
    }
    hw_buffer_sz = qz_sess->sess_params.hw_buff_sz;

    /* cpaDcDeflateCompressBound only provides the maximum output size of deflate blocks,
     * it does not include gzip/gzip-ext header and footer size, so we need to update dest_sz
     * for header and footer size.
     */
    /* Calculate how many gzip/gzip-ext headers and footers will be generated. */
    chunk_cnt = src_sz / hw_buffer_sz + src_sz %
                hw_buffer_sz ? 1 : 0;
    dest_sz += chunk_cnt * (qzGzipHeaderSz() + stdGzipFooterSz());

    return dest_sz;
}

static unsigned int qzLZ4SBound(unsigned int src_sz, QzSession_T *sess)
{
    unsigned int dest_sz = 0;
    CpaStatus status = CPA_STATUS_SUCCESS;

#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
    status = cpaDcLZ4SCompressBound(NULL, src_sz, &dest_sz);
    if (status != CPA_STATUS_SUCCESS) {
        return 0;
    }
#else
    (void)status;
    return 0;
#endif

    return dest_sz;
}

static unsigned int qzLZ4Bound(unsigned int src_sz, QzSession_T *sess)
{
    unsigned int dest_sz = 0;
    unsigned int chunk_cnt = 0;
    QzSess_T *qz_sess = NULL;
    CpaStatus status = CPA_STATUS_SUCCESS;

    assert(sess);
    assert(sess->internal);

#if CPA_DC_API_VERSION_AT_LEAST(3, 1)
    status = cpaDcLZ4CompressBound(NULL, src_sz, &dest_sz);
    if (status != CPA_STATUS_SUCCESS) {
        return 0;
    }
#else
    (void)status;
    return 0;
#endif

    /* cpaDcLZ4CompressBound only provides the maximum output size of lz4 blocks,
     * it does not include lz4 frames header and footer size, so we need to update dest_sz
     * for header and footer size.
     */
    /* Calculate how many frames header and footer will be generated */
    qz_sess = (QzSess_T *)sess->internal;
    chunk_cnt = src_sz / qz_sess->sess_params.hw_buff_sz + src_sz %
                qz_sess->sess_params.hw_buff_sz ? 1 : 0;
    dest_sz += chunk_cnt * outputHeaderSz(qz_sess->sess_params.data_fmt) +
               chunk_cnt * outputFooterSz(qz_sess->sess_params.data_fmt);

    return dest_sz;
}

unsigned int qzMaxCompressedLength(unsigned int src_sz, QzSession_T *sess)
{
    unsigned int dest_sz = 0;
    unsigned int chunk_cnt = 0;

    QzSess_T *qz_sess = NULL;

    if (src_sz == 0) {
        return QZ_COMPRESSED_SZ_OF_EMPTY_FILE;
    }

    if (sess == NULL || sess->internal == NULL || sess->hw_session_stat != QZ_OK) {
        uint64_t in_sz = src_sz;
        uint64_t out_sz = 0;
        out_sz = QZ_CEIL_DIV(9 * in_sz, 8) + QZ_SKID_PAD_SZ;
        chunk_cnt = in_sz / QZ_HW_BUFF_SZ + in_sz %
                    QZ_HW_BUFF_SZ ? 1 : 0;
        out_sz += chunk_cnt * (qzGzipHeaderSz() + stdGzipFooterSz());
        if (out_sz & 0xffffffff00000000UL)
            return 0;
        dest_sz = (unsigned int)out_sz;
        return dest_sz;
    }

    qz_sess = (QzSess_T *)sess->internal;
    switch (qz_sess->sess_params.data_fmt) {
    case DEFLATE_RAW:
    case DEFLATE_GZIP:
    case DEFLATE_GZIP_EXT:
    case DEFLATE_4B:
        dest_sz = qzDeflateBound(src_sz, sess);
        break;
    case LZ4_FH:
        dest_sz = qzLZ4Bound(src_sz, sess);
        break;
    case LZ4S_BK:
        dest_sz = qzLZ4SBound(src_sz, sess);
        break;
    default:
        dest_sz = 0;
        break;
    }

    QZ_DEBUG("src_sz is %u, dest_sz is %u\n", src_sz, dest_sz);
    return dest_sz;
}

int qzGetSoftwareComponentCount(unsigned int *num_elem)
{
    QZ_ERROR("qatzip don't support qzGetSoftwareComponentCount API yet!\n");
    return QZ_FAIL;
}

int qzGetSoftwareComponentVersionList(QzSoftwareVersionInfo_T *api_info,
                                      unsigned int *num_elem)
{
    QZ_ERROR("qatzip don't support qzGetSoftwareComponentVersionList API yet!\n");
    return QZ_FAIL;
}
