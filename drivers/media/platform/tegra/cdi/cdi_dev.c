// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <media/cdi-dev.h>
#include <uapi/media/cdi-mgr.h>

#include "cdi-dev-priv.h"
#include "cdi-mgr-priv.h"

/* i2c payload size is only 12 bit */
#define MAX_MSG_SIZE	(0xFFF - 1)

/*#define DEBUG_I2C_TRAFFIC*/

/* CDI Dev Debugfs functions
 *
 *    - cdi_dev_debugfs_init
 *    - cdi_dev_debugfs_remove
 *    - i2c_oft_get
 *    - i2c_oft_set
 *    - i2c_val_get
 *    - i2c_oft_set
 */
static int i2c_val_get(void *data, u64 *val)
{
	struct cdi_dev_info *cdi_dev = data;
	u8 temp = 0;

	if (cdi_dev_raw_rd(cdi_dev, cdi_dev->reg_off, 0, &temp, 1)) {
		dev_err(cdi_dev->dev, "ERR:%s failed\n", __func__);
		return -EIO;
	}
	*val = (u64)temp;
	return 0;
}

static int i2c_val_set(void *data, u64 val)
{
	struct cdi_dev_info *cdi_dev = data;
	u8 temp[3];

	temp[2] = val & 0xff;
	if (cdi_dev_raw_wr(cdi_dev, cdi_dev->reg_off, temp, 1)) {
		dev_err(cdi_dev->dev, "ERR:%s failed\n", __func__);
		return -EIO;
	}
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(cdi_val_fops, i2c_val_get, i2c_val_set, "0x%02llx\n");

static int i2c_oft_get(void *data, u64 *val)
{
	struct cdi_dev_info *cdi_dev = data;

	*val = (u64)cdi_dev->reg_off;
	return 0;
}

static int i2c_oft_set(void *data, u64 val)
{
	struct cdi_dev_info *cdi_dev = data;

	cdi_dev->reg_off = (typeof(cdi_dev->reg_off))val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(cdi_oft_fops, i2c_oft_get, i2c_oft_set, "0x%02llx\n");

int cdi_dev_debugfs_init(struct cdi_dev_info *cdi_dev)
{
	struct cdi_mgr_priv *cdi_mgr = NULL;
	struct dentry *d;

	dev_dbg(cdi_dev->dev, "%s %s\n", __func__, cdi_dev->devname);

	if (cdi_dev->pdata)
		cdi_mgr = dev_get_drvdata(cdi_dev->pdata->pdev);

	cdi_dev->d_entry = debugfs_create_dir(
		cdi_dev->devname,
		cdi_mgr ? cdi_mgr->d_entry : NULL);
	if (cdi_dev->d_entry == NULL) {
		dev_err(cdi_dev->dev, "%s: create dir failed\n", __func__);
		return -ENOMEM;
	}

	d = debugfs_create_file("val", 0644, cdi_dev->d_entry,
		(void *)cdi_dev, &cdi_val_fops);
	if (!d) {
		dev_err(cdi_dev->dev, "%s: create file failed\n", __func__);
		debugfs_remove_recursive(cdi_dev->d_entry);
		cdi_dev->d_entry = NULL;
	}

	d = debugfs_create_file("offset", 0644, cdi_dev->d_entry,
		(void *)cdi_dev, &cdi_oft_fops);
	if (!d) {
		dev_err(cdi_dev->dev, "%s: create file failed\n", __func__);
		debugfs_remove_recursive(cdi_dev->d_entry);
		cdi_dev->d_entry = NULL;
	}

	return 0;
}

int cdi_dev_debugfs_remove(struct cdi_dev_info *cdi_dev)
{
	if (cdi_dev->d_entry == NULL)
		return 0;
	debugfs_remove_recursive(cdi_dev->d_entry);
	cdi_dev->d_entry = NULL;
	return 0;
}

static void cdi_dev_dump(
	const char *str,
	struct cdi_dev_info *info,
	unsigned int offset,
	u8 *buf, size_t size)
{
#if (defined(DEBUG) || defined(DEBUG_I2C_TRAFFIC))
	char *dump_buf;
	int len, i, off;

	/* alloc enough memory for function name + offset + data */
	len = strlen(str) + size * 3 + 10;
	dump_buf = kzalloc(len, GFP_KERNEL);
	if (dump_buf == NULL) {
		dev_err(info->dev, "%s: Memory alloc ERROR!\n", __func__);
		return;
	}

	off = sprintf(dump_buf, "%s %04x =", str, offset);
	for (i = 0; i < size && off < len - 1; i++)
		off += sprintf(dump_buf + off, " %02x", buf[i]);
	dump_buf[off] = 0;
	dev_notice(info->dev, "%s\n", dump_buf);
	kfree(dump_buf);
#endif
}

/* i2c read from device.
   val    - buffer contains data to write.
   size   - number of bytes to be writen to device.
   offset - address in the device's register space to start with.
*/
int cdi_dev_raw_rd(
	struct cdi_dev_info *info, unsigned int offset,
	unsigned int offset_len, u8 *val, size_t size)
{
	int ret = -ENODEV;
	u8 data[2];
	struct i2c_msg i2cmsg[2];

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);
	if (!info->power_is_on) {
		dev_err(info->dev, "%s: power is off.\n", __func__);
		mutex_unlock(&info->mutex);
		return ret;
	}

	/* when user read device from debugfs, the offset_len will be 0.
	 * And the offset_len should come from device info
	 */
	if (!offset_len)
		offset_len = info->reg_len;

	if (offset_len == 2) {
		data[0] = (u8)((offset >> 8) & 0xff);
		data[1] = (u8)(offset & 0xff);
	} else if (offset_len == 1)
		data[0] = (u8)(offset & 0xff);

	i2cmsg[0].addr = info->i2c_client->addr;
	i2cmsg[0].len = offset_len;
	i2cmsg[0].buf = (__u8 *)data;
	i2cmsg[0].flags = I2C_M_NOSTART;

	i2cmsg[1].addr = info->i2c_client->addr;
	i2cmsg[1].flags = I2C_M_RD;
	i2cmsg[1].len = size;
	i2cmsg[1].buf = (__u8 *)val;

	ret = i2c_transfer(info->i2c_client->adapter, i2cmsg, 2);
	if (ret > 0)
		ret = 0;
	mutex_unlock(&info->mutex);

	if (!ret)
		cdi_dev_dump(__func__, info, offset, val, size);

	return ret;
}

/* i2c write to device.
   val    - buffer contains data to write.
   size   - number of bytes to be writen to device.
   offset - address in the device's register space to start with.
		if offset == -1, it will be ignored and no offset
		value will be integrated into the data buffer.
*/
int cdi_dev_raw_wr(
	struct cdi_dev_info *info, unsigned int offset, u8 *val, size_t size)
{
	int ret = -ENODEV;
	u8 *buf_start = NULL;
	struct i2c_msg *i2cmsg;
	unsigned int num_msgs = 0, total_size, i;

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);

	if (size == 0) {
		dev_dbg(info->dev, "%s: size is 0.\n", __func__);
		mutex_unlock(&info->mutex);
		return 0;
	}

	if (!info->power_is_on) {
		dev_err(info->dev, "%s: power is off.\n", __func__);
		mutex_unlock(&info->mutex);
		return ret;
	}

	if (offset != (unsigned int)-1) { /* offset is valid */
		if (info->reg_len == 2) {
			val[0] = (u8)((offset >> 8) & 0xff);
			val[1] = (u8)(offset & 0xff);
			size += 2;
		} else if (info->reg_len == 1) {
			val++;
			val[0] = (u8)(offset & 0xff);
			size += 1;
		} else
			val += 2;
	}

	cdi_dev_dump(__func__, info, offset, val, size);

	num_msgs = size / MAX_MSG_SIZE;
	num_msgs += (size % MAX_MSG_SIZE) ? 1 : 0;

	i2cmsg = kzalloc((sizeof(struct i2c_msg)*num_msgs), GFP_KERNEL);
	if (!i2cmsg) {
		dev_err(info->dev, "%s: failed to allocate memory\n",
			__func__);
		mutex_unlock(&info->mutex);
		return -ENOMEM;
	}

	buf_start = val;
	total_size = size;

	dev_dbg(info->dev, "%s: num_msgs: %d\n", __func__, num_msgs);
	for (i = 0; i < num_msgs; i++) {
		i2cmsg[i].addr = info->i2c_client->addr;
		i2cmsg[i].buf = (__u8 *)buf_start;

		if (i > 0) {
			i2cmsg[i].flags = I2C_M_NOSTART;
		} else {
			i2cmsg[i].flags = 0;
		}

		if (total_size > MAX_MSG_SIZE) {
			i2cmsg[i].len = MAX_MSG_SIZE;
			buf_start += MAX_MSG_SIZE;
			total_size -= MAX_MSG_SIZE;
		} else {
			i2cmsg[i].len = total_size;
		}
		dev_dbg(info->dev, "%s: addr:%x buf:%p, flags:%u len:%u\n",
			__func__, i2cmsg[i].addr, (void *)i2cmsg[i].buf,
			i2cmsg[i].flags, i2cmsg[i].len);
	}

	ret = i2c_transfer(info->i2c_client->adapter, i2cmsg, num_msgs);
	if (ret > 0)
		ret = 0;

	kfree(i2cmsg);
	mutex_unlock(&info->mutex);
	return ret;
}

