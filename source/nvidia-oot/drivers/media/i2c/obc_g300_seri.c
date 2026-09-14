/*
 * orb.c - Orbbec G3XX camera driver
 *
 * Copyright (c) 2023-2025, ORBBEC CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
//#define DEBUG
#include <linux/kernel.h>
#include <linux/delay.h>

#include <media/gmsl-link.h>
#include <media/obc_max9296.h>

#include <media/obc_g300_priv.h>


//-------------------------------------------------------------------------------------------
#define MAX9295D_PIPE_EN_ADDR 		0x2
#define MAX9295D_START_PIPE_ADDR 	0x311
#define MAX9295D_MIPI_RX0_ADDR 		0x330
#define MAX9295D_MIPI_RX1_ADDR 		0x331
#define MAX9295D_CSI_PORT_SEL_ADDR 	0x308

//-------------------------------------------------------------------------------------------
#define MAX9295D_MAX_PIPES 			4

#define SERI_START_ADDR             0x90
#define TO_SERI_ADDR(A)             (SERI_START_ADDR + (A)*2)

//-------------------------------------------------------------------------------------------
#define ORBBEC_POWER_GPIOA_ADDR     0x2D6
#define ORBBEC_POWER_GPIOB_ADDR     0x2D7

//-------------------------------------------------------------------------------------------
static int max9295d_set_registers(struct orb *state, struct max_reg_pair *map, u16 count)
{
    return state->deser_ops->write_seri_tab(state->dser_dev, state->dser_link, map, count);
}

static int max9295d_write_reg(struct orb *state, u16 addr, u8 val)
{
    return state->deser_ops->write_seri_reg(state->dser_dev, state->dser_link, addr, val);
}

#if 1
// static u8 max9295_pipes_en = 0;

int max9295d_pipe_en(struct orb *state, u8 pipe_id, u8 en)
{
#if 0
    if((max9295_pipes_en & (1<<pipe_id)) && en)
        return 0;
    if(!(max9295_pipes_en & (1<<pipe_id)) && !en)
        return 0;
    if(en)
        max9295_pipes_en |= (1<<pipe_id);
    else
        max9295_pipes_en &= ~(1<<pipe_id);
    return max9295d_write_reg(state, MAX9295D_PIPE_EN_ADDR, (max9295_pipes_en<<4));
#else
    // max9295d_write_reg(state, 0x10, 0x21);
    return 0;
#endif
}
#endif

int max9295d_set_pipe(struct orb *state, u8 pipe_id, u8 data_type1, u8 data_type2, u8 src_vc_id)
{
    int err = 0;
    static u8 pipe_x_val = 0x0;

    struct max_reg_pair map_multi_pipe_en[] = {
        {0x0315, 0x80},
    };
    struct max_reg_pair map_bpp8dbl[] = {
        {0x0312, 0x0F}, // Send 8-bit pixels as 16-bit on Video Pipe X,Y,Z,U, This is “double pixel” mode
    };
    struct max_reg_pair map_pipe_control[] = {
        {0x0314, 0x5E}, // Pipe X route data_type1
        {0x0315, 0x52}, // Pipe X route data_type2
        {0x0309, 0x01}, // Pipe X vc_id low byte
        {0x030A, 0x00}, // Pipe X vc_id high byte
        {0x031C, 0x30}, // [5]: Pipe X override enable, [4:0]: override value is 0x10 = 16bit, raw8 double 
        {0x0102, 0x0E}, // Pipe X: LIM_HEART=1, Disable heartbeat during blanking
    };

    if (data_type1 == GMSL_CSI_DT_RAW_8 || data_type1 == GMSL_CSI_DT_EMBED
        || data_type2 == GMSL_CSI_DT_RAW_8 || data_type2 == GMSL_CSI_DT_EMBED) {
        map_bpp8dbl[0].val |= (1 << pipe_id);
    } else {
        map_bpp8dbl[0].val &= ~(1 << pipe_id);
    }

    err |= max9295d_set_registers(state, map_bpp8dbl, ARRAY_SIZE(map_bpp8dbl));

    map_pipe_control[0].addr += 0x2 * pipe_id;
    map_pipe_control[1].addr += 0x2 * pipe_id;
    map_pipe_control[2].addr += 0x2 * pipe_id;
    map_pipe_control[3].addr += 0x2 * pipe_id;
    map_pipe_control[4].addr += 0x1 * pipe_id;
    map_pipe_control[5].addr += 0x8 * pipe_id;

    map_pipe_control[0].val = 0x40 | data_type1; //MSb is enable
    map_pipe_control[1].val = 0x40 | data_type2; //MSb is enable
    map_pipe_control[2].val = (1<<src_vc_id);
    map_pipe_control[3].val = 0x00;
    map_pipe_control[4].val = (data_type1 == GMSL_CSI_DT_RGB_888) ? 0x18 : 0x30;
    map_pipe_control[5].val = 0x0E;

    err |= max9295d_set_registers(state, map_pipe_control, ARRAY_SIZE(map_pipe_control));

    if (pipe_id == 0)
        pipe_x_val = map_pipe_control[1].val;

    map_multi_pipe_en[0].val = 0x80 | pipe_x_val;
    err |= max9295d_set_registers(state, map_multi_pipe_en, ARRAY_SIZE(map_multi_pipe_en));

    dev_dbg(state->dser_dev, "%s: done, err=%d\n", __func__, err);

    return err;
}

int max9295d_release_pipe(struct orb *state, u8 pipe_id)
{
    return 0; //max9295d_pipe_en(state, pipe_id, 0);
}

int max9295d_init_settings(struct orb *state)
{
    int err = 0;
    int i;
    //struct max9295d *priv = dev_get_drvdata(dev);

    struct max_reg_pair link_map_pipe_opt[] = {
        // Enable all pipes
        {MAX9295D_PIPE_EN_ADDR, 0xF3}, //0x02, 0xF3
        // 0x06 -> 2x4,A&B, 0x05 -> 2x4,only B, 0x04 -> 2x4,only A, only for MAX9295D
        {MAX9295D_MIPI_RX0_ADDR, 0x06}, //0x330
        // 0x11->2 lanes, 0x33->4 lanes
        {MAX9295D_MIPI_RX1_ADDR, 0x11}, //0x331
        // Enable line info, Enable CSI Port A&B, Pipeline X&Z&U -> Port A ，Y -> Port B
        {MAX9295D_CSI_PORT_SEL_ADDR, 0x72}, //0x308
        // Pipeline X&Z&U from Port A ，Y from Port B
        {MAX9295D_START_PIPE_ADDR, 0x2D}, //0x311
    };
    struct max_reg_pair gpio_cfg[] = {
        {0x0005, 0x00}, // Enables LOCK output[7]: 0->disabled, 1->enabled; Enables ERRB output[7]: 0->disabled, 1->enabled;PU_LF1[1]: 0->: Line-fault Monitor 1 disabled, 1->enabled ;PU_LF1[0]
        /* map_fsync_trigger, MFP6, SYNC IN, RX */
        {0x02D0, 0x84}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable; 
        {0x02D1, 0xA0}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02D2, 0x02}, // GPIO_RX_ID[4:0]

        /* map_sync_out, MFP9, SYNC OUT, TX */
        {0x02D9, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable; 
        {0x02DA, 0x9C}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02DB, 0x40}, // GPIO_RX_ID[4:0]

        // map_uart_tx, #MFP11, RX 
        {0x02DF, 0x84}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable;
        {0x02E0, 0xA0}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02E1, 0x1B},

        // map_uart_rx, #MFP12, TX 
        {0x02E2, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable;
        {0x02E3, 0x7A}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02E4, 0x40}, // GPIO_RX_ID[4:0]

        /* map_pps, MFP7, pps, RX */
        {0x02D3, 0x84}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable; 
        {0x02D4, 0xA0}, // PULL_UPDN_SEL[7:6]: 0->7no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02D5, 0x1D}, // GPIO_RX_ID[4:0]

        /* map_wake, MFP3, wake, RX */
        {0x02C7, 0x84}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]，GPIO_OUT_DIS[0]:Disables GPIO output driver 0->enale,1->disable; 
        {0x02C8, 0xA0}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
        {0x02C9, 0x1E}, // GPIO_RX_ID[4:0]
    };
