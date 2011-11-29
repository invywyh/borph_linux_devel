/*******************************************************************************
 * ppc405ez_dac.c
 *
 * AMCC DAC device driver for the DAC core on the AMCC 405EZ
 * PowerPC processors.
 *
 * Author: Victor Gallardo <vgallardo@amcc.com>
 * Date: November 2006
 *
 * Copyright 2006 Applied Micro Circuits Corporation
 *
 * Copyright 2007 DENX Software Engineering - Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ******************************************************************************/

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ppc405ez_dac.h>

#include <asm/io.h>
#include <asm/time.h>
#include <asm/uaccess.h>

#define PPC405EZ_DAC_DRV_NAME		"ppc405ez_dac"
#define PPC405EZ_DAC_DRV_VERSION	"0.3"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Gallardo <vgallardo@amcc.com>");
MODULE_DESCRIPTION("AMCC PowerPC 405EZ DAC device driver");
MODULE_VERSION(PPC405EZ_DAC_DRV_VERSION);

#define PPC405EZ_DAC_BASEADDR		0xEF603300
#define PPC405EZ_DAC_IRQ		24

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/
static void ppc405ez_dac_cleanup(void);
static int ppc405ez_dac_ioremap(void);
static int ppc405ez_dac_register_device(void);
static int ppc405ez_dac_request_interrupts(void);

static int ppc405ez_dac_open(struct inode *inode, struct file *file);
static int ppc405ez_dac_close(struct inode *inode, struct file *file);
static int ppc405ez_dac_ioctl(struct inode *inode, struct file *file,
			      unsigned int cmd, unsigned long arg);

/*******************************************************************************
 * File Operations Definition
 ******************************************************************************/
static struct file_operations ppc405ez_dac_fops = {
	.ioctl = ppc405ez_dac_ioctl,
	.open = ppc405ez_dac_open,
	.release = ppc405ez_dac_close
};

/*******************************************************************************
 * Driver static variables
 ******************************************************************************/
static int ppc405ez_dac_registered = 0;
static int ppc405ez_dac_device_open = 0;
static int ppc405ez_dac_irq = 0;
static int dev_id = 0;

static u32 ppc405ez_dac_base;

static struct proc_dir_entry *proc_driver = NULL;
static struct proc_dir_entry *proc_regs = NULL;

static inline void ppc405ez_dac_write(u32 addr, u32 val)
{
	out_be32((unsigned int *)(ppc405ez_dac_base + addr), val);
}

static inline u32 ppc405ez_dac_read(u32 addr)
{
	return (u32) in_be32((unsigned int *)(ppc405ez_dac_base + addr));
}

/*******************************************************************************
 * proc-fs stuff...
 ******************************************************************************/

static int ppc405ez_dac_proc_strcat(char **procBuf, char *snew,
				    unsigned long *offset, int *buflen)
{
	int len = strlen(snew);

	if (*buflen <= 0)
		return 0;

	if (*offset != 0) {
		if (*offset < len) {
			snew += *offset;
			len -= *offset;
			*offset = 0;
		} else {
			*offset -= len;
			return 0;
		}
	}

	if (*buflen < len)
		len = *buflen;

	memcpy(*procBuf, snew, len);
	*buflen -= len;
	*procBuf += len;

	return len;
}

#define ppc405ez_dac_proc_print(reg)					\
	sprintf(tmp_buff,"%-23s : 0x%08X\n",# reg,ppc405ez_dac_read(reg)); \
	bytes += ppc405ez_dac_proc_strcat(&proc_buff,tmp_buff,&offset,&buff_len);

static int ppc405ez_dac_proc_regs(char *buff, char **start,
				  off_t offset, int buff_len)
{
	char tmp_buff[80];	/* used by ppc405ez_dac_proc_print */
	int bytes = 0;		/* used by ppc405ez_dac_proc_print */
	char *proc_buff = buff;	/* used by ppc405ez_dac_proc_print */

	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_0);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_1);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_2);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_3);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_4);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_5);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_6);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_7);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_8);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_9);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_10);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_11);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_12);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_13);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_14);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_BUF_15);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_PTR_INT_EN);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_INT_EN);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_PTR_INT_STAT);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_INT_STAT);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_CFG);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_PTR_CFG);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_PTR_STAT);
	ppc405ez_dac_proc_print(PPC405EZ_DAC_DMAREQ_EN);

	*start = buff;
	return bytes;
}

int ppc405ez_dac_create_proc_entry(void)
{
	proc_driver = proc_mkdir("driver/ppc405ez_dac", NULL);

	if (proc_driver == NULL) {
		printk(KERN_ERR "%s: Error creating proc directory!\n",
		       __func__);
		return -ENOMEM;
	}

	proc_regs = create_proc_info_entry("registers", 0, proc_driver,
					   ppc405ez_dac_proc_regs);

	if (proc_regs == NULL) {
		printk(KERN_ERR "%s: Error while creating proc reg file!\n",
		       __func__);
		return -ENOMEM;
	}

	proc_driver->owner = THIS_MODULE;
	proc_regs->owner = THIS_MODULE;

	return 0;
}