static int cdi_dev_raw_rw(struct cdi_dev_info *info)
{
	struct cdi_dev_package *pkg = &info->rw_pkg;
	void *u_ptr = (void *)pkg->buffer;
	u8 *buf;
	int ret = -ENODEV;

	dev_dbg(info->dev, "%s\n", __func__);

	buf = kzalloc(pkg->size, GFP_KERNEL);
	if (buf == NULL) {
		dev_err(info->dev, "%s: Unable to allocate memory!\n",
			__func__);
		return -ENOMEM;
	}
	if (pkg->flags & CDI_DEV_PKG_FLAG_WR) {
		/* write to device */
		if (copy_from_user(buf,
			(const void __user *)u_ptr, pkg->size)) {
			dev_err(info->dev, "%s copy_from_user err line %d\n",
				__func__, __LINE__);
			kfree(buf);
			return -EFAULT;
		}
		/* in the user access case, the offset is integrated in the
		   buffer to be transferred, so pass -1 as the offset */
		ret = cdi_dev_raw_wr(info, -1, buf, pkg->size);
	} else {
		/* read from device */
		ret = cdi_dev_raw_rd(info, pkg->offset,
				pkg->offset_len, buf, pkg->size);
		if (!ret && copy_to_user(
			(void __user *)u_ptr, buf, pkg->size)) {
			dev_err(info->dev, "%s copy_to_user err line %d\n",
				__func__, __LINE__);
			ret = -EINVAL;
		}
	}

	kfree(buf);
	return ret;
}

