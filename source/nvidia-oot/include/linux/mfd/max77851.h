/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
/*
 * Defining registers address and its bit definitions of MAX77851.h
 */

#ifndef _MFD_MAX77851_H_
#define _MFD_MAX77851_H_

#include <linux/types.h>

/*
 * MAX77851 REGISTER INFORMATION
 */

#undef  __CONST_FFS
#define __CONST_FFS(_x) \
	((_x) & 0x00FF ?\
			((_x) & 0x000F ? ((_x) & 0x0003 ? ((_x) & 0x0001 ?  0 :  1) :\
									((_x) & 0x0004 ?  2 :  3)) :\
								((_x) & 0x0030 ? ((_x) & 0x0010 ?  4 :  5) :\
									((_x) & 0x0040 ?  6 :  7))) :\
			((_x) & 0x0F00 ? ((_x) & 0x0300 ? ((_x) & 0x0100 ?  8 :  9) :\
									((_x) & 0x0400 ? 10 : 11)) :\
								((_x) & 0x3000 ? ((_x) & 0x1000 ? 12 : 13) :\
									((_x) & 0x4000 ? 14 : 15))))

#undef  FFS
#define FFS(_x) ((_x) ? __CONST_FFS(_x) : 0)

#define BITS(_end, _start) ((BIT(_end) - BIT(_start)) + BIT(_end))
#define BIT_IS_ZERO (0x00)

#define BITS_VALUE(_v, _r) ((_v >> (u16)FFS(_r)) & (_r >> (u16)FFS(_r)))

#define BITS_REAL_VALUE(_v, _r) ((_v << (u16)FFS(_r)) & (_r))

/***********************  TOP REGISTERS & BIT & MASK  *************************/
#define TOP_ID_REG                                                 0x00
#define TOP_ID_ID                                                  BITS(7, 0)

#define TOP_MASK_REV_REG                                           0x1
#define TOP_MASK_REV_MASK_REV                                      BITS(7, 0)

#define TOP_OTP_REV_REG                                            0x2
#define TOP_OTP_REV_OTP_REV                                        BITS(7, 0)

#define TOP_INT0_REG                                               0x3
#define TOP_INT0_BUCK_I                                            BIT(0)
#define TOP_INT0_EN_I                                              BIT(1)
#define TOP_INT0_FPS_I                                             BIT(2)
#define TOP_INT0_GPIO_I                                            BIT(3)
#define TOP_INT0_IO_I                                              BIT(4)
#define TOP_INT0_LDO_I                                             BIT(5)
#define TOP_INT0_RLOGIC_I                                          BIT(6)
#define TOP_INT0_RTC_I                                             BIT(7)

#define TOP_INT1_REG                                               0x4
#define TOP_INT1_UVLO_I                                            BIT(0)
#define TOP_INT1_LB_I                                              BIT(1)
#define TOP_INT1_LB_ALM_I                                          BIT(2)
#define TOP_INT1_OVLO_I                                            BIT(3)
#define TOP_INT1_TJ_SHDN_I                                         BIT(4)
#define TOP_INT1_TJ_ALM1_I                                         BIT(5)
#define TOP_INT1_TJ_ALM2_I                                         BIT(6)
#define TOP_INT1_SMPL_I                                            BIT(7)

#define TOP_MSK0_REG                                               0x5
#define TOP_MSK0_BUCK_M                                            BIT(0)
#define TOP_MSK0_EN_M                                              BIT(1)
#define TOP_MSK0_FPS_M                                             BIT(2)
#define TOP_MSK0_GPIO_M                                            BIT(3)
#define TOP_MSK0_IO_M                                              BIT(4)
#define TOP_MSK0_LDO_M                                             BIT(5)
#define TOP_MSK0_RLOGIC_M                                          BIT(6)
#define TOP_MSK0_RTC_M                                             BIT(7)

#define TOP_MSK1_REG                                               0x6
#define TOP_MSK1_UVLO_M                                            BIT(0)
#define TOP_MSK1_LB_M                                              BIT(1)
#define TOP_MSK1_LB_ALM_M                                          BIT(2)
#define TOP_MSK1_OVLO_M                                            BIT(3)
#define TOP_MSK1_TJ_SHDN_M                                         BIT(4)
#define TOP_MSK1_TJ_ALM1_M                                         BIT(5)
#define TOP_MSK1_TJ_ALM2_M                                         BIT(6)
#define TOP_MSK1_SMPL_M                                            BIT(7)

#define TOP_STAT0_REG                                              0x7
#define TOP_STAT0_BUCK                                             BIT(0)
#define TOP_STAT0_EN                                               BIT(1)
#define TOP_STAT0_FPS                                              BIT(2)
#define TOP_STAT0_IRQ                                              BIT(3)
#define TOP_STAT0_VIO                                              BIT(4)
#define TOP_STAT0_LDO                                              BIT(5)
#define TOP_STAT0_RTC                                              BIT(7)

#define TOP_STAT1_REG                                              0x8
#define TOP_STAT1_UVLO                                             BIT(0)
#define TOP_STAT1_LB                                               BIT(1)
#define TOP_STAT1_LB_ALM                                           BIT(2)
#define TOP_STAT1_OVLO                                             BIT(3)
#define TOP_STAT1_TJ_SHDN                                          BIT(4)
#define TOP_STAT1_TJ_ALM1                                          BIT(5)
#define TOP_STAT1_TJ_ALM2                                          BIT(6)
#define TOP_STAT1_RC4M                                             BIT(7)

#define TOP_STAT2_REG                                              0x9
#define TOP_STAT2_UVLO_LOCK                                        BIT(0)
#define TOP_STAT2_LB_LOCK                                          BIT(1)
#define TOP_STAT2_LB_ALM_LOCK                                      BIT(2)
#define TOP_STAT2_OVLO_LOCK                                        BIT(3)
#define TOP_STAT2_TJ_SHDN_LOCK                                     BIT(4)
#define TOP_STAT2_SYS_WD_LOCK                                      BIT(5)
#define TOP_STAT2_CLOGIC_LOCK                                      BIT(6)

#define TOP_CFG0_REG                                               0xa
#define TOP_CFG0_UVLO_EN                                           BIT(0)
#define TOP_CFG0_LB_EN                                             BIT(1)
#define TOP_CFG0_LB_ALM_EN                                         BIT(2)
#define TOP_CFG0_OVLO_EN                                           BIT(3)
#define TOP_CFG0_TJ_EN                                             BIT(4)
#define TOP_CFG0_TJ_ALM_EN                                         BIT(5)
#define TOP_CFG0_CLK                                               BITS(7, 6)

#define TOP_CFG1_REG                                               0xb
#define TOP_CFG1_SMPL                                              BITS(1, 0)
#define TOP_CFG1_SMPL_EN                                           BIT(2)
#define TOP_CFG1_SMPL_SRC                                          BIT(3)
#define TOP_CFG1_LB_IFILTER                                        BITS(5, 4)
#define TOP_CFG1_LB_OFILTER                                        BITS(7, 6)

#define UVLO_CFG_REG                                               0xc
#define UVLO_CFG_UVLO_F                                            BITS(3, 0)
#define UVLO_CFG_UVLO_R                                            BITS(7, 4)

#define LB_CFG_REG                                                 0xd
#define LB_CFG_LB_F                                                BITS(3, 0)
#define LB_CFG_LB_R                                                BITS(7, 4)

#define LB_ALM_CFG_REG                                             0xe
#define LB_ALM_CFG_LB_ALM_F                                        BITS(3, 0)
#define LB_ALM_CFG_LB_ALM_R                                        BITS(7, 4)

#define OVLO_CFG_REG                                               0xf
#define OVLO_CFG_OVLO_F                                            BITS(1, 0)
#define OVLO_CFG_OVLO_R                                            BITS(3, 2)
#define OVLO_CFG_OVLO_IFILTER                                      BITS(5, 4)
#define OVLO_CFG_UVLO_IFILTER                                      BITS(7, 6)

#define TJ_SHDN_CFG_REG                                            0x10
#define TJ_SHDN_CFG_TJ_ALM1_R                                      BITS(1, 0)
#define TJ_SHDN_CFG_TJ_ALM2_R                                      BITS(3, 2)
#define TJ_SHDN_CFG_TJ_SHDN_R                                      BITS(5, 4)
#define TJ_SHDN_CFG_TJ_FM_EN                                       BIT(6)
#define TJ_SHDN_CFG_BAT_FM_EN                                      BIT(7)

#define SYS_WD_CFG_REG                                             0x11
#define SYS_WD_CFG_SYS_WD                                          BITS(1, 0)
#define SYS_WD_CFG_SYS_WD_EN                                       BIT(2)
#define SYS_WD_CFG_SYS_WD_SLPC                                     BIT(3)

#define SYS_WD_CLR_REG                                             0x12
#define SYS_WD_CLR_SYS_WD_C                                        BITS(7, 0)

/***********************  RLOGIC REGISTERS & BIT & MASK  **********************/
#define RLOGIC_INT0_REG                                            0x13
#define RLOGIC_INT0_RC65K_I                                        BIT(0)
#define RLOGIC_INT0_XTAL_I                                         BIT(1)
#define RLOGIC_INT0_VDD_ALTIN_I                                    BIT(2)

#define RLOGIC_INT1_REG                                            0x14
#define RLOGIC_INT1_SHDN_UVLO_I                                    BIT(0)
#define RLOGIC_INT1_SHDN_LB_I                                      BIT(1)
#define RLOGIC_INT1_SHDN_OVLO_I                                    BIT(2)
#define RLOGIC_INT1_SHDN_TJ_SHDN_I                                 BIT(3)
#define RLOGIC_INT1_SHDN_SYS_WD_I                                  BIT(4)
#define RLOGIC_INT1_SHDN_I2C_WD_I                                  BIT(5)
#define RLOGIC_INT1_SHDN_NRSTIO_I                                  BIT(6)
#define RLOGIC_INT1_SHDN_SHDN_I                                    BIT(7)

#define RLOGIC_INT2_REG                                            0x15
#define RLOGIC_INT2_SHDN_EN0_I                                     BIT(0)
#define RLOGIC_INT2_SHDN_EN1_I                                     BIT(1)
#define RLOGIC_INT2_SHDN_EN0_MR_I                                  BIT(2)
#define RLOGIC_INT2_SHDN_EN1_MR_I                                  BIT(3)
#define RLOGIC_INT2_SHDN_SW_COLD_RST_I                             BIT(4)
#define RLOGIC_INT2_SHDN_SW_OFF_I                                  BIT(5)
#define RLOGIC_INT2_SHDN_FPS_ABT_I                                 BIT(6)
#define RLOGIC_INT2_SHDN_GPIO_I                                    BIT(7)

#define RLOGIC_MSK0_REG                                            0x16
#define RLOGIC_MSK0_RC65K_M                                        BIT(0)
#define RLOGIC_MSK0_XTAL_M                                         BIT(1)
#define RLOGIC_MSK0_VDD_ALTIN_M                                    BIT(2)

#define RLOGIC_MSK1_REG                                            0x17
#define RLOGIC_MSK1_SHDN_UVLO_M                                    BIT(0)
#define RLOGIC_MSK1_SHDN_LB_M                                      BIT(1)
#define RLOGIC_MSK1_SHDN_OVLO_M                                    BIT(2)
#define RLOGIC_MSK1_SHDN_TJ_SHDN_M                                 BIT(3)
#define RLOGIC_MSK1_SHDN_SYS_WD_M                                  BIT(4)
#define RLOGIC_MSK1_SHDN_I2C_WD_M                                  BIT(5)
#define RLOGIC_MSK1_SHDN_NRSTIO_M                                  BIT(6)
#define RLOGIC_MSK1_SHDN_SHDN_M                                    BIT(7)

#define RLOGIC_MSK2_REG                                            0x18
#define RLOGIC_MSK2_SHDN_EN0_M                                     BIT(0)
#define RLOGIC_MSK2_SHDN_EN1_M                                     BIT(1)
#define RLOGIC_MSK2_SHDN_EN0_MR_M                                  BIT(2)
#define RLOGIC_MSK2_SHDN_EN1_MR_M                                  BIT(3)
#define RLOGIC_MSK2_SHDN_SW_COLD_RST_M                             BIT(4)
#define RLOGIC_MSK2_SHDN_SW_OFF_M                                  BIT(5)
#define RLOGIC_MSK2_SHDN_FPS_ABT_M                                 BIT(6)
#define RLOGIC_MSK2_SHDN_GPIO_M                                    BIT(7)