void ppc405ez_dac_remove_proc_entry(void)
{
	if (proc_regs)
		remove_proc_entry("regs", proc_driver);
	if (proc_driver)
		remove_proc_entry("dac", NULL);
}

static int __init ppc405ez_dac_init_module(void)
{
	u32 cpr_perd1;
	int retval = 0;

	pr_debug("%s: initializing\n", __func__);

	if ((retval = ppc405ez_dac_ioremap()) ||
	    (retval = ppc405ez_dac_register_device()) ||
	    (retval = ppc405ez_dac_create_proc_entry()) ||
	    (retval = ppc405ez_dac_request_interrupts())) {
		ppc405ez_dac_cleanup();
		return retval;
	}

	/* reset interrupts */
	ppc405ez_dac_write(PPC405EZ_DAC_INT_STAT, 0xffffffff);
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_INT_STAT, 0xffffffff);

	/*
	 * SET CPR0_PERD1.DACDV = 0xA0
	 */
	mtdcr(DCRN_CPR_CFGADDR, CPR_PERD1);
	cpr_perd1 = mfdcr(DCRN_CPR_CFGDATA);
	cpr_perd1 &= 0x00FFFFFF;
	cpr_perd1 |= 0xA6000000;
	mtdcr(DCRN_CPR_CFGDATA, cpr_perd1);

	printk(KERN_INFO "Registering %s, major=%i, version %s\n", PPC405EZ_DAC_DRV_NAME,
	       PPC405EZ_DAC_MAJOR, PPC405EZ_DAC_DRV_VERSION);

	return 0;
}

static void __exit ppc405ez_dac_cleanup_module(void)
{
	pr_debug("%s: cleaning up\n", __func__);
	ppc405ez_dac_cleanup();
}

static void ppc405ez_dac_cleanup(void)
{
	ppc405ez_dac_remove_proc_entry();

	if (ppc405ez_dac_registered > 0) {
		unregister_chrdev(PPC405EZ_DAC_MAJOR, PPC405EZ_DAC_DRV_NAME);
	}

	if (ppc405ez_dac_base) {
		iounmap((unsigned *)ppc405ez_dac_base);
		ppc405ez_dac_base = 0;
	}

	if (ppc405ez_dac_irq) {
		free_irq(PPC405EZ_DAC_IRQ, &dev_id);
		ppc405ez_dac_irq = 0;
	}
}

static int ppc405ez_dac_register_device(void)
{
	int retval = 0;

	retval = register_chrdev(PPC405EZ_DAC_MAJOR,
				 PPC405EZ_DAC_DRV_NAME, &ppc405ez_dac_fops);
	if (retval < 0)
		printk(KERN_ERR "%s: Error registering device (%d)!\n",
		       __func__, PPC405EZ_DAC_MAJOR);
	else
		ppc405ez_dac_registered = PPC405EZ_DAC_MAJOR;

	return retval;
}

static int ppc405ez_dac_ioremap(void)
{
	ppc405ez_dac_base = (u32) ioremap(PPC405EZ_DAC_BASEADDR, PAGE_SIZE);

	if (ppc405ez_dac_base == 0) {
		printk(KERN_ERR "%s: Error ioremapping device!\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

static irqreturn_t ppc405ez_dac_interrupt(int irq, void *dev_id)
{
	u32 status;

	/*
	 * ToDo: Add special interrupt handling for extended
	 * driver support like DMA etc.
	 */
	status = ppc405ez_dac_read(PPC405EZ_DAC_PTR_INT_STAT);
	if (status != 0) {
		ppc405ez_dac_write(PPC405EZ_DAC_PTR_INT_STAT, status);
	} else {
		status = ppc405ez_dac_read(PPC405EZ_DAC_INT_STAT);
		if (status != 0) {
			ppc405ez_dac_write(PPC405EZ_DAC_INT_STAT, status);
		}
	}

#if 0
	// test-only... PPC405EZ_DAC_BUF_MAX...
#define PPC405EZ_DAC_BUF_MAX     16
	status = ppc405ez_dac_read(PPC405EZ_DAC_PTR_INT_STAT);
	if (status != 0) {
		/* Update DAC Buffers */
		u16 i;
		u32 offset;

		conv_idx++;
		if (conv_idx >= 6)
			conv_idx = 0;

		for (i = 0, offset = PPC405EZ_DAC_BUF_0;
		     i < PPC405EZ_DAC_BUF_MAX; i++, offset += 4)
			ppc405ez_dac_write(offset, conv[conv_idx].buf[i]);

		ppc405ez_dac_write(PPC405EZ_DAC_PTR_INT_STAT, status);
	} else {
		status = ppc405ez_dac_read(PPC405EZ_DAC_INT_STAT);
		if (status != 0)
			ppc405ez_dac_write(PPC405EZ_DAC_INT_STAT, status);
	}
#endif

	return IRQ_HANDLED;
}

static int ppc405ez_dac_request_interrupts(void)
{
	int retval;

	retval = request_irq(PPC405EZ_DAC_IRQ, ppc405ez_dac_interrupt,
			     0, "DAC", &dev_id);

	if (retval) {
		printk(KERN_ERR "%s: Could not get IRQ %d!\n",
		       __func__, PPC405EZ_DAC_IRQ);
		return -EBUSY;
	}

	ppc405ez_dac_irq = PPC405EZ_DAC_IRQ;

	return 0;
}

static int ppc405ez_dac_open(struct inode *inode, struct file *file)
{
	if (ppc405ez_dac_device_open)
		return -EBUSY;

	ppc405ez_dac_device_open++;

	try_module_get(THIS_MODULE);

	return 0;
}

static int ppc405ez_dac_close(struct inode *inode, struct file *file)
{
	ppc405ez_dac_device_open--;

	module_put(THIS_MODULE);

	return 0;
}

static int ppc405ez_put_val(unsigned short val)
{
	union ppc405ez_dac_ptr_cfg ptr_cfg;
	union ppc405ez_dac_cfg cfg;

	/*
	 * write conversion value to buffer (location 0)
	 */
	ppc405ez_dac_write(PPC405EZ_DAC_BUF_0, val << 16);

	/*
	 * set pointer into a round loop
	 */
	ptr_cfg.reg.rollover = 0x0;
	ptr_cfg.reg.ptr_set = 0x0;
	ptr_cfg.reg.ptr_set_en = 0x0;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);
	ptr_cfg.reg.ptr_set_en = 0x1;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);
	ptr_cfg.reg.ptr_set_en = 0x0;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);

	/*
	 * set configuration and trigger
	 */
	cfg.reg.trig_src = 0x0;
	cfg.reg.filter_cnt = 0x1;
	cfg.reg.trig = 0x1;
	cfg.reg.trig_cnt_max = 0xFFFF;
	ppc405ez_dac_write(PPC405EZ_DAC_CFG, cfg.data);

	return 0;
}