static int cdi_dev_get_package(
	struct cdi_dev_info *info, unsigned long arg)
{
	struct cdi_dev_package pkg;

	if (copy_from_user(&pkg,
		(const void __user *)arg, sizeof(pkg))) {
		dev_err(info->dev, "%s copy_from_user err line %d\n",
			__func__, __LINE__);
		return -EFAULT;
	}
	info->rw_pkg.offset = pkg.offset;
	info->rw_pkg.offset_len = pkg.offset_len;
	info->rw_pkg.size = pkg.size;
	info->rw_pkg.flags = pkg.flags;
	info->rw_pkg.buffer = pkg.buffer;

	if ((void __user *)info->rw_pkg.buffer == NULL) {
		dev_err(info->dev, "%s package buffer NULL\n", __func__);
		return -EINVAL;
	}

	if (!info->rw_pkg.size) {
		dev_err(info->dev, "%s invalid package size %d\n",
			__func__, info->rw_pkg.size);
		return -EINVAL;
	}

	return 0;
}

static int cdi_dev_get_pwr_info(
	struct cdi_dev_info *info,
	void __user *arg)
{
	struct cdi_dev_pwr_ctrl_info pinfo;

	if (copy_from_user(&pinfo, arg, sizeof(pinfo))) {
		dev_err(info->dev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	pinfo.cam_pwr_method = info->cam_pwr_method;
	pinfo.cam_pwr_i2c_addr = info->cam_pwr_i2c_addr;
	memcpy(pinfo.cam_pwr_links, info->max20087.links, sizeof(u8) * MAX_POWER_LINKS_PER_BLOCK);

	if (copy_to_user(arg, &pinfo, sizeof(pinfo))) {
		dev_err(info->dev,
			"%s: failed to copy to user\n", __func__);
		return -EFAULT;
	}
	return 0;
}

static int tca9539_wr(
	struct cdi_dev_info *info, unsigned int offset, u8 val)
{
	int ret = -ENODEV;

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);

	ret = tca9539_raw_wr(info->dev, &info->tca9539, offset, val);

	mutex_unlock(&info->mutex);
	return ret;
}

static int tca9539_rd(
	struct cdi_dev_info *info, unsigned int offset, u8 *val)
{
	int ret = -ENODEV;

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);

	ret = tca9539_raw_rd(info->dev, &info->tca9539, offset, val);

	mutex_unlock(&info->mutex);

	return ret;
}