#define RLOGIC_STAT_REG                                            0x19
#define RLOGIC_STAT_RC65K                                          BIT(0)
#define RLOGIC_STAT_XTAL                                           BIT(1)
#define RLOGIC_STAT_VDD_ALTIN                                      BIT(2)
#define RLOGIC_STAT_CLK32K_LOCK                                    BIT(3)
#define RLOGIC_STAT_RLOGIC_LOCK                                    BIT(4)

#define RLOGIC_CFG_REG                                             0x1a
#define RLOGIC_CFG_VRTC_LPM_EN                                     BIT(0)
#define RLOGIC_CFG_VDD_LPM_EN                                      BIT(1)
#define RLOGIC_CFG_VDD_AD_EN                                       BIT(2)
#define RLOGIC_CFG_VDD_TON                                         BIT(3)
#define RLOGIC_CFG_VDD_ALTIN_EN                                    BIT(4)
#define RLOGIC_CFG_VDD_VOUT                                        BITS(6, 5)
#define RLOGIC_CFG_RTC_EN                                          BIT(7)

/***********************  ENABLE REGISTERS & BIT & MASK  **********************/
#define EN_INT_REG                                                 0x1b
#define EN_INT_EN0_FL_I                                            BIT(0)
#define EN_INT_EN0_RH_I                                            BIT(1)
#define EN_INT_EN0_1SEC_I                                          BIT(2)
#define EN_INT_EN0_MR_WRN_I                                        BIT(3)
#define EN_INT_EN1_FL_I                                            BIT(4)
#define EN_INT_EN1_RH_I                                            BIT(5)
#define EN_INT_EN1_1SEC_I                                          BIT(6)
#define EN_INT_EN1_MR_WRN_I                                        BIT(7)

#define EN_MSK_REG                                                 0x1c
#define EN_MSK_EN0_FL_M                                            BIT(0)
#define EN_MSK_EN0_RH_M                                            BIT(1)
#define EN_MSK_EN0_1SEC_M                                          BIT(2)
#define EN_MSK_EN0_MR_WRN_M                                        BIT(3)
#define EN_MSK_EN1_FL_M                                            BIT(4)
#define EN_MSK_EN1_RH_M                                            BIT(5)
#define EN_MSK_EN1_1SEC_M                                          BIT(6)
#define EN_MSK_EN1_MR_WRN_M                                        BIT(7)

#define EN_STAT_REG                                                0x1d
#define EN_STAT_EN0                                                BIT(0)
#define EN_STAT_EN0_LOCK                                           BIT(1)
#define EN_STAT_EN1                                                BIT(4)
#define EN_STAT_EN1_LOCK                                           BIT(5)

#define EN0_CFG0_REG                                               0x1e
#define EN0_CFG0_PD                                                BIT(0)
#define EN0_CFG0_PU                                                BIT(1)
#define EN0_CFG0_POL                                               BIT(2)
#define EN0_CFG0_DTC                                               BIT(3)
#define EN0_CFG0_IFILTER                                           BITS(6, 4)
#define EN0_CFG0_SUP                                               BIT(7)

#define EN0_CFG1_REG                                               0x1f
#define EN0_CFG1_MR                                                BITS(2, 0)
#define EN0_CFG1_MR_EN                                             BIT(3)
#define EN0_CFG1_MODE                                              BITS(6, 4)
#define EN0_CFG1_WAKE                                              BIT(7)

#define EN1_CFG0_REG                                               0x20
#define EN1_CFG0_PD                                                BIT(0)
#define EN1_CFG0_PU                                                BIT(1)
#define EN1_CFG0_POL                                               BIT(2)
#define EN1_CFG0_DTC                                               BIT(3)
#define EN1_CFG0_IFILTER                                           BITS(6, 4)
#define EN1_CFG0_SUP                                               BIT(7)

#define EN1_CFG1_REG                                               0x21
#define EN1_CFG1_MR                                                BITS(2, 0)
#define EN1_CFG1_MR_EN                                             BIT(3)
#define EN1_CFG1_MODE                                              BITS(6, 4)
#define EN1_CFG1_WAKE                                              BIT(7)

/***********************  CLK32K REGISTERS & BIT & MASK  **********************/
#define CLK32K_CFG_REG                                             0x22
#define CLK32K_CFG_CLK32K_EN                                       BITS(1, 0)
#define CLK32K_CFG_XTAL_OK_EN                                      BIT(2)
#define CLK32K_CFG_XTAL_LOAD                                       BITS(5, 3)
#define CLK32K_CFG_XTAL_LOAD_EN                                    BIT(6)
#define CLK32K_CFG_XTAL_WAIT_EN                                    BIT(7)

/***********************  I2C REGISTERS & BIT & MASK  *************************/
#define I2C_CFG0_REG                                               0x23
#define I2C_CFG0_HS_EXT_EN                                         BIT(0)
#define I2C_CFG0_I2C_WD_EN                                         BIT(1)
#define I2C_CFG0_I2C_GC_WARM_RST_EN                                BIT(2)
#define I2C_CFG0_PAIR_PMIC_EN                                      BIT(4)
#define I2C_CFG0_PAIR_RTC_EN                                       BIT(5)

#define I2C_CFG1_REG                                               0x24
#define I2C_CFG1_I2C_WD                                            BITS(1, 0)
#define I2C_CFG1_I2C_SUP                                           BIT(2)
#define I2C_CFG1_I2C_LOCK                                          BIT(3)

/***********************  IO REGISTERS & BIT & MASK ***************************/
#define IO_INT_REG                                                 0x25
#define IO_INT_NRSTIO_FL_I                                         BIT(0)
#define IO_INT_NRSTIO_RH_I                                         BIT(1)
#define IO_INT_SHDN_FL_I                                           BIT(2)
#define IO_INT_SHDN_RH_I                                           BIT(3)
#define IO_INT_VIO0_F_I                                            BIT(4)
#define IO_INT_VIO0_R_I                                            BIT(5)
#define IO_INT_VIO1_F_I                                            BIT(6)
#define IO_INT_VIO1_R_I                                            BIT(7)

#define IO_MSK_REG                                                 0x26
#define IO_MSK_NRSTIO_FL_M                                         BIT(0)
#define IO_MSK_NRSTIO_RH_M                                         BIT(1)
#define IO_MSK_SHDN_FL_M                                           BIT(2)
#define IO_MSK_SHDN_RH_M                                           BIT(3)
#define IO_MSK_VIO0_F_M                                            BIT(4)
#define IO_MSK_VIO0_R_M                                            BIT(5)
#define IO_MSK_VIO1_F_M                                            BIT(6)
#define IO_MSK_VIO1_R_M                                            BIT(7)

#define IO_STAT_REG                                                0x27
#define IO_STAT_NRSTIO                                             BIT(0)
#define IO_STAT_NRSTIO_LOCK                                        BIT(1)
#define IO_STAT_SHDN                                               BIT(2)
#define IO_STAT_SHDN_LOCK                                          BIT(3)
#define IO_STAT_VIO0                                               BIT(4)
#define IO_STAT_VIO1                                               BIT(6)

#define NRSTIO_CFG0_REG                                            0x28
#define NRSTIO_CFG0_PD                                             BIT(0)
#define NRSTIO_CFG0_PU                                             BIT(1)
#define NRSTIO_CFG0_POL                                            BIT(2)
#define NRSTIO_CFG0_DTCT                                           BIT(3)
#define NRSTIO_CFG0_IFILTER                                        BITS(6, 4)
#define NRSTIO_CFG0_SUP                                            BIT(7)

#define NRSTIO_CFG1_REG                                            0x29
#define NRSTIO_CFG1_MODE                                           BITS(2, 0)
#define NRSTIO_CFG1_DRV                                            BIT(4)
#define NRSTIO_CFG1_OFILTER                                        BIT(5)
#define NRSTIO_CFG1_CFG1_NRSTIO                                    BIT(6)

#define SHDN_CFG_REG                                               0x2a
#define SHDN_CFG_PD                                                BIT(0)
#define SHDN_CFG_PU                                                BIT(1)
#define SHDN_CFG_POL                                               BIT(2)
#define SHDN_CFG_DTCT                                              BIT(3)
#define SHDN_CFG_IFILTER                                           BITS(6, 4)
#define SHDN_CFG_SUP                                               BIT(7)

/***********************  GENERAL PURPOSE IO REGISTERS  **********************/
#define GPIO_INT0_REG                                              0x2b
#define GPIO_INT0_GPIO0_FL_I                                       BIT(0)
#define GPIO_INT0_GPIO1_FL_I                                       BIT(1)
#define GPIO_INT0_GPIO2_FL_I                                       BIT(2)
#define GPIO_INT0_GPIO3_FL_I                                       BIT(3)
#define GPIO_INT0_GPIO4_FL_I                                       BIT(4)
#define GPIO_INT0_GPIO5_FL_I                                       BIT(5)
#define GPIO_INT0_GPIO6_FL_I                                       BIT(6)
#define GPIO_INT0_GPIO7_FL_I                                       BIT(7)

#define GPIO_INT1_REG                                              0x2c
#define GPIO_INT1_GPIO0_RH_I                                       BIT(0)
#define GPIO_INT1_GPIO1_RH_I                                       BIT(1)
#define GPIO_INT1_GPIO2_RH_I                                       BIT(2)
#define GPIO_INT1_GPIO3_RH_I                                       BIT(3)
#define GPIO_INT1_GPIO4_RH_I                                       BIT(4)
#define GPIO_INT1_GPIO5_RH_I                                       BIT(5)
#define GPIO_INT1_GPIO6_RH_I                                       BIT(6)
#define GPIO_INT1_GPIO7_RH_I                                       BIT(7)

#define GPIO_MSK0_REG                                              0x2d
#define GPIO_MSK0_GPIO0_FL_M                                       BIT(0)
#define GPIO_MSK0_GPIO1_FL_M                                       BIT(1)
#define GPIO_MSK0_GPIO2_FL_M                                       BIT(2)
#define GPIO_MSK0_GPIO3_FL_M                                       BIT(3)
#define GPIO_MSK0_GPIO4_FL_M                                       BIT(4)
#define GPIO_MSK0_GPIO5_FL_M                                       BIT(5)
#define GPIO_MSK0_GPIO6_FL_M                                       BIT(6)
#define GPIO_MSK0_GPIO7_FL_M                                       BIT(7)

#define GPIO_MSK1_REG                                              0x2e
#define GPIO_MSK1_GPIO0_RH_M                                       BIT(0)
#define GPIO_MSK1_GPIO1_RH_M                                       BIT(1)
#define GPIO_MSK1_GPIO2_RH_M                                       BIT(2)
#define GPIO_MSK1_GPIO3_RH_M                                       BIT(3)
#define GPIO_MSK1_GPIO4_RH_M                                       BIT(4)
#define GPIO_MSK1_GPIO5_RH_M                                       BIT(5)
#define GPIO_MSK1_GPIO6_RH_M                                       BIT(6)
#define GPIO_MSK1_GPIO7_RH_M                                       BIT(7)

#define GPIO_STAT0_REG                                             0x2f
#define GPIO_STAT0_GPIO0                                           BIT(0)
#define GPIO_STAT0_GPIO1                                           BIT(1)
#define GPIO_STAT0_GPIO2                                           BIT(2)
#define GPIO_STAT0_GPIO3                                           BIT(3)
#define GPIO_STAT0_GPIO4                                           BIT(4)
#define GPIO_STAT0_GPIO5                                           BIT(5)
#define GPIO_STAT0_GPIO6                                           BIT(6)
#define GPIO_STAT0_GPIO7                                           BIT(7)