#if 1
    u8 link_pipe_datatype[] = {
        GMSL_CSI_DT_YUV422_8,
        GMSL_CSI_DT_YUV422_8,
        GMSL_CSI_DT_RAW_8,
        GMSL_CSI_DT_RAW_8,
        GMSL_CSI_DT_YUV422_8,
        GMSL_CSI_DT_YUV422_8,
    };
#endif

    if (state->dser_link&1) {
        link_map_pipe_opt[3].val = 0x78; // Enable line info, Enable CSI Port A&B, Pipeline X&Y&Z -> Port A ，U -> Port B
        link_map_pipe_opt[4].val = 0x87; // Pipeline X&Y&Z from Port A ，U from Port B
    }

    dev_info(state->dser_dev, "%s: start, dser_port=%d\n", __func__, state->dser_link);

    //mutex_lock(&priv->lock);
    if(state->dser_link&1){
        link_map_pipe_opt[3].val = 0x74;
        link_map_pipe_opt[4].val = 0x4B;
    }

    err |= max9295d_set_registers(state, link_map_pipe_opt, ARRAY_SIZE(link_map_pipe_opt));
    err |= max9295d_set_registers(state, gpio_cfg, ARRAY_SIZE(gpio_cfg));

#if 1
    for (i = 0; i < MAX9295D_MAX_PIPES; i++) {
        if(state->dser_link&1)
            err |= max9295d_set_pipe(state, i, link_pipe_datatype[i+2], GMSL_CSI_DT_EMBED, 3-i);
        else
            err |= max9295d_set_pipe(state, i, link_pipe_datatype[i], GMSL_CSI_DT_EMBED, i);	
    }
#endif
    //mutex_unlock(&priv->lock);
    dev_info(state->dser_dev, "%s: done, err=%d\n", __func__, err);

    return err;
}

int max9295d_set_orbbec_on(struct orb *state)
{
    int ret = 0;
    ret = max9295d_write_reg(state, ORBBEC_POWER_GPIOA_ADDR, 0x00);
    if (ret)
        dev_err(state->dser_dev, "%s:set_orbbec_on fail\n", __func__);
    
    return ret;
}

int max9295d_set_orbbec_off(struct orb *state)
{
    int ret = 0;
    ret = max9295d_write_reg(state, ORBBEC_POWER_GPIOA_ADDR, 0x10);
    if (ret)
        dev_err(state->dser_dev, "%s:set_orbbec_off fail\n", __func__);
    
    return ret;
}