static int cdi_dev_set_fsync_mux(
	struct cdi_dev_info *info,
	void __user *arg)
{
	u8 val, shift;
	struct cdi_dev_fsync_mux fsync_mux;

	if (copy_from_user(&fsync_mux, arg, sizeof(fsync_mux))) {
		dev_err(info->dev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	if (info->cim_ver == 2U) {
		/* P01:P00 for the camera group B. cam_grp 1.
		 * P03:P02 for the camera group C. cam_grp 2.
		 * P05:P04 for the camera group D. cam_grp 3.
		 */
		if ((fsync_mux.cam_grp > 0U) && (fsync_mux.cam_grp < 4U)) {
			if (tca9539_rd(info, 0x02, &val) != 0)
				return -EFAULT;
			switch (fsync_mux.cam_grp) {
			case 1U:
				shift = 0U;
				break;
			case 2U:
				shift = 2U;
				break;
			case 3U:
				shift = 4U;
				break;
			default:
				shift = 0U;
				break;
			}

			val &= ~(0x3 << shift);
			val |= (fsync_mux.mux_sel << shift);
			if (tca9539_wr(info, 0x02, val) != 0)
				return -EFAULT;
		}
	}

	return 0;
}

static long cdi_dev_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct cdi_dev_info *info = file->private_data;
	int err = 0;

	switch (cmd) {
	case CDI_DEV_IOCTL_RW:
		err = cdi_dev_get_package(info, arg);
		if (err)
			break;

		err = cdi_dev_raw_rw(info);
		break;
	case CDI_DEV_IOCTL_GET_PWR_INFO:
		err = cdi_dev_get_pwr_info(info, (void __user *)arg);
		break;
	case CDI_DEV_IOCTL_FRSYNC_MUX:
		err = cdi_dev_set_fsync_mux(info, (void __user *)arg);
		break;
	default:
		dev_dbg(info->dev, "%s: invalid cmd %x\n", __func__, cmd);
		return -EINVAL;
	}

	return err;
}

static int cdi_dev_open(struct inode *inode, struct file *file)
{
	struct cdi_dev_info *info;

	if (inode == NULL)
		return -ENODEV;

	info = container_of(inode->i_cdev, struct cdi_dev_info, cdev);

	if (atomic_xchg(&info->in_use, 1))
		return -EBUSY;

	file->private_data = info;
	dev_dbg(info->dev, "%s\n", __func__);
	return 0;
}

static int cdi_dev_release(struct inode *inode, struct file *file)
{
	struct cdi_dev_info *info = file->private_data;

	dev_dbg(info->dev, "%s\n", __func__);
	file->private_data = NULL;
	WARN_ON(!atomic_xchg(&info->in_use, 0));
	return 0;
}

static const struct file_operations cdi_dev_fileops = {
	.owner = THIS_MODULE,
	.open = cdi_dev_open,
	.unlocked_ioctl = cdi_dev_ioctl,
	.release = cdi_dev_release,
};

static void cdi_dev_get_cim_ver(struct device_node *np, struct cdi_dev_info *info)
{
	int err = 0;
	struct device_node *child = NULL;
	struct device_node *cim = NULL;
	const char *cim_ver;

	child = of_get_parent(np);
	if (child != NULL) {
		cim = of_get_compatible_child(child,
					"nvidia,cim_ver");
		if (cim != NULL) {
			err = of_property_read_string(cim,
					"cim_ver",
					&cim_ver);
			if (!err) {
				if (!strncmp(cim_ver,
					"cim_ver_a01",
					sizeof("cim_ver_a01"))) {
					dev_info(info->dev,
						"CIM A01\n");
					info->cim_ver = 1U;
				} else {
					dev_info(info->dev,
						"CIM A02\n");
					info->cim_ver = 2U;
				}
			}
		}
	}
}

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int cdi_dev_probe(struct i2c_client *client)
#else
static int cdi_dev_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#endif
{
	struct cdi_dev_info *info;
	struct cdi_mgr_priv *cdi_mgr = NULL;
	struct device *pdev;
	struct device_node *child = NULL, *child_max20087 = NULL;
	struct device_node *child_tca9539 = NULL;
	int err;
	int numLinks = 0;
	int i;

	dev_dbg(&client->dev, "%s: initializing link @%x-%04x\n",
		__func__, client->adapter->nr, client->addr);

	info = devm_kzalloc(
		&client->dev, sizeof(struct cdi_dev_info), GFP_KERNEL);
	if (!info) {
		pr_err("%s: Unable to allocate memory!\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&info->mutex);

	if (client->dev.platform_data) {
		info->pdata = client->dev.platform_data;
		dev_dbg(&client->dev, "pdata: %p\n", info->pdata);
	} else {
		dev_notice(&client->dev, "%s NO platform data\n", __func__);
		return -ENODEV;
	}

	if (info->pdata->np != NULL) {
		cdi_dev_get_cim_ver(info->pdata->np, info);

		child = of_get_child_by_name(info->pdata->np,
				"pwr_ctrl");
		if (child != NULL) {
			if (of_property_read_bool(child,
						"cam-pwr-max20087"))
				info->cam_pwr_method = CAM_PWR_MAX20087;
			else if (of_property_read_bool(child,
						"cam-pwr-nvccp"))
				info->cam_pwr_method = CAM_PWR_NVCCP;
			else if (of_property_read_bool(child,
						"cam-pwr-tps2h160b"))
				info->cam_pwr_method = CAM_PWR_TPS160;
			else
				info->cam_pwr_method = CAM_PWR_NO_PWR;

			/* get the max20087 information */
			child_max20087 = of_get_child_by_name(child, "max20087");
			if (child_max20087 != NULL) {
				err = of_property_read_u32(child_max20087, "i2c-bus",
						&info->max20087.bus);
				if (err) {
					dev_err(&client->dev, "%s: failed to read \"i2c-bus\"\n",
						__func__);
					return err;
				}
				err = of_property_read_u32(child_max20087, "addr",
					&info->max20087.addr);
				if (err || !info->max20087.addr) {
					info->cam_pwr_i2c_addr = 0;
					dev_err(&client->dev, "%s: failed to read \"addr\"\n",
						__func__);
					return err;
				}
				info->cam_pwr_i2c_addr = info->max20087.addr;
				err = of_property_read_u32(child_max20087, "reg_len",
					&info->max20087.reg_len);
				if (err || !info->max20087.reg_len) {
					dev_err(&client->dev, "%s: failed to read \"reg_len\"\n",
						__func__);
					return err;
				}
				err = of_property_read_u32(child_max20087, "dat_len",
					&info->max20087.dat_len);
				if (err || !info->max20087.dat_len) {
					dev_err(&client->dev, "%s: failed to read \"dat_len\"\n",
						__func__);
					return err;
				}
				/* default link mappings */
				for (i = 0; i < MAX_POWER_LINKS_PER_BLOCK; i++) {
					info->max20087.links[i] = i;
				}
				numLinks = of_property_count_elems_of_size(child_max20087, "links",
					sizeof(u32));
				/* Only links with MAX_POWER_LINKS_PER_BLOCK elements is valid */
				if (numLinks == MAX_POWER_LINKS_PER_BLOCK) {
					u32 link;
					for (i = 0; i < numLinks; i++) {
						err = of_property_read_u32_index(child_max20087, "links", i, &link);
						if (err == 0) {
							if (link < MAX_POWER_LINKS_PER_BLOCK) {
								info->max20087.links[i] = (u8) link;
							} else {
								dev_err(&client->dev, "%s: Incorrect camera module index: %d\n",
									__func__, link);
								return -EINVAL;
							}
						}
					}
				}
			}
		}

		if (info->cim_ver == 2U) {
			/* get the I/O expander information */
			child_tca9539 = of_get_child_by_name(child, "tca9539");
			if (child_tca9539 != NULL) {
				err = of_property_read_u32(child_tca9539, "i2c-bus",
						&info->tca9539.bus);
				if (err) {
					dev_err(info->dev,
						"%s: Failed to get I2C bus number, ERROR %d\n",
						__func__, err);
					return -ENODEV;
				}
				err = of_property_read_u32(child_tca9539, "addr",
					&info->tca9539.addr);
				if (err || !info->tca9539.addr) {
					dev_err(info->dev,
						"%s: ERROR %d addr = %d\n",
						__func__, err,
						info->tca9539.addr);
					return -ENODEV;
				}
				err = of_property_read_u32(child_tca9539, "reg_len",
					&info->tca9539.reg_len);
				if (err || !info->tca9539.reg_len) {
					dev_err(info->dev,
						"%s: ERROR %d reg_len = %d\n",
						__func__, err,
						info->tca9539.reg_len);
					return -ENODEV;
				}
				err = of_property_read_u32(child_tca9539, "dat_len",
					&info->tca9539.dat_len);
				if (err || !info->tca9539.dat_len) {
					dev_err(info->dev,
						"%s: ERROR %d dat_len = %d\n",
						__func__, err,
						info->tca9539.dat_len);
					return -ENODEV;
				}
				err = of_property_read_u32(child_tca9539->parent,
						"power_port",
						&info->tca9539.power_port);
				if (err) {
					dev_err(info->dev,
						"%s: ERROR %d power_port = %d\n",
						__func__, err,
						info->tca9539.power_port);
					return -ENODEV;
				}

				info->tca9539.reg_len /= 8;
				info->tca9539.dat_len /= 8;
				info->tca9539.enable = 1;
				info->tca9539.adap = i2c_get_adapter(info->tca9539.bus);
				if (!info->tca9539.adap) {
					dev_err(info->dev, "%s no such i2c bus %d\n",
						__func__, info->tca9539.bus);
					return -ENODEV;
				}
			}
		}
	}

	if (info->pdata->reg_bits)
		info->reg_len = info->pdata->reg_bits / 8;
	else
		info->reg_len = 2;

	if (info->reg_len > 2) {
		dev_err(&client->dev,
			"device offset length invalid: %d\n", info->reg_len);
		devm_kfree(&client->dev, info);
		return -ENODEV;
	}
	info->i2c_client = client;
	info->dev = &client->dev;

	if (info->pdata)
		err = snprintf(info->devname, sizeof(info->devname),
			       "%s", info->pdata->drv_name);
	else
		err = snprintf(info->devname, sizeof(info->devname),
			       "cdi-dev.%u.%02x", client->adapter->nr,
			       client->addr);

	if (err < 0) {
		dev_err(&client->dev,
			"encoding error: %d\n", err);
		devm_kfree(&client->dev, info);
		return err;
	}

	if (info->pdata->pdev == NULL)
		return -ENODEV;

	cdev_init(&info->cdev, &cdi_dev_fileops);
	info->cdev.owner = THIS_MODULE;
	pdev = info->pdata->pdev;

	err = cdev_add(&info->cdev, MKDEV(MAJOR(pdev->devt), client->addr), 1);
	if (err < 0) {
		dev_err(&client->dev,
			"%s: Could not add cdev for %d\n", __func__,
			MKDEV(MAJOR(pdev->devt), client->addr));
		devm_kfree(&client->dev, info);
		return err;
	}

	/* send uevents to udev, it will create /dev node for cdi-mgr */
	info->dev = device_create(pdev->class, &client->dev,
				  info->cdev.dev,
				  info, info->devname);
	if (IS_ERR(info->dev)) {
		info->dev = NULL;
		cdev_del(&info->cdev);
		devm_kfree(&client->dev, info);
		return PTR_ERR(info->dev);
	}

	/* parse the power control method */
	if (info->pdata)
		cdi_mgr = dev_get_drvdata(info->pdata->pdev);

	info->power_is_on = 1;
	i2c_set_clientdata(client, info);
	cdi_dev_debugfs_init(info);
	return 0;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int cdi_dev_remove(struct i2c_client *client)
#else
static void cdi_dev_remove(struct i2c_client *client)
#endif
{
	struct cdi_dev_info *info = i2c_get_clientdata(client);
	struct device *pdev;

	dev_dbg(&client->dev, "%s\n", __func__);
	cdi_dev_debugfs_remove(info);

	/* remove only cdi_dev_info not i2c_client itself */
	pdev = info->pdata->pdev;

	if (info->dev)
		device_destroy(pdev->class, info->cdev.dev);

	if (info->cdev.dev)
		cdev_del(&info->cdev);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

#ifdef CONFIG_PM
static int cdi_dev_suspend(struct device *dev)
{
	struct cdi_dev_info *cdi = (struct cdi_dev_info *)dev_get_drvdata(dev);

	dev_info(dev, "Suspending\n");
	mutex_lock(&cdi->mutex);
	cdi->power_is_on = 0;
	mutex_unlock(&cdi->mutex);

	return 0;
}

static int cdi_dev_resume(struct device *dev)
{
	struct cdi_dev_info *cdi = (struct cdi_dev_info *)dev_get_drvdata(dev);

	dev_info(dev, "Resuming\n");
	mutex_lock(&cdi->mutex);
	cdi->power_is_on = 1;
	mutex_unlock(&cdi->mutex);

	return 0;
}
#endif

static const struct i2c_device_id cdi_dev_id[] = {
	{ "cdi-dev", 0 },
	{ },
};

static const struct dev_pm_ops cdi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(cdi_dev_suspend,
			cdi_dev_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(cdi_dev_suspend,
			cdi_dev_resume)
};

static struct i2c_driver cdi_dev_drv = {
	.driver = {
		.name = "cdi-dev",
		.owner = THIS_MODULE,
		.pm = &cdi_dev_pm_ops,
	},
	.id_table = cdi_dev_id,
	.probe = cdi_dev_probe,
	.remove = cdi_dev_remove,
};

module_i2c_driver(cdi_dev_drv);

MODULE_DESCRIPTION("CDI Generic I2C driver");
MODULE_AUTHOR("Charlie Huang <chahuang@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: cdi_gpio");