#define GPIO_STAT1_REG                                             0x30
#define GPIO_STAT1_GPIO0_LOCK                                      BIT(0)
#define GPIO_STAT1_GPIO1_LOCK                                      BIT(1)
#define GPIO_STAT1_GPIO2_LOCK                                      BIT(2)
#define GPIO_STAT1_GPIO3_LOCK                                      BIT(3)
#define GPIO_STAT1_GPIO4_LOCK                                      BIT(4)
#define GPIO_STAT1_GPIO5_LOCK                                      BIT(5)
#define GPIO_STAT1_GPIO6_LOCK                                      BIT(6)
#define GPIO_STAT1_GPIO7_LOCK                                      BIT(7)

/***********************  GPIO0 REGISTERS  ***********************************/
#define GPIO0_CFG0_REG                                             0x31
#define GPIO0_CFG1_REG                                             0x32
#define GPIO0_SRC0_REG                                             0x33
#define GPIO0_SRC1_REG                                             0x34
#define GPIO0_SRC2_REG                                             0x35
/***********************  GPIO1 REGISTERS  ***********************************/
#define GPIO1_CFG0_REG                                             0x36
#define GPIO1_CFG1_REG                                             0x37
#define GPIO1_SRC0_REG                                             0x38
#define GPIO1_SRC1_REG                                             0x39
#define GPIO1_SRC2_REG                                             0x3a
/***********************  GPIO2 REGISTERS  ***********************************/
#define GPIO2_CFG0_REG                                             0x3b
#define GPIO2_CFG1_REG                                             0x3c
#define GPIO2_SRC0_REG                                             0x3d
#define GPIO2_SRC1_REG                                             0x3e
#define GPIO2_SRC2_REG                                             0x3f
/***********************  GPIO3 REGISTERS  ***********************************/
#define GPIO3_CFG0_REG                                             0x40
#define GPIO3_CFG1_REG                                             0x41
#define GPIO3_SRC0_REG                                             0x42
#define GPIO3_SRC1_REG                                             0x43
#define GPIO3_SRC2_REG                                             0x44
/***********************  GPIO4 REGISTERS  ***********************************/
#define GPIO4_CFG0_REG                                             0x45
#define GPIO4_CFG1_REG                                             0x46
#define GPIO4_SRC0_REG                                             0x47
#define GPIO4_SRC1_REG                                             0x48
#define GPIO4_SRC2_REG                                             0x49
/***********************  GPIO5 REGISTERS  ***********************************/
#define GPIO5_CFG0_REG                                             0x4a
#define GPIO5_CFG1_REG                                             0x4b
#define GPIO5_SRC0_REG                                             0x4c
#define GPIO5_SRC1_REG                                             0x4d
#define GPIO5_SRC2_REG                                             0x4e
/***********************  GPIO6 REGISTERS  ***********************************/
#define GPIO6_CFG0_REG                                             0x4f
#define GPIO6_CFG1_REG                                             0x50
#define GPIO6_SRC0_REG                                             0x51
#define GPIO6_SRC1_REG                                             0x52
#define GPIO6_SRC2_REG                                             0x53
/***********************  GPIO7 REGISTERS  ***********************************/
#define GPIO7_CFG0_REG                                             0x54
#define GPIO7_CFG1_REG                                             0x55
#define GPIO7_SRC0_REG                                             0x56
#define GPIO7_SRC1_REG                                             0x57
#define GPIO7_SRC2_REG                                             0x58

/***********************  GPIO BIT & MASK ***********************************/
#define GPIO_CFG0_PD                                               BIT(0)
#define GPIO_CFG0_PU                                               BIT(1)
#define GPIO_CFG0_POL                                              BIT(2)
#define GPIO_CFG0_DTCT                                             BIT(3)
#define GPIO_CFG0_IFILTER                                          BITS(6, 4)
#define GPIO_CFG0_SUP                                              BIT(7)

#define GPIO_CFG1_MODE                                             BITS(3, 0)
#define GPIO_CFG1_DRV                                              BIT(4)
#define GPIO_CFG1_OFILTER                                          BIT(5)
#define GPIO_CFG1_OUTPUT                                           BIT(6)
#define GPIO_CFG1_POK                                              BIT(7)

#define GPIO_SRC0_GPIO0                                            BIT(0)
#define GPIO_SRC0_GPIO1                                            BIT(1)
#define GPIO_SRC0_GPIO2                                            BIT(2)
#define GPIO_SRC0_GPIO3                                            BIT(3)
#define GPIO_SRC0_GPIO4                                            BIT(4)
#define GPIO_SRC0_GPIO5                                            BIT(5)
#define GPIO_SRC0_GPIO6                                            BIT(6)

#define GPIO_SRC1_LDO0                                             BIT(0)
#define GPIO_SRC1_LDO1                                             BIT(1)
#define GPIO_SRC1_LDO2                                             BIT(2)
#define GPIO_SRC1_LDO3                                             BIT(3)
#define GPIO_SRC1_LDO4                                             BIT(4)
#define GPIO_SRC1_LDO5                                             BIT(5)
#define GPIO_SRC1_LDO6                                             BIT(6)
#define GPIO_SRC1_SRC_EN                                           BIT(7)

#define GPIO_SRC2_BUCK0                                            BIT(0)
#define GPIO_SRC2_BUCK1                                            BIT(1)
#define GPIO_SRC2_BUCK2                                            BIT(2)
#define GPIO_SRC2_BUCK3                                            BIT(3)
#define GPIO_SRC2_BUCK4                                            BIT(4)
#define GPIO_SRC2_BAT                                              BIT(5)
#define GPIO_SRC2_TJ                                               BIT(6)
#define GPIO_SRC2_SID                                              BIT(7)

/***********************  FPSO0 REGISTERS  ***********************************/
#define FPSO0_CFG_REG                                              0x59

/***********************  FPSO1 REGISTERS  ***********************************/
#define FPSO1_CFG_REG                                              0x5a

/***********************  FPSO2 REGISTERS  ***********************************/
#define FPSO2_CFG_REG                                              0x5b

/***********************  FPSO3 REGISTERS  ***********************************/
#define FPSO3_CFG_REG                                               0x5c

#define FPSO_CFG_PD                                                BIT(0)
#define FPSO_CFG_PU                                                BIT(1)
#define FPSO_CFG_MODE                                              BITS(3, 2)
#define FPSO_CFG_DRV                                               BIT(4)
#define FPSO_CFG_OFILTER                                           BIT(5)
#define FPSO_CFG_OUTPUT                                            BIT(6)
#define FPSO_CFG_POL                                               BIT(7)

/***********************  FLEXIBLE POWER SEQUENCER REGISTERS  ****************/
#define FPS_INT0_REG                                               0x5d
#define FPS_INT0_OFF_UVLO_I                                        BIT(0)
#define FPS_INT0_OFF_LB_I                                          BIT(1)
#define FPS_INT0_OFF_OVLO_I                                        BIT(2)
#define FPS_INT0_OFF_TJ_SHDN_I                                     BIT(3)
#define FPS_INT0_OFF_SYS_WD_I                                      BIT(4)
#define FPS_INT0_OFF_I2C_WD_I                                      BIT(5)
#define FPS_INT0_OFF_NRSTIO_I                                      BIT(6)
#define FPS_INT0_OFF_SHDN_I                                        BIT(7)

#define FPS_INT1_REG                                               0x5e
#define FPS_INT1_OFF_EN0_I                                         BIT(0)
#define FPS_INT1_OFF_EN1_I                                         BIT(1)
#define FPS_INT1_OFF_EN0_MR_I                                      BIT(2)
#define FPS_INT1_OFF_EN1_MR_I                                      BIT(3)
#define FPS_INT1_OFF_SW_COLD_RST_I                                 BIT(4)
#define FPS_INT1_OFF_SW_OFF_I                                      BIT(5)
#define FPS_INT1_OFF_FPS_ABT_I                                     BIT(6)
#define FPS_INT1_OFF_GPIO_I                                        BIT(7)

#define FPS_MSK0_REG                                               0x5f
#define FPS_MSK0_OFF_UVLO_M                                        BIT(0)
#define FPS_MSK0_OFF_LB_M                                          BIT(1)
#define FPS_MSK0_OFF_OVLO_M                                        BIT(2)
#define FPS_MSK0_OFF_TJ_SHDN_M                                     BIT(3)
#define FPS_MSK0_OFF_SYS_WD_M                                      BIT(4)
#define FPS_MSK0_OFF_I2C_WD_M                                      BIT(5)
#define FPS_MSK0_OFF_NRSTIO_M                                      BIT(6)
#define FPS_MSK0_OFF_SHDN_M                                        BIT(7)

#define FPS_MSK1_REG                                               0x60
#define FPS_MSK1_OFF_EN0_M                                         BIT(0)
#define FPS_MSK1_OFF_EN1_M                                         BIT(1)
#define FPS_MSK1_OFF_EN0_MR_M                                      BIT(2)
#define FPS_MSK1_OFF_EN1_MR_M                                      BIT(3)
#define FPS_MSK1_OFF_SW_COLD_RST_M                                 BIT(4)
#define FPS_MSK1_OFF_SW_OFF_M                                      BIT(5)
#define FPS_MSK1_OFF_FPS_ABT_M                                     BIT(6)
#define FPS_MSK1_OFF_GPIO_M                                        BIT(7)

#define FPS_STAT0_REG                                              0x61
#define FPS_STAT0_FPS_STATE                                        BITS(4, 0)
#define FPS_STAT0_FPS_STATUS                                       BITS(7, 5)

#define FPS_STAT1_REG                                              0x62
#define FPS_STAT1_FPS_M0_LOCK                                      BIT(0)
#define FPS_STAT1_FPS_M1_LOCK                                      BIT(1)
#define FPS_STAT1_FPS_M2_LOCK                                      BIT(2)
#define FPS_STAT1_FPS_M3_LOCK                                      BIT(3)
#define FPS_STAT1_FPS_LOCK                                         BIT(4)
#define FPS_STAT1_FPS_GPIO_LOCK                                    BIT(5)
#define FPS_STAT1_FPS_LDO_LOCK                                     BIT(6)
#define FPS_STAT1_FPS_BUCK_LOCK                                    BIT(7)

#define FPS_SW_REG                                                 0x63
#define FPS_SW_SLP                                             	  BIT(0)
#define FPS_SW_GLB_ULPM                                           BIT(1)
#define FPS_SW_GLB_LPM                                            BIT(2)
#define FPS_SW_COLD_RST                                           BIT(4)
#define FPS_SW_OFF                                                BIT(5)
#define FPS_SW_WARM_RST                                           BIT(6)
#define FPS_SW_ON                                                 BIT(7)

#define FPS_SRC_CFG0_REG                                           0x64
#define FPS_SRC_CFG0_SRC_UVLO                                      BIT(0)
#define FPS_SRC_CFG0_SRC_LB                                        BIT(1)
#define FPS_SRC_CFG0_SRC_OVLO                                      BIT(2)
#define FPS_SRC_CFG0_SRC_TJ_SHDN                                   BIT(3)
#define FPS_SRC_CFG0_SRC_SYS_WD                                    BIT(4)
#define FPS_SRC_CFG0_SRC_I2C_WD                                    BIT(5)
#define FPS_SRC_CFG0_SRC_NRSTIO                                    BIT(6)
#define FPS_SRC_CFG0_SRC_SHDN                                      BIT(7)

#define FPS_SRC_CFG1_REG                                           0x65
#define FPS_SRC_CFG1_SRC_EN0                                       BIT(0)
#define FPS_SRC_CFG1_SRC_EN1                                       BIT(1)
#define FPS_SRC_CFG1_SRC_EN0_MR                                    BIT(2)
#define FPS_SRC_CFG1_SRC_EN1_MR                                    BIT(3)
#define FPS_SRC_CFG1_SRC_SW_COLD_RST                               BIT(4)
#define FPS_SRC_CFG1_SRC_SW_OFF                                    BIT(5)
#define FPS_SRC_CFG1_SRC_FPS_ABT                                   BIT(6)
#define FPS_SRC_CFG1_SRC_GPIO                                      BIT(7)

