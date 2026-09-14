/*
 *
 * If you received this File from Marvell, you may opt to use, redistribute and/or
 * modify this File in accordance with the terms and conditions of the General
 * Public License Version 2, June 1991 (the "GPL License"), a copy of which is
 * available along with the File in the license.txt file or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or
 * on the worldwide web at http://www.gnu.org/licenses/gpl.txt.
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY
 * DISCLAIMED. The GPL License provides additional details about this warranty
 * disclaimer.
 *
 */
#include "oak_irq.h"

struct oak_tstruct;

/* Name        : oak_irq_dis_gicu
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u32 mask_0, u32 mask_1
 * Description : This function set GICU mask.
 */
static void oak_irq_dis_gicu(struct oak_tstruct *np, u32 mask_0, u32 mask_1)
{
	oak_unimac_io_write_32(np, OAK_GICU_HOST_SET_MASK_0, mask_0);
	oak_unimac_io_write_32(np, OAK_GICU_HOST_SET_MASK_1, mask_1);
}

/* Name        : oak_irq_ena_gicu
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u32 mask_0, u32 mask_1
 * Description : This function clear GICU mask.
 */
static void oak_irq_ena_gicu(struct oak_tstruct *np, u32 mask_0, u32 mask_1)
{
	oak_unimac_io_write_32(np, OAK_GICU_HOST_CLR_MASK_0, mask_0);
	oak_unimac_io_write_32(np, OAK_GICU_HOST_CLR_MASK_1, mask_1);
}

/* Name        : oak_irq_reset_gicu_ldg
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function resets Generic Interrupt Controller (gicu)
 * Logical devices and device groups (ldg)
 */
static void oak_irq_reset_gicu_ldg(struct oak_tstruct *np)
{
	u32 i = 0;

	/* Reset GICU logical device group structure members */
	while (i < np->gicu.num_ldg) {
		np->gicu.ldg[i].device = np;
		np->gicu.ldg[i].msi_grp = i;
		np->gicu.ldg[i].msi_tx = 0;
		np->gicu.ldg[i].msi_rx = 0;
		np->gicu.ldg[i].msi_te = 0;
		np->gicu.ldg[i].msi_re = 0;
		np->gicu.ldg[i].msi_ge = 0;
		np->gicu.ldg[i].msiname[0] = '\0';
		++i;
	}
}

/* Name        : oak_irq_set_tx_rx_dma_bit
 * Returns     : int
 * Parameters  : struct oak_tstruct *np, int grp
 * Description : This function sets the tx and rx bit of a dma channel
 */
static u32 oak_irq_set_tx_rx_dma_bit(struct oak_tstruct *np, u32 grp)
{
	u32 i = 0;
	u64 val;

	val = (1UL << TX_DMA_BIT);
	/* Set tx DMA bit for all the tx channels */
	while (i < np->num_tx_chan) {
		if (np->gicu.num_ldg > 0)
			grp = (grp % np->gicu.num_ldg);
		np->gicu.ldg[grp].msi_tx |= val;
		val <<= 4ULL;
		++grp;
		++i;
	}

	i = 0;
	val = (1UL << RX_DMA_BIT);
	/* Set rx DMA bit for all the rx channels */
	while (i < np->num_rx_chan) {
		if (np->gicu.num_ldg > 0)
			grp = (grp % np->gicu.num_ldg);
		np->gicu.ldg[grp].msi_rx |= val;
		val <<= 4UL;
		++grp;
		++i;
	}
	return grp;
}

/* Name        : oak_irq_set_tx_rx_err_bit
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, int grp
 * Description : This function sets the tx and rx err bit of a dma channel
 */
static void oak_irq_set_tx_rx_err_bit(struct oak_tstruct *np, u32 grp)
{
	u32 i = 0;
	u64 val;

	val = (1UL << TX_ERR_BIT);
	/* Set tx error bit for all the tx channels */
	while (i < np->num_tx_chan) {
		if (np->gicu.num_ldg > 0)
			grp = (grp % np->gicu.num_ldg);
		np->gicu.ldg[grp].msi_te |= val;
		val <<= 4ULL;
		++grp;
		++i;
	}

	i = 0;
	val = (1UL << RX_ERR_BIT);
	/* Set rx error bit for all the rx channels */
	while (i < np->num_rx_chan) {
		if (np->gicu.num_ldg > 0)
			grp = (grp % np->gicu.num_ldg);
		np->gicu.ldg[grp].msi_re |= val;
		val <<= 4ULL;
		++grp;
		++i;
	}
}

