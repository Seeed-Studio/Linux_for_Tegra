/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

/* No header guards as this file doesnot define any. Added for documentation only */

/**
 * @defgroup CalPcieEpLinux PCIe_EP_Linux_Device_Tree
 *
 * **PCIe Controller DT properties**
 *
 *  `compatible`:
 *
 *   - **Description:**  Compatibility string name.
 *
 *   - **Range:** "nvidia,tegra264-pcie-ep".
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 *  `status`:
 *
 *   - **Description:** Enabled status of PCIe controller.
 *
 *   - **Range:** "okay" or "disabled"
 *
 *   - **Resolution:**
 *
 *        - Customizable: No.
 *
 *        * Optional: No.
 *
 *  `reg`:
 *
 *   - **Description:** A list of physical base address and length pairs for each set of controller
 *     registers. Must contain an entry for each entry in the reg-names property in same order.
 *
 *   - **Range:** Values for EP Controller (C2, C4, C5) respectively are
 *
			reg = <0xa8 0x08420000 0x0 0x00004000
			       0xa8 0x08500000 0x0 0x00001000
			       0xa8 0x08510000 0x0 0x00001000
			       0xa8 0x08511000 0x0 0x00001000
			       0xa8 0x08520000 0x0 0x00010000
			       0xa8 0x08430000 0x0 0x00010000
			       0xb0 0x80000000 0x8 0x00000000>;

			reg = <0xa8 0x08460000 0x0 0x00004000
			       0xa8 0x08530000 0x0 0x00001000
			       0xa8 0x08540000 0x0 0x00001000
			       0xa8 0x08541000 0x0 0x00001000
			       0xa8 0x08550000 0x0 0x00010000
			       0xa8 0x08470000 0x0 0x00010000
			       0xc0 0x80000000 0x8 0x00000000>;

			reg = <0xa8 0x08480000 0x0 0x00004000
			       0xa8 0x08560000 0x0 0x00001000
			       0xa8 0x08570000 0x0 0x00001000
			       0xa8 0x08571000 0x0 0x00001000
			       0xa8 0x08580000 0x0 0x00010000
			       0xa8 0x08490000 0x0 0x00010000
			       0xc8 0x80000000 0x8 0x00000000>;
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 *  `reg-names`:
 *
 *   - **Description:** Names of the registers defined in `reg` property. Details explained in the
 *     range.
 *
 *   - **Range:** Must include the following entries in same order:
 *    - xal: Controller's application logic registers.
 *    - xal-ep-dm: Controller's transport layer
 *    - xtl-ep-pri: Controller's transport layer private registers.
 *    - xtl-ep-cfg: Controller's PCIe config registers.
 *    - xpl: Controller's physical layer registers.
 *    - xdma: Controller's XDMA registers.
 *    - addr_space: Used to map remote RC address space
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 *  `intr`:
 *
 *   - **Description:** A list of interrupt outputs of the controller. Must contain an entry for
 *     each entry in the interrupt-names property
 *
 *   - **Range:** Values for EP Controller (C2, C4, C5) are 0x391, 0x3a4 and 0x3ad respectively.
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 *  `intr-names`:
 *
 *   - **Description:** Names of the registers defined in `intr` property. Details explained in the
 *     range.
 *
 *   - **Range:** Must include the "intr" entry
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 * `nvidia,bpmp`:
 *
 *   - **Description:** Contains phanldle of bpmp node  and controller ID of the PCIe controller.
 *
 *   - **Range:** Supported values for controller ID are 2, 4, 5.
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 * `dma-coherent`:
 *
 *   - **Description:** boolean flag which indicates whether PCIe is DMA coherent or not.
 *
 *   - **Resolution:**:
 *
 *        * Customizable: No.
 *
 *        * Optional: No.
 *
 * `reset-gpios`:
 *
 *   - **Description:** Must contain a phandle to a GPIO controller followed by GPIO that is being
 *     used as PERST input signal.
 *
 *   - **Resolution:**:
 *
 *        * Customizable: Yes, based on the platform design.
 *
 *        * Optional: No.
 *
 * `nvidia,pex-prsnt-gpios`:
 *
 *   - **Description:** Must contain a phandle to a GPIO controller followed by GPIO that is being
 *     used as PRSNT output signal.
 *     This GPIO is used to generate hot-plug events to the connected root port device.
 *
 *   - **Resolution:**:
 *
 *        * Customizable: Yes, based on the platform design.
 *
 *        * Optional: Yes.
 *

 * Example:
    @verbatim
    pcie-ep@a808460000 {
	    status = "disabled";
	    compatible = "nvidia,tegra264-pcie-ep";
	    reg = <0xa8 0x08460000 0x0 0x00004000
		    0xa8 0x08530000 0x0 0x00001000
		    0xa8 0x08540000 0x0 0x00001000
		    0xa8 0x08541000 0x0 0x00001000
		    0xa8 0x08550000 0x0 0x00010000
		    0xa8 0x08470000 0x0 0x00010000
		    0xc0 0x80000000 0x8 0x00000000>;
	    reg-names = "xal", "xal-ep-dm", "xtl-ep-pri", "xtl-ep-cfg", "xdma", "xpl", "addr_space";
	    interrupts = <GIC_SPI 0x3a4 IRQ_TYPE_LEVEL_HIGH>;
	    interrupt-names = "intr";
	    reset-gpios = <&gpio_uphy TEGRA264_UPHY_GPIO(D, 1) GPIO_ACTIVE_LOW>;
	    iommus = <&smmu1_mmu 0x40000>;
	    dma-coherent;
	    msi-parent = <&its 0x140000>;
	    nvidia,bpmp = <&bpmp 0x4>;
    };
    @endverbatim
 */