#define FPS_SHDN_CFG0_REG                                          0x66
#define FPS_SHDN_CFG0_SHDN_UVLO                                    BIT(0)
#define FPS_SHDN_CFG0_SHDN_LB                                      BIT(1)
#define FPS_SHDN_CFG0_SHDN_OVLO                                    BIT(2)
#define FPS_SHDN_CFG0_SHDN_TJ_SHDN                                 BIT(3)
#define FPS_SHDN_CFG0_SHDN_SYS_WD                                  BIT(4)
#define FPS_SHDN_CFG0_SHDN_I2C_WD                                  BIT(5)
#define FPS_SHDN_CFG0_SHDN_NRSTIO                                  BIT(6)
#define FPS_SHDN_CFG0_SHDN_SHDN                                    BIT(7)

#define FPS_SHDN_CFG1_REG                                          0x67
#define FPS_SHDN_CFG1_SHDN_EN0                                     BIT(0)
#define FPS_SHDN_CFG1_SHDN_EN1                                     BIT(1)
#define FPS_SHDN_CFG1_SHDN_EN0_MR                                  BIT(2)
#define FPS_SHDN_CFG1_SHDN_EN1_MR                                  BIT(3)
#define FPS_SHDN_CFG1_SHDN_SW_COLD_RST                             BIT(4)
#define FPS_SHDN_CFG1_SHDN_SW_OFF                                  BIT(5)
#define FPS_SHDN_CFG1_SHDN_FPS_ABT                                 BIT(6)
#define FPS_SHDN_CFG1_SHDN_GPIO                                    BIT(7)

#define FPS_IMM_CFG0_REG                                           0x68
#define FPS_IMM_CFG0_IMM_UVLO                                      BIT(0)
#define FPS_IMM_CFG0_IMM_LB                                        BIT(1)
#define FPS_IMM_CFG0_IMM_OVLO                                      BIT(2)
#define FPS_IMM_CFG0_IMM_TJ_SHDN                                   BIT(3)
#define FPS_IMM_CFG0_IMM_SYS_WD                                    BIT(4)
#define FPS_IMM_CFG0_IMM_I2C_WD                                    BIT(5)
#define FPS_IMM_CFG0_IMM_NRSTIO                                    BIT(6)
#define FPS_IMM_CFG0_IMM_SHDN                                      BIT(7)

#define FPS_IMM_CFG1_REG                                           0x69
#define FPS_IMM_CFG1_IMM_EN0                                       BIT(0)
#define FPS_IMM_CFG1_IMM_EN1                                       BIT(1)
#define FPS_IMM_CFG1_IMM_EN0_MR                                    BIT(2)
#define FPS_IMM_CFG1_IMM_EN1_MR                                    BIT(3)
#define FPS_IMM_CFG1_IMM_SW_COLD_RST                               BIT(4)
#define FPS_IMM_CFG1_IMM_SW_OFF                                    BIT(5)
#define FPS_IMM_CFG1_IMM_FPS_ABT                                   BIT(6)
#define FPS_IMM_CFG1_IMM_GPIO                                      BIT(7)

#define FPS_RSTRT_CFG0_REG                                         0x6a
#define FPS_RSTRT_CFG0_RSTRT_UVLO                                  BIT(0)
#define FPS_RSTRT_CFG0_RSTRT_LB                                    BIT(1)
#define FPS_RSTRT_CFG0_RSTRT_OVLO                                  BIT(2)
#define FPS_RSTRT_CFG0_RSTRT_TJ_SHDN                               BIT(3)
#define FPS_RSTRT_CFG0_RSTRT_SYS_WD                                BIT(4)
#define FPS_RSTRT_CFG0_RSTRT_I2C_WD                                BIT(5)
#define FPS_RSTRT_CFG0_RSTRT_NRSTIO                                BIT(6)
#define FPS_RSTRT_CFG0_RSTRT_SHDN                                  BIT(7)

#define FPS_RSTRT_CFG1_REG                                         0x6b
#define FPS_RSTRT_CFG1_RSTRT_EN0                                   BIT(0)
#define FPS_RSTRT_CFG1_RSTRT_EN1                                   BIT(1)
#define FPS_RSTRT_CFG1_RSTRT_EN0_MR                                BIT(2)
#define FPS_RSTRT_CFG1_RSTRT_EN1_MR                                BIT(3)
#define FPS_RSTRT_CFG1_RSTRT_SW_COLD_RST                           BIT(4)
#define FPS_RSTRT_CFG1_RSTRT_SW_OFF                                BIT(5)
#define FPS_RSTRT_CFG1_RSTRT_FPS_ABT                               BIT(6)
#define FPS_RSTRT_CFG1_RSTRT_GPIO                                  BIT(7)

#define FPS_CFG_REG                                                0x6c
#define FPS_CFG_MX_RW                                              BIT(0)
#define FPS_CFG_GPIOX_RW                                           BIT(1)
#define FPS_CFG_FPSOX_RW                                           BIT(2)
#define FPS_CFG_OFF_RST                                            BIT(3)
#define FPS_CFG_RSTRT_MAX                                          BIT(4)
#define FPS_CFG_RSTRT_WAIT                                         BITS(6, 5)
#define FPS_CFG_GLB_ABT                                            BIT(7)

#define FPS_IM_CFG0_REG                                            0x6d
#define FPS_IM_CFG0_PD_T                                           BITS(3, 0)
#define FPS_IM_CFG0_PU_T                                           BITS(7, 4)

#define FPS_IM_CFG1_REG                                            0x6e
#define FPS_IM_CFG1_SLPY_T                                         BITS(3, 0)
#define FPS_IM_CFG1_SLPX_T                                         BITS(7, 4)

#define FPS_M02_CFG0_REG                                           0x6f
#define FPS_M02_CFG1_REG                                           0x70
#define FPS_M02_CFG2_REG                                           0x71
#define FPS_M02_CFG3_REG                                           0x72
#define FPS_M02_CFG4_REG                                           0x73
#define FPS_M13_CFG0_REG                                           0x74
#define FPS_M13_CFG1_REG                                           0x75
#define FPS_M13_CFG2_REG                                           0x76
#define FPS_M13_CFG3_REG                                           0x77
#define FPS_M13_CFG4_REG                                           0x78

/***********************  FPS MASTER CONFIGURATION BIT & MASK  ***************/
#define FPS_CFG0_PD                                                BITS(1, 0)
#define FPS_CFG0_PU                                                BITS(5, 4)
#define FPS_CFG0_EN                                                BIT(6)
#define FPS_CFG0_ABT_EN                                            BIT(7)

#define FPS_M13_CFG1_REG                                           0x75
#define FPS_CFG1_SLPY                                              BITS(1, 0)
#define FPS_CFG1_SLP_EN                                            BITS(3, 2)
#define FPS_CFG1_SLPX                                              BITS(5, 4)
#define FPS_CFG1_ABT                                               BIT(7)

#define FPS_M13_CFG2_REG                                           0x76
#define FPS_CFG2_PD_T                                              BITS(3, 0)
#define FPS_CFG2_PU_T                                              BITS(7, 4)

#define FPS_M13_CFG3_REG                                           0x77
#define FPS_CFG3_SLPY_T                                            BITS(3, 0)
#define FPS_CFG3_SLPX_T                                            BITS(7, 4)

#define FPS_M13_CFG4_REG                                           0x78
#define FPS_CFG4_PD_MAX                                            BITS(1, 0)
#define FPS_CFG4_SLPY_MAX                                          BITS(3, 2)
#define FPS_CFG4_PU_MAX                                            BITS(5, 4)
#define FPS_CFG4_SLPX_MAX                                          BITS(7, 6)

/***********************  FLEXIBLE POWER SEQUENCER GPIO REGISTERS  ***********/
#define FPS_GPIO_ONOFF_CFG_REG                                     0x79
#define FPS_GPIO_ONOFF_CFG_GPIO0                                   BIT(0)
#define FPS_GPIO_ONOFF_CFG_GPIO1                                   BIT(1)
#define FPS_GPIO_ONOFF_CFG_GPIO2                                   BIT(2)
#define FPS_GPIO_ONOFF_CFG_GPIO3                                   BIT(3)
#define FPS_GPIO_ONOFF_CFG_GPIO4                                   BIT(4)
#define FPS_GPIO_ONOFF_CFG_GPIO5                                   BIT(5)
#define FPS_GPIO_ONOFF_CFG_GPIO6                                   BIT(6)
#define FPS_GPIO_ONOFF_CFG_GPIO7                                   BIT(7)

#define FPS_GPIO_SLP_CFG_REG                                       0x7a
#define FPS_GPIO_SLP_CFG_GPIO0                                     BIT(0)
#define FPS_GPIO_SLP_CFG_GPIO1                                     BIT(1)
#define FPS_GPIO_SLP_CFG_GPIO2                                     BIT(2)
#define FPS_GPIO_SLP_CFG_GPIO3                                     BIT(3)
#define FPS_GPIO_SLP_CFG_GPIO4                                     BIT(4)
#define FPS_GPIO_SLP_CFG_GPIO5                                     BIT(5)
#define FPS_GPIO_SLP_CFG_GPIO6                                     BIT(6)
#define FPS_GPIO_SLP_CFG_GPIO7                                     BIT(7)

#define FPS_GPIO_ULPM_CFG_REG                                      0x7b
#define FPS_GPIO_ULPM_CFG_GPIO0                                    BIT(0)
#define FPS_GPIO_ULPM_CFG_GPIO1                                    BIT(1)
#define FPS_GPIO_ULPM_CFG_GPIO2                                    BIT(2)
#define FPS_GPIO_ULPM_CFG_GPIO3                                    BIT(3)
#define FPS_GPIO_ULPM_CFG_GPIO4                                    BIT(4)
#define FPS_GPIO_ULPM_CFG_GPIO5                                    BIT(5)
#define FPS_GPIO_ULPM_CFG_GPIO6                                    BIT(6)
#define FPS_GPIO_ULPM_CFG_GPIO7                                    BIT(7)

#define FPS_GPIO04_CFG0_REG                                        0x7c
#define FPS_GPIO04_CFG1_REG                                        0x7d
#define FPS_GPIO04_CFG2_REG                                        0x7e
#define FPS_GPIO15_CFG0_REG                                        0x7f
#define FPS_GPIO15_CFG1_REG                                        0x80
#define FPS_GPIO15_CFG2_REG                                        0x81
#define FPS_GPIO26_CFG0_REG                                        0x82
#define FPS_GPIO26_CFG1_REG                                        0x83
#define FPS_GPIO26_CFG2_REG                                        0x84
#define FPS_GPIO37_CFG0_REG                                        0x85
#define FPS_GPIO37_CFG1_REG                                        0x86
#define FPS_GPIO37_CFG2_REG                                        0x87

/***********************  FLEXIBLE POWER SEQUENCER FPSO REGISTERS  ***********/
#define FPS_FPSO02_CFG0_REG                                        0x88
#define FPS_FPSO02_CFG1_REG                                        0x89
#define FPS_FPSO02_CFG2_REG                                        0x8a
#define FPS_FPSO13_CFG0_REG                                        0x8b
#define FPS_FPSO13_CFG1_REG                                        0x8c
#define FPS_FPSO13_CFG2_REG                                        0x8d

/***********************  FLEXIBLE POWER SEQUENCER NRSTIO REGISTERS  *********/
#define FPS_NRSTIO_CFG0_REG                                        0x8e
#define FPS_NRSTIO_CFG1_REG                                        0x8f
#define FPS_NRSTIO_CFG2_REG                                        0x90

/***********************  FLEXIBLE POWER SEQUENCER LDO REGISTERS  ************/
#define FPS_LDO0_CFG0_REG                                          0x91
#define FPS_LDO0_CFG1_REG                                          0x92
#define FPS_LDO0_CFG2_REG                                          0x93
#define FPS_LDO1_CFG0_REG                                          0x94
#define FPS_LDO1_CFG1_REG                                          0x95
#define FPS_LDO1_CFG2_REG                                          0x96
#define FPS_LDO2_CFG0_REG                                          0x97
#define FPS_LDO2_CFG1_REG                                          0x98
#define FPS_LDO2_CFG2_REG                                          0x99
#define FPS_LDO3_CFG0_REG                                          0x9a
#define FPS_LDO3_CFG1_REG                                          0x9b
#define FPS_LDO3_CFG2_REG                                          0x9c
#define FPS_LDO4_CFG0_REG                                          0x9d
#define FPS_LDO4_CFG1_REG                                          0x9e
#define FPS_LDO4_CFG2_REG                                          0x9f
#define FPS_LDO5_CFG0_REG                                          0xa0
#define FPS_LDO5_CFG1_REG                                          0xa1
#define FPS_LDO5_CFG2_REG                                          0xa2
#define FPS_LDO6_CFG0_REG                                          0xa3
#define FPS_LDO6_CFG1_REG                                          0xa4
#define FPS_LDO6_CFG2_REG                                          0xa5