/* Name        : oak_irq_callback
 * Returns     : irqreturn_t
 * Parameters  : int irq, void *cookie
 * Description : This function set GICU IRQ mask and schedule NAPI
 */
static irqreturn_t oak_irq_callback(int irq, void *cookie)
{
	ldg_t *ldg = (ldg_t *)cookie;
	irqreturn_t rc_4 = IRQ_HANDLED;

	oak_unimac_io_write_32(ldg->device, OAK_GICU_INTR_GRP_SET_MASK,
			       ldg->msi_grp |
			       (u32)OAK_GICU_INTR_GRP_MASK_ENABLE);
#ifdef DEBUG
	{
		u32 mask_0;
		u32 mask_1;

		mask_0 = oak_unimac_io_read_32(ldg->device,
					       OAK_GICU_INTR_FLAG_0);
		mask_1 = oak_unimac_io_read_32(ldg->device,
					       OAK_GICU_INTR_FLAG_1);
		oakdbg(debug, INTR, "IRQ GRP %d [flag0=0x%0x flag1=0x%0x]",
		       ldg->msi_grp, mask_0, mask_1);
	}
#endif
	/* Schedule NAPI poll */
	napi_schedule(&ldg->napi);
	oakdbg(debug, INTR, "============ IRQ GRP END ============");
	return rc_4;
}

/* Name        : oak_request_irq
 * Returns     : int
 * Parameters  : struct oak_tstruct *np, ldg_t *ldg,
 * const char *str, u32 idx, int cpu
 * Description : This function registers the PCIe irq
 */
static int oak_request_irq(struct oak_tstruct *np, ldg_t *ldg,
			   const char *str, u32 idx,  u32 cpu)
{
	int retval = 0;

	snprintf(np->gicu.ldg[idx].msiname,
		 sizeof(np->gicu.ldg[idx].msiname) - 1,
		 "%s-%s-%d", np->pdev->driver->name, str, idx);

#ifdef OAK_MSIX_LEGACY
	/* In older version of the kernel pci_irq_vector() is not supported
	 */
	retval = request_irq(np->gicu.msi_vec[idx].vector, oak_irq_callback, 0,
			     np->gicu.ldg[idx].msiname, &np->gicu.ldg[idx]);
	if (retval == 0)
		retval = irq_set_affinity_hint(np->gicu.msi_vec[idx].vector,
					       get_cpu_mask(cpu));
#else
	/* To get the Linux IRQ numbers passed to request_irq(), Call the
	 * function pci_irq_vector() because most of the hard work is done for
	 * the driver in the PCI layer. The driver simply has to request that
	 * the PCI layer set up the MSI capability for Oak device.
	 */
	retval = request_irq(pci_irq_vector(np->pdev, idx), oak_irq_callback, 0,
			     np->gicu.ldg[idx].msiname, &np->gicu.ldg[idx]);
	if (retval == 0)
		/* Deprecated. Use irq_update_affinity_hint() or
		 * irq_set_affinity_and_hint() instead irq_set_affinity_hint
		 * as menioned in include/linux/interrupt.h
		 */
		retval = irq_set_affinity_hint(pci_irq_vector(np->pdev, idx),
					       get_cpu_mask(cpu));
#endif
	oakdbg(debug, INTR,
	       "np=%p ivec[%2d]=%2d tx=0x%8llx rx=0x%8llx te=0x%8llx re=0x%8llx ge=%8llx type=%s err=%d",
	       np, np->gicu.ldg[idx].msi_grp, np->gicu.msi_vec[idx].vector,
	       ldg->msi_tx, ldg->msi_rx, ldg->msi_te, ldg->msi_re,
	       ldg->msi_ge, str, retval);

	return retval;
}

/* Name        : oak_request_single_ivec
 * Returns     : int
 * Parameters  : struct oak_tstruct *np, ldg_t *ldg, u64 val, u32 idx,
 * int cpu
 * Description : This function check and set the ldg msix value, then call the
 * request irq oak driver function.
 */
