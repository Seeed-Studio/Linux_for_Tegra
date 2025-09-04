/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <nv.h>

#include <nv-priv.h>
#include <osapi.h>
#include <core/locks.h>

static NV_STATUS
RmPowerManagementInternalTegra(
    OBJGPU *pGpu,
    nv_pm_action_t pmAction
)
{
    //
    // Default to NV_OK. there may cases where resman is loaded, but
    // no devices are allocated (we're still at the console). in these
    // cases, it's fine to let the system do whatever it wants.
    //
    NV_STATUS rmStatus = NV_OK;

    if (pGpu)
    {
        nv_state_t *nv = NV_GET_NV_STATE(pGpu);
        nv_priv_t  *nvp = NV_GET_NV_PRIV(nv);

        switch (pmAction)
        {
            case NV_PM_ACTION_HIBERNATE:
                nvp->pm_state.InHibernate = NV_TRUE;
            case NV_PM_ACTION_STANDBY:
                nvp->pm_state.InHibernate = NV_FALSE;
                pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_PM_CODEPATH, NV_TRUE);
                rmStatus = gpuStateUnload(pGpu,
                    IS_GPU_GC6_STATE_ENTERING(pGpu) ?
                    GPU_STATE_FLAGS_PRESERVING | GPU_STATE_FLAGS_PM_TRANSITION | GPU_STATE_FLAGS_GC6_TRANSITION :
                    GPU_STATE_FLAGS_PRESERVING | GPU_STATE_FLAGS_PM_TRANSITION);
                pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_STANDBY, NV_TRUE);
                break;

            case NV_PM_ACTION_RESUME:
                if (!nvp->pm_state.InHibernate)
                {
                    pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_PM_RESUME_CODEPATH, NV_TRUE);
                    rmStatus = gpuStateLoad(pGpu,
                        IS_GPU_GC6_STATE_ENTERING(pGpu) ?
                        GPU_STATE_FLAGS_PRESERVING | GPU_STATE_FLAGS_PM_TRANSITION | GPU_STATE_FLAGS_GC6_TRANSITION :
                        GPU_STATE_FLAGS_PRESERVING | GPU_STATE_FLAGS_PM_TRANSITION);
                    pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_STANDBY, NV_FALSE);
                    pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_PM_CODEPATH, NV_FALSE);
                    pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_PM_RESUME_CODEPATH, NV_FALSE);
                }
                break;

            default:
                rmStatus = NV_ERR_INVALID_ARGUMENT;
                break;
        }

    }

    return rmStatus;
}

NV_STATUS NV_API_CALL rm_power_management(
    nvidia_stack_t *sp,
    nv_state_t *pNv,
    nv_pm_action_t pmAction
)
{
    THREAD_STATE_NODE threadState;
    NV_STATUS rmStatus = NV_OK;
    void *fp;

    NV_ENTER_RM_RUNTIME(sp,fp);
    threadStateInit(&threadState, THREAD_STATE_FLAGS_NONE);

    NV_ASSERT_OK(os_flush_work_queue(pNv->queue));

    // LOCK: acquire API lock
    if ((rmStatus = rmApiLockAcquire(API_LOCK_FLAGS_NONE, RM_LOCK_MODULES_DYN_POWER)) == NV_OK)
    {
        OBJGPU *pGpu = NV_GET_NV_PRIV_PGPU(pNv);

        if (pGpu != NULL)
        {
            // LOCK: acquire GPUs lock
            if ((rmStatus = rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_DYN_POWER)) == NV_OK)
            {
                rmStatus = RmPowerManagementInternalTegra(pGpu, pmAction);

                //
                // RmPowerManagementInternalTegra() is most likely to fail due to
                // gpuStateUnload() failures deep in the RM's GPU power
                // management paths.  However, those paths make no
                // attempt to unwind in case of errors.  Rather, they
                // soldier on and simply report an error at the very end.
                // GPU software state meanwhile will indicate the GPU
                // has been suspended.
                //
                // Sadly, in case of an error during suspend/hibernate,
                // the only path forward here is to attempt to resume the
                // GPU, accepting that the odds of success will vary.
                //
                if (rmStatus != NV_OK && pmAction != NV_PM_ACTION_RESUME)
                {
                    RmPowerManagementInternalTegra(pGpu, NV_PM_ACTION_RESUME);
                }

                // UNLOCK: release GPUs lock
                rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);
            }
        }
        // UNLOCK: release API lock
        rmApiLockRelease();
    }

    NV_ASSERT_OK(os_flush_work_queue(pNv->queue));

    threadStateFree(&threadState, THREAD_STATE_FLAGS_NONE);
    NV_EXIT_RM_RUNTIME(sp,fp);

    return rmStatus;
}