/***********************  FLEXIBLE POWER SEQUENCER BUCK REGISTERS  ***********/
#define FPS_BUCK0_CFG0_REG                                         0xa6
#define FPS_BUCK0_CFG1_REG                                         0xa7
#define FPS_BUCK0_CFG2_REG                                         0xa8
#define FPS_BUCK1_CFG0_REG                                         0xa9
#define FPS_BUCK1_CFG1_REG                                         0xaa
#define FPS_BUCK1_CFG2_REG                                         0xab
#define FPS_BUCK2_CFG0_REG                                         0xac
#define FPS_BUCK2_CFG1_REG                                         0xad
#define FPS_BUCK2_CFG2_REG                                         0xae
#define FPS_BUCK3_CFG0_REG                                         0xaf
#define FPS_BUCK3_CFG1_REG                                         0xb0
#define FPS_BUCK3_CFG2_REG                                         0xb1
#define FPS_BUCK4_CFG0_REG                                         0xb2
#define FPS_BUCK4_CFG1_REG                                         0xb3
#define FPS_BUCK4_CFG2_REG                                         0xb4

/***********************  FLEXIBLE POWER SEQUENCER GPIO BIT & MASK  **********/
#define FPS_CFG0_MD0                                               BIT(0)
#define FPS_CFG0_MD1                                               BIT(1)
#define FPS_CFG0_MD2                                               BIT(2)
#define FPS_CFG0_MD3                                               BIT(3)
#define FPS_CFG0_MU0                                               BIT(4)
#define FPS_CFG0_MU1                                               BIT(5)
#define FPS_CFG0_MU2                                               BIT(6)
#define FPS_CFG0_MU3                                               BIT(7)
#define FPS_CFG1_PD                                                BITS(3, 0)
#define FPS_CFG1_PU                                                BITS(7, 4)
#define FPS_CFG2_SLPY                                              BITS(3, 0)
#define FPS_CFG2_SLPX                                              BITS(7, 4)

/***********************  LDO REGISTERS  *************************************/
#define LDO_INT0_REG                                               0xb5
#define LDO_INT0_LDO0_POK_I                                        BIT(0)
#define LDO_INT0_LDO1_POK_I                                        BIT(1)
#define LDO_INT0_LDO2_POK_I                                        BIT(2)
#define LDO_INT0_LDO3_POK_I                                        BIT(3)
#define LDO_INT0_LDO4_POK_I                                        BIT(4)
#define LDO_INT0_LDO5_POK_I                                        BIT(5)
#define LDO_INT0_LDO6_POK_I                                        BIT(6)

#define LDO_INT1_REG                                               0xb6
#define LDO_INT1_LDO0_VOK_I                                        BIT(0)
#define LDO_INT1_LDO1_VOK_I                                        BIT(1)
#define LDO_INT1_LDO2_VOK_I                                        BIT(2)
#define LDO_INT1_LDO3_VOK_I                                        BIT(3)
#define LDO_INT1_LDO4_VOK_I                                        BIT(4)
#define LDO_INT1_LDO5_VOK_I                                        BIT(5)
#define LDO_INT1_LDO6_VOK_I                                        BIT(6)

#define LDO_MSK0_REG                                               0xb7
#define LDO_MSK0_LDO0_POK_M                                        BIT(0)
#define LDO_MSK0_LDO1_POK_M                                        BIT(1)
#define LDO_MSK0_LDO2_POK_M                                        BIT(2)
#define LDO_MSK0_LDO3_POK_M                                        BIT(3)
#define LDO_MSK0_LDO4_POK_M                                        BIT(4)
#define LDO_MSK0_LDO5_POK_M                                        BIT(5)
#define LDO_MSK0_LDO6_POK_M                                        BIT(6)

#define LDO_MSK1_REG                                               0xb8
#define LDO_MSK1_LDO0_VOK_M                                        BIT(0)
#define LDO_MSK1_LDO1_VOK_M                                        BIT(1)
#define LDO_MSK1_LDO2_VOK_M                                        BIT(2)
#define LDO_MSK1_LDO3_VOK_M                                        BIT(3)
#define LDO_MSK1_LDO4_VOK_M                                        BIT(4)
#define LDO_MSK1_LDO5_VOK_M                                        BIT(5)
#define LDO_MSK1_LDO6_VOK_M                                        BIT(6)

#define LDO_STAT0_REG                                              0xb9
#define LDO_STAT0_LDO0_POK                                         BIT(0)
#define LDO_STAT0_LDO1_POK                                         BIT(1)
#define LDO_STAT0_LDO2_POK                                         BIT(2)
#define LDO_STAT0_LDO3_POK                                         BIT(3)
#define LDO_STAT0_LDO4_POK                                         BIT(4)
#define LDO_STAT0_LDO5_POK                                         BIT(5)
#define LDO_STAT0_LDO6_POK                                         BIT(6)

#define LDO_STAT1_REG                                              0xba
#define LDO_STAT1_LDO0_VOK                                         BIT(0)
#define LDO_STAT1_LDO1_VOK                                         BIT(1)
#define LDO_STAT1_LDO2_VOK                                         BIT(2)
#define LDO_STAT1_LDO3_VOK                                         BIT(3)
#define LDO_STAT1_LDO4_VOK                                         BIT(4)
#define LDO_STAT1_LDO5_VOK                                         BIT(5)
#define LDO_STAT1_LDO6_VOK                                         BIT(6)

#define LDO_STAT2_REG                                              0xbb
#define LDO_STAT2_LDO0_LOCK                                        BIT(0)
#define LDO_STAT2_LDO1_LOCK                                        BIT(1)
#define LDO_STAT2_LDO2_LOCK                                        BIT(2)
#define LDO_STAT2_LDO3_LOCK                                        BIT(3)
#define LDO_STAT2_LDO4_LOCK                                        BIT(4)
#define LDO_STAT2_LDO5_LOCK                                        BIT(5)
#define LDO_STAT2_LDO6_LOCK                                        BIT(6)

/***********************  LDO0 REGISTERS  ************************************/
#define LDO0_CFG0_REG                                              0xbc
#define LDO0_CFG1_REG                                              0xbd
/***********************  LDO1 REGISTERS  ************************************/
#define LDO1_CFG0_REG                                              0xbe
#define LDO1_CFG1_REG                                              0xbf
/***********************  LDO2 REGISTERS  ************************************/
#define LDO2_CFG0_REG                                              0xc0
#define LDO2_CFG1_REG                                              0xc1
/***********************  LDO3 REGISTERS  ************************************/
#define LDO3_CFG0_REG                                              0xc2
#define LDO3_CFG1_REG                                              0xc3
/***********************  LDO4 REGISTERS  ************************************/
#define LDO4_CFG0_REG                                              0xc4
#define LDO4_CFG1_REG                                              0xc5
/***********************  LDO5 REGISTERS  ************************************/
#define LDO5_CFG0_REG                                              0xc6
#define LDO5_CFG1_REG                                              0xc7
/***********************  LDO6 REGISTERS  ************************************/
#define LDO6_CFG0_REG                                              0xc8
#define LDO6_CFG1_REG                                              0xc9

#define LDO_CFG0_EN                                                BIT(0)
#define LDO_CFG0_LPM_EN                                            BIT(1)
#define LDO_CFG0_LDSW_EN                                           BIT(2)
#define LDO_CFG0_SR                                                BIT(3)
#define LDO_CFG0_SR_EN                                             BIT(4)
#define LDO_CFG0_ADE                                               BIT(5)
#define LDO_CFG0_VOUT_RNG                                          BIT(6)
#define LDO_CFG0_VOUT_RW                                           BIT(7)

#define LDO_CFG1_VOUT                                              BITS(7, 0)
#define LDO_CFG2_VOUT                                              BITS(7, 0)

/***********************  BUCK REGISTERS  ************************************/
#define BUCK_INT0_REG                                              0xca
#define BUCK_INT0_BUCK0_POK_I                                      BIT(0)
#define BUCK_INT0_BUCK1_POK_I                                      BIT(1)
#define BUCK_INT0_BUCK2_POK_I                                      BIT(2)
#define BUCK_INT0_BUCK3_POK_I                                      BIT(3)
#define BUCK_INT0_BUCK4_POK_I                                      BIT(4)
#define BUCK_INT0_VL0_I                                            BIT(5)
#define BUCK_INT0_VL1_I                                            BIT(6)
#define BUCK_INT0_VL2_I                                            BIT(7)

#define BUCK_INT1_REG                                              0xcb
#define BUCK_INT1_BUCK0_SC_I                                       BIT(0)
#define BUCK_INT1_BUCK1_SC_I                                       BIT(1)
#define BUCK_INT1_BUCK2_SC_I                                       BIT(2)
#define BUCK_INT1_BUCK3_SC_I                                       BIT(3)
#define BUCK_INT1_BUCK4_SC_I                                       BIT(4)
#define BUCK_INT1_VL0_ALTIN_I                                      BIT(5)
#define BUCK_INT1_VL1_ALTIN_I                                      BIT(6)
#define BUCK_INT1_VL2_ALTIN_I                                      BIT(7)

#define BUCK_INT2_REG                                              0xcc
#define BUCK_INT2_BUCK0_CLK_EXT_I                                  BIT(0)
#define BUCK_INT2_BUCK1_CLK_EXT_I                                  BIT(1)
#define BUCK_INT2_BUCK2_CLK_EXT_I                                  BIT(2)
#define BUCK_INT2_BUCK3_CLK_EXT_I                                  BIT(3)
#define BUCK_INT2_BUCK4_CLK_EXT_I                                  BIT(4)

#define BUCK_MSK0_REG                                              0xcd
#define BUCK_MSK0_BUCK0_POK_M                                      BIT(0)
#define BUCK_MSK0_BUCK1_POK_M                                      BIT(1)
#define BUCK_MSK0_BUCK2_POK_M                                      BIT(2)
#define BUCK_MSK0_BUCK3_POK_M                                      BIT(3)
#define BUCK_MSK0_BUCK4_POK_M                                      BIT(4)
#define BUCK_MSK0_VL0_M                                            BIT(5)
#define BUCK_MSK0_VL1_M                                            BIT(6)
#define BUCK_MSK0_VL2_M                                            BIT(7)

#define BUCK_MSK1_REG                                              0xce
#define BUCK_MSK1_BUCK0_SC_M                                       BIT(0)
#define BUCK_MSK1_BUCK1_SC_M                                       BIT(1)
#define BUCK_MSK1_BUCK2_SC_M                                       BIT(2)
#define BUCK_MSK1_BUCK3_SC_M                                       BIT(3)
#define BUCK_MSK1_BUCK4_SC_M                                       BIT(4)
#define BUCK_MSK1_VL0_ALTIN_M                                      BIT(5)
#define BUCK_MSK1_VL1_ALTIN_M                                      BIT(6)
#define BUCK_MSK1_VL2_ALTIN_M                                      BIT(7)

#define BUCK_MSK2_REG                                              0xcf
#define BUCK_MSK2_BUCK0_CLK_EXT_M                                  BIT(0)
#define BUCK_MSK2_BUCK1_CLK_EXT_M                                  BIT(1)
#define BUCK_MSK2_BUCK2_CLK_EXT_M                                  BIT(2)
#define BUCK_MSK2_BUCK3_CLK_EXT_M                                  BIT(3)
#define BUCK_MSK2_BUCK4_CLK_EXT_M                                  BIT(4)