static int oak_irq_request_single_ivec(struct oak_tstruct *np, ldg_t *ldg,
				       u64 val, u32 idx, u32 cpu)
{
	const char *str = "xx";
	int retval;

	/* Set ldg MSI structure members tx, rx, te, re, ge */
	if (val == ldg->msi_tx)
		str = "tx";
	if (val == ldg->msi_rx)
		str = "rx";
	if (val == ldg->msi_te)
		str = "te";
	if (val == ldg->msi_re)
		str = "re";
	if (val == ldg->msi_ge)
		str = "ge";

	retval = oak_request_irq(np, ldg, str, idx, cpu);

	return retval;
}

/* Name        : oak_irq_allocate_ivec
 * Returns     : int
 * Parameters  : struct oak_tstruct *np
 * Description : This function allocate ivec
 */
static int oak_irq_allocate_ivec(struct oak_tstruct *np)
{
	u32 i = 0;
	u32 cpu;
	int err = 0;

	cpu = cpumask_first(cpu_online_mask);
	/* In a loop we need to check for all the available online CPU and then
	 * request and map IRQ line from linux kernel. The function call
	 * oak_irq_request_single_ivec finally endup calling request_irq and
	 * irq_set_affinity_hint linux kernel functions.
	 */
	while ((i < np->gicu.num_ldg) && (err == 0)) {
		u64 val;
		ldg_t *p = &np->gicu.ldg[i];

		val = (p->msi_tx | p->msi_rx | p->msi_te | p->msi_re
				| p->msi_ge);
		if (val != 0) {
			err = oak_irq_request_single_ivec(np, p, val, i, cpu);
			cpu = cpumask_next(cpu, cpu_online_mask);
			if (cpu >= nr_cpu_ids)
				cpu = cpumask_first(cpu_online_mask);
			if (err != 0) {
				/* Reset ldg MSI structure members */
				p->msi_tx = 0;
				p->msi_rx = 0;
				p->msi_te = 0;
				p->msi_re = 0;
				p->msi_ge = 0;
			}
		}
		++i;
	}
	return err;
}

/* Name         : oak_irq_request_ivec
 * Returns      : int
 * Parameters   : struct oak_tstruct *np
 *  Description : This function request irq vector
 */
int oak_irq_request_ivec(struct oak_tstruct *np)
{
	u32 grp = 0;
	u32 num_chan_req;
	int err = 0;

	oak_irq_dis_gicu(np, OAK_GICU_HOST_MASK_0,
			 OAK_GICU_HOST_MASK_1 | OAK_GICU_HOST_MASK_E);

	if (np->num_rx_chan < np->num_tx_chan)
		num_chan_req = np->num_tx_chan;
	else
		num_chan_req = np->num_rx_chan;

	if (num_chan_req <= MAX_NUM_OF_CHANNELS) {
		oak_irq_reset_gicu_ldg(np);
		grp = oak_irq_set_tx_rx_dma_bit(np, grp);
		oak_irq_set_tx_rx_err_bit(np, grp);
	} else {
		err = -ENOMEM;
	}

	if (err == 0)
		err = oak_irq_allocate_ivec(np);
	if (err != 0)
		oak_irq_release_ivec(np);

	oakdbg(debug, INTR, "np=%p num_ldg=%d num_chan_req=%d err=%d", np,
	       np->gicu.num_ldg, num_chan_req, err);

	return err;
}

/* Name        : oak_irq_release_ivec
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function reset the MSI interrupt vector structure.
 */
void oak_irq_release_ivec(struct oak_tstruct *np)
{
	u32 i = 0;

	while (i < np->gicu.num_ldg) {
		ldg_t *p = &np->gicu.ldg[i];

		if (((p->msi_tx | p->msi_rx | p->msi_te | p->msi_re |
		      p->msi_ge) != 0)) {
#ifdef OAK_MSIX_LEGACY
			/* legacy kernel will not support pci_irq_vector() */
			synchronize_irq(np->gicu.msi_vec[i].vector);
			irq_set_affinity_hint(np->gicu.msi_vec[i].vector, NULL);
			free_irq(np->gicu.msi_vec[i].vector, &np->gicu.ldg[i]);
#else
			/* Wait for pending IRQ handlers (on other CPUs) -
			 * This function waits for any pending IRQ handlers for
			 * this interrupt to complete before returning
			 */
			synchronize_irq(pci_irq_vector(np->pdev, i));
			irq_set_affinity_hint(pci_irq_vector(np->pdev, i),
					      NULL);
			/* Free an interrupt allocated with request_irq */
			free_irq(pci_irq_vector(np->pdev, i), &np->gicu.ldg[i]);
#endif
			p->msi_tx = 0;
			p->msi_rx = 0;
			p->msi_te = 0;
			p->msi_re = 0;
			p->msi_ge = 0;
		}
		++i;
	}
}

