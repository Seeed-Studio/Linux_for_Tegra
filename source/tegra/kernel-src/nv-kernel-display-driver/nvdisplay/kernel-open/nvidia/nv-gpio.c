/*
 * SPDX-FileCopyrightText: Copyright (c) 2020 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define  __NO_VERSION__

#include "os-interface.h"
#include "nv-linux.h"

#include "os_gpio.h"

#define NV_GPIOF_DIR_IN    (1 << 0)

/*!
 * @brief Mapping array of OS GPIO function ID to OS function name,
 * this name is used to get GPIO number from Device Tree.
 */
static const char *osMapGpioFunc[] = {
    [NV_OS_GPIO_FUNC_HOTPLUG_A] = "os_gpio_hotplug_a",
    [NV_OS_GPIO_FUNC_HOTPLUG_B] = "os_gpio_hotplug_b",
};

NV_STATUS NV_API_CALL nv_gpio_get_pin_state
(
    nv_state_t *nv,
    NvU32 pinNum,
    NvU32 *pinValue
)
{
    int ret;

#if defined(NV_GPIO_GET_VALUE_PRESENT)
    ret = gpio_get_value(pinNum);
#else
    nv_printf(NV_DBG_ERRORS, "gpio_get_value not present\n");
    return NV_ERR_GENERIC;
#endif
    if (ret < 0)
    {
        nv_printf(NV_DBG_ERRORS, "%s: failed with err: %d\n",
                  __func__, ret);
        return NV_ERR_GENERIC;
    }

    *pinValue = ret;

    return NV_OK;
}

void NV_API_CALL nv_gpio_set_pin_state
(
    nv_state_t *nv,
    NvU32 pinNum,
    NvU32 pinValue
)
{
#if defined(NV_GPIO_SET_VALUE_PRESENT)
    gpio_set_value(pinNum, pinValue);
#else
    nv_printf(NV_DBG_ERRORS, "gpio_set_value not present\n");
#endif
}

NV_STATUS NV_API_CALL nv_gpio_set_pin_direction
(
    nv_state_t *nv,
    NvU32 pinNum,
    NvU32 direction
)
{
    int ret;

    if (direction)
    {
#if defined(NV_GPIO_DIRECTION_INPUT_PRESENT)
        ret = gpio_direction_input(pinNum);
#else
        nv_printf(NV_DBG_ERRORS, "gpio_direction_input not present\n");
        return NV_ERR_GENERIC;
#endif
    }
    else
    {
#if defined(NV_GPIO_DIRECTION_OUTPUT_PRESENT)
        ret = gpio_direction_output(pinNum, 0);
#else
        nv_printf(NV_DBG_ERRORS, "gpio_direction_output not present\n");
        return NV_ERR_GENERIC;
#endif
    }

    if (ret)
    {
        nv_printf(NV_DBG_ERRORS, "%s: failed with err: %d\n",
                  __func__, ret);
        return NV_ERR_GENERIC;
    }

    return NV_OK;
}

NV_STATUS NV_API_CALL nv_gpio_get_pin_direction
(
    nv_state_t *nv,
    NvU32 pinNum,
    NvU32 *direction
)
{
/*!
 * TODO: Commenting out until gpio_get_direction wrapper
 * support is added in kernel.
 */
#if 0
    int ret;

    ret = nv_gpio_get_direction(pinNum);
    if (ret)
    {
        nv_printf(NV_DBG_ERRORS, "%s: failed with err: %d\n",
                  __func__, ret);
        return NV_ERR_GENERIC;
    }
    *direction = ret;
#endif

    return NV_OK;
}

NV_STATUS NV_API_CALL nv_gpio_get_pin_number
(
    nv_state_t *nv,
    NvU32 function,
    NvU32 *pinNum
)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    int rc;

#if defined(NV_OF_GET_NAME_GPIO_PRESENT)
    rc = of_get_named_gpio(nvl->dev->of_node, osMapGpioFunc[function], 0);
#else
    nv_printf(NV_DBG_ERRORS, "of_get_named_gpio not present\n");
    return NV_ERR_GENERIC;
#endif
    if (rc < 0)
    {
        nv_printf(NV_DBG_ERRORS, "of_get_name_gpio failed for gpio - %s, rc - %d\n",
                  osMapGpioFunc[function], rc);
        return NV_ERR_GENERIC;
    }
    *pinNum = rc;

#if defined(NV_DEVM_GPIO_REQUEST_ONE_PRESENT)
    rc = devm_gpio_request_one(nvl->dev, *pinNum, NV_GPIOF_DIR_IN,
                               osMapGpioFunc[function]);
#else
    nv_printf(NV_DBG_ERRORS, "devm_gpio_request_one not present\n");
    return NV_ERR_GENERIC;
#endif
    if (rc < 0)
    {
        nv_printf(NV_DBG_ERRORS, "request gpio failed for gpio - %s, rc - %d\n",
                  osMapGpioFunc[function], rc);
        return NV_ERR_GENERIC;
    }

    return NV_OK;
}

NvBool NV_API_CALL nv_gpio_get_pin_interrupt_status
(
    nv_state_t *nv,
    NvU32 pinNum,
    NvU32 direction
)
{
    NvU32 irqGpioPin;
    NvU32 pinValue;

    if (nv_get_current_irq_type(nv) != NV_SOC_IRQ_GPIO_TYPE)
    {
        return NV_FALSE;
    }

    nv_get_current_irq_priv_data(nv, &irqGpioPin);
    if (pinNum != irqGpioPin)
    {
        return NV_FALSE;
    }

#if defined(NV_GPIO_GET_VALUE_PRESENT)
    pinValue = gpio_get_value(pinNum);
#else
    nv_printf(NV_DBG_ERRORS, "gpio_get_value not present\n");
    return NV_FALSE;
#endif
    if (pinValue != direction)
    {
        return NV_FALSE;
    }

    return NV_TRUE;
}

NV_STATUS NV_API_CALL nv_gpio_set_pin_interrupt
(
    nv_state_t * nv,
    NvU32 pinNum,
    NvU32 trigger_level
)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    int rc;
    int irq_num;

#if defined(NV_GPIO_TO_IRQ_PRESENT)
    irq_num = gpio_to_irq(pinNum);
#else
    nv_printf(NV_DBG_ERRORS, "gpio_to_irq not present\n");
    return NV_ERR_GENERIC;
#endif

    /*
     * TODO:Ignoring trigger_level as RM calls this function twice to set
     * the level(rising/falling seperately) of interrupt for same gpio pin,
     * so hardcoding the trigger_level to rising/falling during first
     * registration as second time this registration fails as interrupt
     * is already registered. For initial GPIO support let interrupt
     * registration fail for second time, will plan to check the status if
     * interrupt is already registered and skip for second time in the
     * follow-up patch.
     */

    rc = nv_request_soc_irq(nvl, irq_num, NV_SOC_IRQ_GPIO_TYPE,
                            (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
                            IRQF_ONESHOT), pinNum);
    if (rc < 0)
    {
        nv_printf(NV_DBG_ERRORS, "IRQ registration failed for gpio - %d, rc - %d\n",
                  pinNum, rc);
        return NV_ERR_GENERIC;
    }

    return NV_OK;
}