#define BUCK_STAT0_REG                                             0xd0
#define BUCK_STAT0_BUCK0_POK                                       BIT(0)
#define BUCK_STAT0_BUCK1_POK                                       BIT(1)
#define BUCK_STAT0_BUCK2_POK                                       BIT(2)
#define BUCK_STAT0_BUCK3_POK                                       BIT(3)
#define BUCK_STAT0_BUCK4_POK                                       BIT(4)
#define BUCK_STAT0_VL0                                             BIT(5)
#define BUCK_STAT0_VL1                                             BIT(6)
#define BUCK_STAT0_VL2                                             BIT(7)

#define BUCK_STAT1_REG                                             0xd1
#define BUCK_STAT1_BUCK0_SC                                        BIT(0)
#define BUCK_STAT1_BUCK1_SC                                        BIT(1)
#define BUCK_STAT1_BUCK2_SC                                        BIT(2)
#define BUCK_STAT1_BUCK3_SC                                        BIT(3)
#define BUCK_STAT1_BUCK4_SC                                        BIT(4)
#define BUCK_STAT1_VL0_ALTIN                                       BIT(5)
#define BUCK_STAT1_VL1_ALTIN                                       BIT(6)
#define BUCK_STAT1_VL2_ALTIN                                       BIT(7)

#define BUCK_STAT2_REG                                             0xd2
#define BUCK_STAT2_BUCK0_CLK_EXT                                   BIT(0)
#define BUCK_STAT2_BUCK1_CLK_EXT                                   BIT(1)
#define BUCK_STAT2_BUCK2_CLK_EXT                                   BIT(2)
#define BUCK_STAT2_BUCK3_CLK_EXT                                   BIT(3)
#define BUCK_STAT2_BUCK4_CLK_EXT                                   BIT(4)

#define BUCK_STAT3_REG                                             0xd3
#define BUCK_STAT3_BUCK0_LOCK                                      BIT(0)
#define BUCK_STAT3_BUCK1_LOCK                                      BIT(1)
#define BUCK_STAT3_BUCK2_LOCK                                      BIT(2)
#define BUCK_STAT3_BUCK3_LOCK                                      BIT(3)
#define BUCK_STAT3_BUCK4_LOCK                                      BIT(4)
#define BUCK_STAT3_BUCK01_LOCK                                     BIT(5)
#define BUCK_STAT3_BUCK23_LOCK                                     BIT(6)
#define BUCK_STAT3_BUCK45_LOCK                                     BIT(7)

#define BUCK_CFG_REG                                               0xd4
#define BUCK_CFG_BUCK0_SYNC_EN                                     BIT(0)
#define BUCK_CFG_BUCK1_SYNC_EN                                     BIT(1)
#define BUCK_CFG_BUCK2_SYNC_EN                                     BIT(2)
#define BUCK_CFG_BUCK3_SYNC_EN                                     BIT(3)
#define BUCK_CFG_BUCK4_SYNC_EN                                     BIT(4)

/***********************  BUCK01 REGISTERS  **********************************/
#define BUCK01_CFG_REG                                             0xd5
#define BUCK01_CFG_VL0_EN                                          BIT(0)
#define BUCK01_CFG_VL0_LPM_EN                                      BIT(1)
#define BUCK01_CFG_VL0_AD_EN                                       BIT(2)
#define BUCK01_CFG_VL0_HZ_EN                                       BIT(3)
#define BUCK01_CFG_VL0_ALTIN_EN                                    BIT(4)
#define BUCK01_CFG_VL0_VOUT                                        BITS(6, 5)
#define BUCK01_CFG_BUCK01_PH                                       BIT(7)

/***********************  BUCK0 REGISTERS  ***********************************/
#define BUCK0_CFG0_REG                                             0xd6
#define BUCK0_CFG1_REG                                             0xd7
#define BUCK0_CFG2_REG                                             0xd8
#define BUCK0_CFG3_REG                                             0xd9
#define BUCK0_CFG4_REG                                             0xda
#define BUCK0_CFG5_REG                                             0xdb
#define BUCK0_CFG6_REG                                             0xdc
#define BUCK0_CFG7_REG                                             0xdd
/***********************  BUCK1 REGISTERS  ***********************************/
#define BUCK1_CFG0_REG                                             0xde
#define BUCK1_CFG1_REG                                             0xdf
#define BUCK1_CFG2_REG                                             0xe0
#define BUCK1_CFG3_REG                                             0xe1
#define BUCK1_CFG4_REG                                             0xe2
#define BUCK1_CFG5_REG                                             0xe3
#define BUCK1_CFG6_REG                                             0xe4
#define BUCK1_CFG7_REG                                             0xe5
/***********************  BUCK23 REGISTERS  **********************************/
#define BUCK23_CFG_REG                                             0xe6
#define BUCK23_CFG_VL1_EN                                          BIT(0)
#define BUCK23_CFG_VL1_LPM_EN                                      BIT(1)
#define BUCK23_CFG_VL1_AD_EN                                       BIT(2)
#define BUCK23_CFG_VL1_HZ_EN                                       BIT(3)
#define BUCK23_CFG_VL1_ALTIN_EN                                    BIT(4)
#define BUCK23_CFG_VL1_VOUT                                        BITS(6, 5)
#define BUCK23_CFG_BUCK23_PH                                       BIT(7)

/***********************  BUCK2 REGISTERS  ***********************************/
#define BUCK2_CFG0_REG                                             0xe7
#define BUCK2_CFG1_REG                                             0xe8
#define BUCK2_CFG2_REG                                             0xe9
#define BUCK2_CFG3_REG                                             0xea
#define BUCK2_CFG4_REG                                             0xeb
#define BUCK2_CFG5_REG                                             0xec
#define BUCK2_CFG6_REG                                             0xed
#define BUCK2_CFG7_REG                                             0xee
/***********************  BUCK3 REGISTERS  ***********************************/
#define BUCK3_CFG0_REG                                             0xef
#define BUCK3_CFG1_REG                                             0xf0
#define BUCK3_CFG2_REG                                             0xf1
#define BUCK3_CFG3_REG                                             0xf2
#define BUCK3_CFG4_REG                                             0xf3
#define BUCK3_CFG5_REG                                             0xf4
#define BUCK3_CFG6_REG                                             0xf5
#define BUCK3_CFG7_REG                                             0xf6
/***********************  BUCK45 REGISTERS  **********************************/
#define BUCK45_CFG_REG                                             0xf7
#define BUCK45_CFG_VL2_EN                                          BIT(0)
#define BUCK45_CFG_VL2_LPM_EN                                      BIT(1)
#define BUCK45_CFG_VL2_AD_EN                                       BIT(2)
#define BUCK45_CFG_VL2_HZ_EN                                       BIT(3)
#define BUCK45_CFG_VL2_ALTIN_EN                                    BIT(4)
#define BUCK45_CFG_VL2_VOUT                                        BITS(6, 5)

/***********************  BUCK4 REGISTERS  ***********************************/
#define BUCK4_CFG0_REG                                             0xf8
#define BUCK4_CFG1_REG                                             0xf9
#define BUCK4_CFG2_REG                                             0xfa
#define BUCK4_CFG3_REG                                             0xfb
#define BUCK4_CFG4_REG                                             0xfc
#define BUCK4_CFG5_REG                                             0xfd
#define BUCK4_CFG6_REG                                             0xfe
#define BUCK4_CFG7_REG                                             0xff

/***********************  BUCK4 BIT & MASK  ***********************************/
#define BUCK_CFG0_EN                                               BIT(0)
#define BUCK_CFG0_LPM_EN                                           BIT(1)
#define BUCK_CFG0_ULPM_EN                                          BIT(2)
#define BUCK_CFG0_FPWM                                             BIT(3)
#define BUCK_CFG0_VOUT_RNG                                         BITS(5, 4)
#define BUCK_CFG0_VCS_SR                                           BITS(7, 6)

#define BUCK_CFG1_VOUT0                                            BITS(7, 0)

#define BUCK_CFG2_VOUT1                                            BITS(7, 0)

#define BUCK_CFG3_GAIN                                             BITS(1, 0)
#define BUCK_CFG3_CCOMP                                            BITS(3, 2)
#define BUCK_CFG3_ILIM                                             BITS(6, 4)
#define BUCK_CFG3_VCS_SR_EN                                        BIT(7)

#define BUCK_CFG4_SSP_SR                                           BITS(2, 0)
#define BUCK_CFG4_SST_SR                                           BITS(6, 4)

#define BUCK_CFG5_RD_SR                                            BITS(2, 0)
#define BUCK_CFG5_RU_SR                                            BITS(6, 4)

#define BUCK_CFG6_FREQ                                             BITS(1, 0)
#define BUCK_CFG6_FTRAK                                            BIT(2)
#define BUCK_CFG6_ULTRA                                            BIT(3)
#define BUCK_CFG6_ADIS1                                            BIT(4)
#define BUCK_CFG6_ADIS100                                          BIT(5)
#define BUCK_CFG6_SHDN                                             BITS(7, 6)
#define BUCK_CFG6_ADE											   BITS(5, 4)

#define BUCK_CFG7_SS_PAT                                           BITS(1, 0)
#define BUCK_CFG7_SS_FREQ                                          BITS(3, 2)
#define BUCK_CFG7_SS_ENV                                           BITS(5, 4)
#define BUCK_CFG7_SNS                                              BITS(7, 6)

/***********************  REAL TIME CLOCK REGISTERS  *************************/
#define RTC_INT_REG                                                0x0
#define RTC_INT_RTC60S_I                                           BIT(0)
#define RTC_INT_RTCA1_I                                            BIT(1)
#define RTC_INT_RTCA2_I                                            BIT(2)
#define RTC_INT_RTC1S_I                                            BIT(3)

#define RTC_MSK_REG                                                0x1
#define RTC_MSK_RTC60S_M                                           BIT(0)
#define RTC_MSK_RTCA1_M                                            BIT(1)
#define RTC_MSK_RTCA2_M                                            BIT(2)
#define RTC_MSK_RTC1S_M                                            BIT(3)

#define RTC_CFG0M_REG                                              0x2
#define RTC_CFG0M_BCD_M                                            BIT(0)
#define RTC_CFG0M_HRMODE_M                                         BIT(1)

#define RTC_CFG0_REG                                               0x3
#define RTC_CFG0_BCD                                               BIT(0)
#define RTC_CFG0_HRMODE                                            BIT(1)

#define RTC_CFG1_REG                                               0x4
#define RTC_CFG1_FCUR                                              BIT(0)
#define RTC_CFG1_FREEZE_SEC                                        BIT(1)
#define RTC_CFG1_RTCWAKE                                           BIT(2)

#define RTC_UPDATE_REG                                             0x5
#define RTC_UPDATE_UDR                                             BIT(0)
#define RTC_UPDATE_RBUDR                                           BIT(1)

#define RTC_UPDATED_REG                                            0x6
#define RTC_UPDATED_UDF                                            BIT(0)
#define RTC_UPDATED_RBUDF                                          BIT(1)

#define RTC_SEC_REG                                                0x7
#define RTC_SEC_SEC                                                BITS(6, 0)

#define RTC_MIN_REG                                                0x8
#define RTC_MIN_MIN                                                BITS(6, 0)

#define RTC_HOUR_REG                                               0x9
#define RTC_HOUR_HOUR                                              BITS(5, 0)
#define RTC_HOUR_AMPM                                              BIT(6)

#define RTC_DOW_REG                                                0xa
#define RTC_DOW_SUN                                                BIT(0)
#define RTC_DOW_MON                                                BIT(1)
#define RTC_DOW_TUE                                                BIT(2)
#define RTC_DOW_WED                                                BIT(3)
#define RTC_DOW_THU                                                BIT(4)
#define RTC_DOW_FRI                                                BIT(5)
#define RTC_DOW_SAT                                                BIT(6)

#define RTC_MONTH_REG                                              0xb
#define RTC_MONTH_MONTH                                            BITS(4, 0)

#define RTC_YEAR_REG                                               0xc
#define RTC_YEAR_YEAR                                              BITS(7, 0)

#define RTC_DOM_REG                                                0xd
#define RTC_DOM_DOM                                                BITS(5, 0)

