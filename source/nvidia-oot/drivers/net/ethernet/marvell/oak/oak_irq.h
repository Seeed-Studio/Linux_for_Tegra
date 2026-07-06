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
#ifndef H_OAK_IRQ
#define H_OAK_IRQ

/* Include for relation to classifier oak_unimac */
#include "oak_unimac.h"

extern u32 debug;
struct oak_tstruct;
/* Name         : oak_irq_request_ivec
 * Returns      : int
 * Parameters   : struct oak_tstruct * np
 *  Description : This function request irq vector
 */
int oak_irq_request_ivec(struct oak_tstruct *np);

/* Name        : oak_irq_release_ivec
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function reset the MSI interrupt vector structure.
 */
void oak_irq_release_ivec(struct oak_tstruct *np);

/* Name        : oak_irq_enable_gicu_64
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u64 mask
 * Description : This function set the 64bit GICU mask.
 */
void oak_irq_enable_gicu_64(struct oak_tstruct *np, u64 mask);

/* Name        : oak_irq_ena_general
 * Returns     : void
 * Parameters  : struct oak_tstruct *np, u32 enable
 * Description : This function set the mask which serve general errors
 */
void oak_irq_ena_general(struct oak_tstruct *np, u32 enable);

/* Name      : enable_groups
 * Returns   : int
 * Parameters:  struct oak_tstruct *np
 * Description : This function enable the group irq
 */
int oak_irq_enable_groups(struct oak_tstruct *np);

/* Name        : oak_irq_disable_groups
 * Returns     : void
 * Parameters  : struct oak_tstruct *np
 * Description : This function disbles the group irq.
 */
void oak_irq_disable_groups(struct oak_tstruct *np);

#endif /* #ifndef H_OAK_IRQ */