static int ppc405ez_dac_ioctl(struct inode *inode, struct file *file,
			      unsigned int cmd, unsigned long arg)
{
	struct dac_reg reg;

	pr_debug("%s: cmd=%x\n", __func__, cmd);

	switch (cmd) {
	case PPC405EZ_DAC_WRITE_REG:
		copy_from_user(&reg, (void *)arg, sizeof(struct dac_reg));
		pr_debug("PPC405EZ_DAC_WRITE_REG: offs=%x val=%x\n",
			 reg.offs, reg.data.val);
		ppc405ez_dac_write(reg.offs, reg.data.val);
		break;

	case PPC405EZ_DAC_PUT:
		pr_debug("PPC405EZ_DAC_PUT: val=%04x\n", (unsigned short)arg);
		ppc405ez_put_val((unsigned short)arg);
		break;

	default:
		return -EBADMSG;
	}

	return 0;
}

#if 0 // test-only
static int ppc405ez_dac_start_test(void)
{
	u16 c, i;
	u32 offset;
	u32 ptr_int_en;

	union ppc405ez_dac_ptr_cfg ptr_cfg;
	union ppc405ez_dac_cfg cfg;

	PPC405EZ_DAC_LOG("ppc405ez_dac_start_test");

	/*
	 * Create 6 conversion buffers patters.
	 * You will be able to tell which pattern we
	 * are on by the letters A,B,C,D,E,F in the
	 * lower 4 bits of the conversion value
	 */

	for (c = 0; c < 6; c++) {
		for (i = 0; i < PPC405EZ_DAC_VAL_MAX; i++) {
			conv[c].val[i] = (i + 1) * 16;
			conv[c].val[i] |= (0xA + c);
		}
	}

	for (i = 0, offset = PPC405EZ_DAC_BUF_0;
	     i < PPC405EZ_DAC_BUF_MAX; i++, offset += 4)
		ppc405ez_dac_write(offset, conv[conv_idx].buf[i]);

	/*
	 * set interrupt
	 */
	ptr_int_en = 0x00000001;	/* BIT 31 */
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_INT_EN, ptr_int_en);

	/*
	 * set pointer into a round loop
	 */
	ptr_cfg.reg.rollover = 0x1F;
	ptr_cfg.reg.ptr_set = 0x15;
	ptr_cfg.reg.ptr_set_en = 0x0;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);
	ptr_cfg.reg.ptr_set_en = 0x1;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);
	ptr_cfg.reg.ptr_set_en = 0x0;
	ppc405ez_dac_write(PPC405EZ_DAC_PTR_CFG, ptr_cfg.data);

	/*
	 * set configuration
	 */
	cfg.reg.trig_src = 0x3;
	cfg.reg.filter_cnt = 0x1;
	cfg.reg.trig = 0x0;
	cfg.reg.trig_cnt_max = 0xFFFF;
	ppc405ez_dac_write(PPC405EZ_DAC_CFG, cfg.data);

	return 0;
}
#endif

module_init(ppc405ez_dac_init_module);
module_exit(ppc405ez_dac_cleanup_module);