#define RTC_AE1_REG                                                0xe
#define RTC_AE1_AESECA1                                            BIT(0)
#define RTC_AE1_AEMINA1                                            BIT(1)
#define RTC_AE1_AEHOURA1                                           BIT(2)
#define RTC_AE1_AEDOWA1                                            BIT(3)
#define RTC_AE1_AEMONTHA1                                          BIT(4)
#define RTC_AE1_AEYEARA1                                           BIT(5)
#define RTC_AE1_AEDOMA1                                            BIT(6)

#define RTC_SECA1_REG                                              0xf
#define RTC_SECA1_SECA1                                            BITS(6, 0)

#define RTC_MINA1_REG                                              0x10
#define RTC_MINA1_MINA1                                            BITS(6, 0)

#define RTC_HOURA1_REG                                             0x11
#define RTC_HOURA1_HOURA1                                          BITS(5, 0)
#define RTC_HOURA1_AMPMA1                                          BIT(6)

#define RTC_DOWA1_REG                                              0x12
#define RTC_DOWA1_SUNA1                                            BIT(0)
#define RTC_DOWA1_MONA1                                            BIT(1)
#define RTC_DOWA1_TUEA1                                            BIT(2)
#define RTC_DOWA1_WEDA1                                            BIT(3)
#define RTC_DOWA1_THUA1                                            BIT(4)
#define RTC_DOWA1_FRIA1                                            BIT(5)
#define RTC_DOWA1_SATA1                                            BIT(6)

#define RTC_MONTHA1_REG                                            0x13
#define RTC_MONTHA1_MONTHA1                                        BITS(4, 0)

#define RTC_YEARA1_REG                                             0x14
#define RTC_YEARA1_YEARA1                                          BITS(7, 0)

#define RTC_DOMA1_REG                                              0x15
#define RTC_DOMA1_DOMA1                                            BITS(5, 0)

#define RTC_AE2_REG                                                0x16
#define RTC_AE2_AESECA2                                            BIT(0)
#define RTC_AE2_AEMINA2                                            BIT(1)
#define RTC_AE2_RTC_AE2_AEHOURA1                                   BIT(2)
#define RTC_AE2_AEDOWA2                                            BIT(3)
#define RTC_AE2_AEMONTHA2                                          BIT(4)
#define RTC_AE2_AEYEARA2                                           BIT(5)
#define RTC_AE2_AEDOMA2                                            BIT(6)

#define RTC_SECA2_REG                                              0x17
#define RTC_SECA2_SECA2                                            BITS(6, 0)

#define RTC_MINA2_REG                                              0x18
#define RTC_MINA2_MINA2                                            BITS(6, 0)

#define RTC_HOURA2_REG                                             0x19
#define RTC_HOURA2_HOURA2                                          BITS(5, 0)
#define RTC_HOURA2_AMPMA2                                          BIT(6)

#define RTC_DOWA2_REG                                              0x1a
#define RTC_DOWA2_SUNA2                                            BIT(0)
#define RTC_DOWA2_MONA2                                            BIT(1)
#define RTC_DOWA2_TUEA2                                            BIT(2)
#define RTC_DOWA2_WEDA2                                            BIT(3)
#define RTC_DOWA2_THUA2                                            BIT(4)
#define RTC_DOWA2_FRIA2                                            BIT(5)
#define RTC_DOWA2_SATA2                                            BIT(6)

#define RTC_MONTHA2_REG                                            0x1b
#define RTC_MONTHA2_MONTHA2                                        BITS(4, 0)

#define RTC_YEARA2_REG                                             0x1c
#define RTC_YEARA2_YEARA2                                          BITS(7, 0)

#define RTC_DOMA2_REG                                              0x1d
#define RTC_DOMA2_DOMA2                                            BITS(5, 0)

/***********************  SCRATCHPAD R REGISTERS  ****************************/
#define SCRATCHPAD_R0_REG                                          0x20
#define SCRATCHPAD_R0_SCRATCHPAD_R0                                BITS(7, 0)

#define SCRATCHPAD_R1_REG                                          0x21
#define SCRATCHPAD_R1_SCRATCHPAD_R1                                BITS(7, 0)

#define SCRATCHPAD_R2_REG                                          0x22
#define SCRATCHPAD_R2_SCRATCHPAD_R2                                BITS(7, 0)

#define SCRATCHPAD_R3_REG                                          0x23
#define SCRATCHPAD_R3_SCRATCHPAD_R3                                BITS(7, 0)

#define SCRATCHPAD_R4_REG                                          0x24
#define SCRATCHPAD_R4_SCRATCHPAD_R4                                BITS(7, 0)

#define SCRATCHPAD_R5_REG                                          0x25
#define SCRATCHPAD_R5_SCRATCHPAD_R5                                BITS(7, 0)

#define SCRATCHPAD_R6_REG                                          0x26
#define SCRATCHPAD_R6_SCRATCHPAD_R6                                BITS(7, 0)

#define SCRATCHPAD_R7_REG                                          0x27
#define SCRATCHPAD_R7_SCRATCHPAD_R7                                BITS(7, 0)

/***********************  SCRATCHPAD S REGISTERS  ****************************/
#define SCRATCHPAD_S0_REG                                          0x28
#define SCRATCHPAD_S0_SCRATCHPAD_S0                                BITS(7, 0)

#define SCRATCHPAD_S1_REG                                          0x29
#define SCRATCHPAD_S1_SCRATCHPAD_S1                                BITS(7, 0)

#define SCRATCHPAD_S2_REG                                          0x2a
#define SCRATCHPAD_S2_SCRATCHPAD_S2                                BITS(7, 0)

#define SCRATCHPAD_S3_REG                                          0x2b
#define SCRATCHPAD_S3_SCRATCHPAD_S3                                BITS(7, 0)

#define SCRATCHPAD_S4_REG                                          0x2c
#define SCRATCHPAD_S4_SCRATCHPAD_S4                                BITS(7, 0)

#define SCRATCHPAD_S5_REG                                          0x2d
#define SCRATCHPAD_S5_SCRATCHPAD_S5                                BITS(7, 0)

#define SCRATCHPAD_S6_REG                                          0x2e
#define SCRATCHPAD_S6_SCRATCHPAD_S6                                BITS(7, 0)

#define SCRATCHPAD_S7_REG                                          0x2f
#define SCRATCHPAD_S7_SCRATCHPAD_S7                                BITS(7, 0)

/***********************  SCRATCHPAD O REGISTERS  ****************************/
#define SCRATCHPAD_O0_REG                                          0x30
#define SCRATCHPAD_O0_SCRATCHPAD_O0                                BITS(7, 0)

#define SCRATCHPAD_O1_REG                                          0x31
#define SCRATCHPAD_O1_SCRATCHPAD_O1                                BITS(7, 0)

#define SCRATCHPAD_O2_REG                                          0x32
#define SCRATCHPAD_O2_SCRATCHPAD_O2                                BITS(7, 0)

#define SCRATCHPAD_O3_REG                                          0x33
#define SCRATCHPAD_O3_SCRATCHPAD_O3                                BITS(7, 0)

#define SCRATCHPAD_O4_REG                                          0x34
#define SCRATCHPAD_O4_SCRATCHPAD_O4                                BITS(7, 0)

#define SCRATCHPAD_O5_REG                                          0x35
#define SCRATCHPAD_O5_SCRATCHPAD_O5                                BITS(7, 0)

#define SCRATCHPAD_O6_REG                                          0x36
#define SCRATCHPAD_O6_SCRATCHPAD_O6                                BITS(7, 0)

#define SCRATCHPAD_O7_REG                                          0x37
#define SCRATCHPAD_O7_SCRATCHPAD_O7                                BITS(7, 0)

/*
 * Minimum and maximum FPS period time (in microseconds)
 * Minimum : 32KHz :30, 4MHz : 0.25
 * maximum : 32KHz :7995, 4MHz : 4000
 */

/* FPS Period */
#define FPS_PERIOD_32KHZ_30US     0x00
#define FPS_PERIOD_32KHZ_61US     0x01
#define FPS_PERIOD_32KHZ_122US    0x02
#define FPS_PERIOD_32KHZ_244US    0x03
#define FPS_PERIOD_32KHZ_488US    0x04
#define FPS_PERIOD_32KHZ_762US    0x05
#define FPS_PERIOD_32KHZ_1007US   0x06
#define FPS_PERIOD_32KHZ_1251US   0x07
#define FPS_PERIOD_32KHZ_1495US   0x08
#define FPS_PERIOD_32KHZ_1739US   0x09
#define FPS_PERIOD_32KHZ_2014US   0x0A
#define FPS_PERIOD_32KHZ_2990US   0x0B
#define FPS_PERIOD_32KHZ_3997US   0x0C
#define FPS_PERIOD_32KHZ_5004US   0x0D
#define FPS_PERIOD_32KHZ_6011US   0x0E
#define FPS_PERIOD_32KHZ_7995US   0x0F

#define FPS_PERIOD_4KHZ_025US     0x00
#define FPS_PERIOD_4KHZ_050US     0x01
#define FPS_PERIOD_4KHZ_1US       0x02
#define FPS_PERIOD_4KHZ_2US       0x03
#define FPS_PERIOD_4KHZ_4US       0x04
#define FPS_PERIOD_4KHZ_8US       0x05
#define FPS_PERIOD_4KHZ_16US      0x06
#define FPS_PERIOD_4KHZ_25US      0x07
#define FPS_PERIOD_4KHZ_50US      0x08
#define FPS_PERIOD_4KHZ_100US     0x09
#define FPS_PERIOD_4KHZ_250US     0x0A
#define FPS_PERIOD_4KHZ_500US     0x0B
#define FPS_PERIOD_4KHZ_1000US    0x0C
#define FPS_PERIOD_4KHZ_2000US    0x0D
#define FPS_PERIOD_4KHZ_3000US    0x0E
#define FPS_PERIOD_4KHZ_4000US    0x0F

/* INPUT DEBOUNCE FILTER */
#define MAX77851_NO_RESYNC_NO_DEB           0x00
#define MAX77851_RESYNC_NO_DEB              0x01
#define MAX77851_RESYNC_100US_DEB           0x02
#define MAX77851_RESYNC_1MS_DEB             0x03
#define MAX77851_RESYNC_4MS_DEB             0x04
#define MAX77851_RESYNC_8MS_DEB             0x05
#define MAX77851_RESYNC_16MS_DEB            0x06
#define MAX77851_RESYNC_32MS_DEB            0x07

#define MAX77851_FPS_PERIOD_MIN_US FPS_PERIOD_32KHZ_30US
#define MAX77851_FPS_PERIOD_MAX_US FPS_PERIOD_32KHZ_7995US

#define MAX77851_FPS_PU_SLPX_SLOT_MASK    0xF0
#define MAX77851_FPS_PU_SLPX_SLOT_SHIFT   0x04

#define MAX77851_FPS_PD_SLPY_SLOT_MASK    0x0F
#define MAX77851_FPS_PD_SLPY_SLOT_SHIFT   0x00

#define MAX77851_FPS_PD_SLOT_MASK         0x0F
#define MAX77851_FPS_PD_SLOT_SHIFT        0x00

#define MAX77851_FPS_SLPY_SLOT_MASK       0x0F
#define MAX77851_FPS_SLPY_SLOT_SHIFT      0x00

#define MAX77851_FPS_PU_SLOT_MASK         0xF0
#define MAX77851_FPS_PU_SLOT_SHIFT        0x04

#define MAX77851_FPS_SLPX_SLOT_MASK       0xF0
#define MAX77851_FPS_SLPX_SLOT_SHIFT      0x04

#define FPS_CFG0_PD_MASK                  BITS(1, 0)
#define FPS_CFG0_PU_MASK                  BITS(5, 4)
#define FPS_CFG0_EN_MASK                  BIT(6)
#define FPS_CFG0_ABT_EN_MASK              BIT(7)

#define FPS_CFG1_SLPY_MASK                BITS(1, 0)
#define FPS_CFG1_SLP_EN_MASK              BITS(3, 2)
#define FPS_CFG1_SLPX_MASK                BITS(5, 4)
#define FPS_CFG1_FPS_CFG1_RSVD_MASK       BIT(6)
#define FPS_CFG1_ABT_MASK                 BIT(7)