/* Name        : oak_irq_enable_gicu_64
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u64 mask
 * Description : This function set the 64bit GICU mask.
 */
void oak_irq_enable_gicu_64(struct oak_tstruct *np, u64 mask)
{
	u32 val_0 = (u32)(mask & OAK_GICU_HOST_MASK_0);
	u32 val_1 = (u32)((mask >> 32) & OAK_GICU_HOST_MASK_1);

	oakdbg(debug, INTR, "Enable IRQ mask %016llx", mask);

	oak_irq_ena_gicu(np, val_0, val_1);
}

/* Name        : oak_irq_ena_general
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u32 enable
 * Description : This function set the mask which serve general errors
 */
void oak_irq_ena_general(struct oak_tstruct *np, u32 enable)
{
	if (enable != 0)
		enable = (u32)(OAK_UNI_INTR_SEVERE_ERRORS);
	oak_unimac_io_write_32(np, OAK_UNI_IMSK, enable);
}

/* Name        : oak_unimac_enable_tx_rx_channel_irq
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function enables tx and rx channel ring irqs
 */
static void oak_unimac_enable_tx_rx_channel_irq(struct oak_tstruct *np)
{
	u32 i;

	oak_irq_ena_gicu(np, 0, OAK_GICU_HOST_UNIMAC_P11_IRQ);
	i = 0;

	/* Enable IRQ for all the tx channels */
	while (i < np->num_tx_chan) {
		oak_unimac_ena_tx_ring_irq(np, i, 1);
		++i;
	}

	i = 0;

	/* Enable IRQ for all the rx channels */
	while (i < np->num_rx_chan) {
		oak_unimac_ena_rx_ring_irq(np, i, 1);
		++i;
	}
}

/* Name        : oak_irq_enable_groups
 * Returns     : int
 * Parameters  : struct oak_tstruct *np
 * Description : This function enables the group irqs
 */
int oak_irq_enable_groups(struct oak_tstruct *np)
{
	u32 grp = 0;
	u32 irq;
	u64 irq_val;
	int retval = 0;

	/* Map irq bit and enable the group irqs for all the logical device
	 * groups in a loop. Then enable the IRQ on both tx/rx channel.
	 */
	while (grp < np->gicu.num_ldg) {
		ldg_t *p = &np->gicu.ldg[grp];

		p->irq_mask = (p->msi_tx | p->msi_rx | p->msi_te | p->msi_re |
				p->msi_ge);
		p->irq_first = 0;
		p->irq_count = 0;
		irq_val = 1;
		irq = 0;

		/* The below while loop does following
		 * -Map IRQ bit for each group
		 * -Enable the IRQ line
		 */
		while (irq < OAK_MAX_INTR_GRP) {
			if (p->irq_mask & irq_val) {
				if (p->irq_count == 0)
					p->irq_first = irq;
				++p->irq_count;

				oak_unimac_io_write_32(np,
						       OAK_GICU_INTR_GRP(irq),
						grp);
				oakdbg(debug, INTR,
				       "Map IRQ bit %02d => group #%02d (1st=%2d of %2d)",
				       irq, grp, p->irq_first, p->irq_count);
			}
			irq_val <<= 1ULL;
			++irq;
		}
		oak_irq_enable_gicu_64(np, p->irq_mask);
		++grp;
	}

	oak_unimac_enable_tx_rx_channel_irq(np);

	return retval;
}

/* Name        : oak_irq_disable_groups
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function disbles the group irq.
 */
void oak_irq_disable_groups(struct oak_tstruct *np)
{
	u32 i;

	oak_irq_dis_gicu(np, OAK_GICU_HOST_MASK_0, OAK_GICU_HOST_MASK_1);
	i = 0;

	/* Disable IRQ for all rx channels */
	while (i < np->num_rx_chan) {
		oak_unimac_ena_rx_ring_irq(np, i, 0);
		++i;
	}

	i = 0;

	/* Disable IRQ for all tx channels */
	while (i < np->num_tx_chan) {
		oak_unimac_ena_tx_ring_irq(np, i, 0);
		++i;
	}
}