#define FPS_CFG2_PD_T_MASK                BITS(3, 0)
#define FPS_CFG2_PU_T_MASK                BITS(7, 4)

#define FPS_CFG3_SLPY_T_MASK              BITS(3, 0)
#define FPS_CFG3_SLPX_T_MASK              BITS(7, 4)

#define FPS_CFG4_PD_MAX_MASK              BITS(1, 0)
#define FPS_CFG4_SLPY_MAX_MASK            BITS(3, 2)
#define FPS_CFG4_PU_MAX_MASK              BITS(5, 4)
#define FPS_CFG4_SLPX_MAX_MASK            BITS(7, 6)

#define MAX77851_FPS_DISABLE      0x00
#define MAX77851_FPS_ENABLE       0x01
#define MAX77851_FPS_DEFAULT      0x02

#define MAX77851_FPS_ABORT_DISABLE   0x00
#define MAX77851_FPS_ABORT_ENABLE    0x01

#define MAX77851_FPS_SLEEP_DISABLE   0x00
#define MAX77851_FPS_SLEEP_ENABLE    0x01
#define MAX77851_FPS_SLEEP_LPM       0x02
#define MAX77851_FPS_SLEEP_ULPM      0x03

#define MAX77851_FPS_ABORT_NEXT_SLOT           0x00
#define MAX77851_FPS_ABORT_NEXT_MASTER_SLOT    0x01

#define MAX77851_FPS_16_SLOTS       0x00
#define MAX77851_FPS_12_SLOTS       0x01
#define MAX77851_FPS_10_SLOTS       0x02
#define MAX77851_FPS_08_SLOTS       0x03

#define MAX77851_LOW_BAT_ENABLE       0x01
#define MAX77851_LOW_BAT_DISABLE      0x00

#define MAX77851_LOW_BAT_ALARM_ENABLE     0x01
#define MAX77851_LOW_BAT_ALARM_AUTO_MODE  0x00

#define SYS_WD_CLR_COMMAND			0x85
#define	MAX77851_TWD_2_SEC			0x00
#define	MAX77851_TWD_16_SEC			0x01
#define	MAX77851_TWD_64_SEC			0x02
#define	MAX77851_TWD_128_SEC		0x03

/* GPIO & FPSO */
#define FPSO_PD_MASK                                                BIT(0)
#define FPSO_PU_MASK                                                BIT(1)
#define FPSO_MODE_MASK                                              BITS(3, 2)
#define FPSO_DRV_MASK                                               BIT(4)
#define FPSO_OFILTER_MASK                                           BIT(5)
#define FPSO_OUTPUT_MASK                                            BIT(6)
#define FPSO_POL_MASK                                               BIT(7)

#define GPIO_OUTPUT_VAL_LOW									BIT_IS_ZERO
#define GPIO_OUTPUT_VAL_HIGH								BIT(6)

#define GPIO_DBNC_NONE		(BIT_IS_ZERO)
#define GPIO_DBNC_100US		BIT(5)
#define GPIO_DBNC_1MS		BITS(5, 4)
#define GPIO_DBNC_4MS		BIT(6)
#define GPIO_DBNC_8MS		(BIT(6) | BIT(4))
#define GPIO_DBNC_16MS		(BIT(6) | BIT(5))
#define GPIO_DBNC_32MS		BITS(6, 4)

#define GPIO_DRV_OPENDRAIN	(BIT_IS_ZERO)
#define GPIO_DRV_PUSHPULL	BIT(4)

/* LDO & BUCK */
#define REGULATOR_ENABLE_MASK                                BIT(0)
#define REGULATOR_ENABLE                                     BIT(0)
#define REGULATOR_DISABLE                                    BIT_IS_ZERO

enum max77851_alternate_pinmux_option {
	/* GPIO 0-7 */
	GPIO_PINMUX_HIGH_Z							= 0,
	GPIO_PINMUX_GPIO_INPUT						= 1,
	GPIO_PINMUX_GPIO_OUTPUT						= 2,
	GPIO_PINMUX_FPS_DIGITAL_INPUT				= 3,
	GPIO_PINMUX_FPS_DIGITAL_OUTPUT				= 4,
	GPIO_PINMUX_SRC_ENABLE_DIGITAL_INPUT		= 5,
	GPIO_PINMUX_SRC_BOOT_DVS_DIGITAL_INPUT		= 6,
	GPIO_PINMUX_SRC_CLOCK_DIGITAL_INPUT			= 7,
	GPIO_PINMUX_SRC_FPWM_DIGITAL_INPUT			= 8,
	GPIO_PINMUX_SRC_POK_GPIO_DIGITAL_OUTPUT		= 9,
	GPIO_PINMUX_CLK_32K_OUT						= 10,
	GPIO_PINMUX_LB_ALARM_OUTPUT					= 11,
	GPIO_PINMUX_O_TYPE_RESET					= 12,
	GPIO_PINMUX_TEST_DIGITAL_INPUT				= 13,
	GPIO_PINMUX_TEST_DIGITAL_OUTPUT				= 14,
	GPIO_PINMUX_TEST_ANALOG_IN_OUT				= 15,

	/* FPSO 0-3 */
	FPSO_PINMUX_OFFSET							= 16,
	FPSO_PINMUX_HIGH_Z							= 16,
	FPSO_PINMUX_DIGITAL_OUTPUT					= 17,
	FPSO_PINMUX_FPS_DIGITAL_OUTPUT				= 18,
	FPSO_PINMUX_BUCK_SENSE						= 19,

	/* NRSTIO */
	NRSTIO_PINMUX_OFFSET						= 20,
	NRSTIO_PINMUX_HIGH_Z						= 20,
	NRSTIO_PINMUX_DIGITAL_INPUT					= 21,
	NRSTIO_PINMUX_DIGITAL_OUTPUT				= 22,
	NRSTIO_PINMUX_FPS_DIGITAL_OUTPUT			= 23,
	NRSTIO_PINMUX_LB_DIGITAL_OUTPUT				= 24,
};

#define fps_master_slot_name(fps_src)	\
	(fps_src == MAX77851_FPS_MASTER_SLOT_0 ? "MASTER_SLOT_0" :	\
	fps_src == MAX77851_FPS_MASTER_SLOT_1 ? "MASTER_SLOT_1" :	\
	fps_src == MAX77851_FPS_MASTER_SLOT_2 ? "MASTER_SLOT_2" :	\
	fps_src == MAX77851_FPS_MASTER_SLOT_3 ? "MASTER_SLOT_3" : "MASTER_SLOT_NONE")

/* Interrupts */
enum {
	/* Addr : 0x03 */
	MAX77851_IRQ_TOP_BUCK,
	MAX77851_IRQ_TOP_EN,
	MAX77851_IRQ_TOP_FPS,
	MAX77851_IRQ_TOP_GPIO,
	MAX77851_IRQ_TOP_IO,
	MAX77851_IRQ_TOP_LDO,
	MAX77851_IRQ_TOP_RLOGIC,
	MAX77851_IRQ_TOP_RTC,
	/* Addr : 0x04 */
	MAX77851_IRQ_TOP_UVLO,
	MAX77851_IRQ_TOP_LB,
	MAX77851_IRQ_TOP_LB_ALM,
	MAX77851_IRQ_TOP_OVLO,
	MAX77851_IRQ_TOP_TJ_SHDN,
	MAX77851_IRQ_TOP_TJ_ALM1,
	MAX77851_IRQ_TOP_TJ_ALM2,
	MAX77851_IRQ_TOP_TJ_SMPL,
};

/* GPIOs */
enum {
	MAX77851_GPIO0,
	MAX77851_GPIO1,
	MAX77851_GPIO2,
	MAX77851_GPIO3,
	MAX77851_GPIO4,
	MAX77851_GPIO5,
	MAX77851_GPIO6,
	MAX77851_GPIO7,

	MAX77851_FPSO0,
	MAX77851_FPSO1,
	MAX77851_FPSO2,
	MAX77851_FPSO3,

	MAX77851_NRSTIO,

	MAX77851_GPIO_NR,
};

/* FPS Source */
enum max77851_fps_master_slot {
	MAX77851_FPS_MASTER_SLOT_0 = 1,
	MAX77851_FPS_MASTER_SLOT_1 = 2,
	MAX77851_FPS_MASTER_SLOT_2 = 4,
	MAX77851_FPS_MASTER_SLOT_3 = 8,
	MAX77851_FPS_MASTER_SLOT_DEFAULT = 255,
};

enum max77851_fps_master_slot_num {
	MAX77851_FPS_SLOT_0 = 0,
	MAX77851_FPS_SLOT_1,
	MAX77851_FPS_SLOT_2,
	MAX77851_FPS_SLOT_3,
	MAX77851_FPS_SLOT_4,
	MAX77851_FPS_SLOT_5,
	MAX77851_FPS_SLOT_6,
	MAX77851_FPS_SLOT_7,
	MAX77851_FPS_SLOT_8,
	MAX77851_FPS_SLOT_9,
	MAX77851_FPS_SLOT_A,
	MAX77851_FPS_SLOT_B,
	MAX77851_FPS_SLOT_C,
	MAX77851_FPS_SLOT_D,
	MAX77851_FPS_SLOT_E,
	MAX77851_FPS_SLOT_F,
	MAX77851_FPS_SLOT_DEFAULT = 255,
};

enum max77851_vout_range_num {
	MAX77851_VOUT_RNG_LOW = 0,
	MAX77851_VOUT_RNG_MID,
	MAX77851_VOUT_RNG_HIGH,
};

enum max77851_vout_num {
	MAX77851_VOUT0 = 0,
	MAX77851_VOUT1,
	MAX77851_VOUT_NUM,
};

enum max77851_chip_id {
	MAX77851,
};

enum max77851_fps_sleep_mode {
	MAX77851_FPS_MASTER_SLEEP_DISABLE,
	MAX77851_FPS_MASTER_SLEEP_ENABLE,
	MAX77851_FPS_MASTER_SLEEP_LPM,
	MAX77851_FPS_MASTER_SLEEP_ULPM,
};

enum max77851_mx_fps_master_num {
	MX_FPS_MASTER0 = 0,
	MX_FPS_MASTER1,
	MX_FPS_MASTER2,
	MX_FPS_MASTER3,
	MX_FPS_MASTER_NUM,
};

enum max77851_mx_fps_master_slot_num {
	FPS_MX_MASTER_SLOT_0 = 0,
	FPS_MX_MASTER_SLOT_1,
	FPS_MX_MASTER_SLOT_2,
	FPS_MX_MASTER_SLOT_3,
};

/* Regulator Configurations */
struct max77851_fps_data {
	u8 fps_cfg0_addr;
	u8 fps_cfg1_addr;
	u8 fps_cfg2_addr;

	int pu_slpx_master_slot;
	int pd_slpy_master_slot;
	int pd_slot;
	int pu_slot;
	int slpy_slot;
	int slpx_slot;
};

struct max77851_fps_master_data {
	unsigned int pd_slot;
	unsigned int pu_slot;
	unsigned int slpx_slot;
	unsigned int slpy_slot;

	unsigned int pd_period;
	unsigned int pu_period;
	unsigned int slpx_period;
	unsigned int slpy_period;

	unsigned int enable;
	unsigned int sleep_mode;
	unsigned int abort_enable;
	unsigned int abort_mode;

	unsigned int pd_max_slot;
	unsigned int pu_max_slot;
	unsigned int slpy_max_slot;
	unsigned int slpx_max_slot;
};

struct max77851_chip {
	struct device *dev;
	struct regmap *rmap;

	int chip_irq;
	int irq_base;

	bool sleep_enable;
	bool enable_global_lpm;
	bool sleep_on_suspend;

	unsigned int fps_master_pd_slot_period;
	unsigned int fps_master_pu_slot_period;
	unsigned int fps_master_slpx_slot_period;
	unsigned int fps_master_slpy_slot_period;

	struct max77851_fps_master_data fps_master_data[MX_FPS_MASTER_NUM];

	struct regmap_irq_chip_data *top_irq_data;
	struct regmap_irq_chip_data *gpio_irq_data;
};

#endif /* _MFD_MAX77851_H_ */
