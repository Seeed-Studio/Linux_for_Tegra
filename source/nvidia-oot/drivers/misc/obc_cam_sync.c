#include "obc_cam_sync.h"

static const struct debugfs_reg32 tsc_signal_generator_debugfs_regset[] = {
	{
		.name = "status",
		.offset = TSC_GENX_STATUS,
	},
};
#define TSC_SIG_GEN_DEBUGFS_REGSET_SIZE ARRAY_SIZE(tsc_signal_generator_debugfs_regset)

static inline void cdi_tsc_generator_writel(struct tsc_signal_generator *generator, u32 reg, u32 val)
{
	writel(val, generator->base + reg);
}

static inline u32 cdi_tsc_generator_readl(struct tsc_signal_generator *generator, u32 reg)
{
	return readl(generator->base + reg);
}

static inline u32 cdi_tsc_controller_readl(struct tsc_signal_controller *controller, u32 reg)
{
	return readl(controller->base + reg);
}
//--------------------------------------------------------------------------


int m_lock = 0;

//--------------------------------------------------------------------------
static int debug_en = 0;
module_param(debug_en, int, 0644);

//--------------------------------------------------------------------------

static int cdi_tsc_find_and_add_generators(struct tsc_signal_controller *controller)
{
	struct tsc_signal_generator *generator;
	struct device_node *np;
	struct resource res;
	const char *node_status;
	int err;

	for_each_child_of_node(controller->dev->of_node, np) {
		err = of_property_read_string(np, "status", &node_status);
		if (err != 0) {
			dev_err(controller->dev, "Failed to read generator status: %d\n", err);
			return err;
		}
		if (strcmp("okay", node_status) != 0) {
			dev_dbg(controller->dev, "Generator %s disabled - skipping\n", np->full_name);
			continue;
		}


		dev_dbg(controller->dev, "Generator found: %s\n", np->full_name);

		generator = devm_kzalloc(controller->dev, sizeof(*generator), GFP_KERNEL);
		if (!generator)
			return -ENOMEM;

		/* Read pinmux gpio from DT */
		generator->config.gpio_pinmux = of_get_named_gpio(np, "gpio_pinmux", 0);

		generator->of = np;
		INIT_LIST_HEAD(&generator->list);

		if (of_address_to_resource(np, 0, &res))
			return -EINVAL;

		generator->base = devm_ioremap_resource(controller->dev, &res);
		if (IS_ERR(generator->base))
			return PTR_ERR(generator->base);

		err = of_property_read_u32(np, "freq_hz", &generator->config.freq_hz);
		if (err != 0) {
			dev_err(controller->dev, "Failed to read generator frequency: %d\n", err);
			return err;
		}
        generator->config.freq_hz = generator->config.freq_hz * TSC_FPS_SCALE;
		if (generator->config.freq_hz == 0) {
			dev_err(controller->dev, "Frequency must be non-zero\n");
			return -EINVAL;
		}

		err = of_property_read_u32(np, "duty_cycle", &generator->config.duty_cycle);
		if (err != 0) {
			dev_err(controller->dev, "Failed to read generator duty cycle: %d\n", err);
			return err;
		}
		if (generator->config.duty_cycle >= 100) {
			dev_err(controller->dev, "Duty cycle must be < 100%%\n");
			return -EINVAL;
		}

		err = of_property_read_u32(np, "offset_ms", &generator->config.offset_ms);
		if (err != 0) {
			dev_err(controller->dev, "Failed to read generator offset: %d\n", err);
			return err;
		}

		list_add_tail(&generator->list, &cam_sync->controller.generators);
		dev_dbg(controller->dev, "Generator %s added to controller\n", np->full_name);
	}

	return 0;
}

static int cdi_tsc_program_generator_edges(struct tsc_signal_controller *controller)
{
	struct tsc_signal_generator *generator;
	
	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		u32 ticks_in_period = 0;
		u32 ticks_active = 0;
		u32 ticks_inactive = 0;

		ticks_in_period = DIV_ROUND_CLOSEST(TSC_TICKS_PER_HZ * TSC_FPS_SCALE, generator->config.freq_hz);
		
		ticks_active = TSC_TICKS_PER_HZ/750; //1.5ms
		ticks_inactive = ticks_in_period - ticks_active;

		cdi_tsc_generator_writel(generator, TSC_GENX_EDGE0,
			TSC_GENX_EDGEX_TOGGLE |
			FIELD_PREP(TSC_GENX_EDGEX_OFFSET, ticks_active));

		cdi_tsc_generator_writel(generator, TSC_GENX_EDGE1,
			TSC_GENX_EDGEX_TOGGLE |
			TSC_GENX_EDGEX_LOOP |
			FIELD_PREP(TSC_GENX_EDGEX_OFFSET, ticks_inactive));
	}

	return 0;
}

static void cdi_tsc_program_generator_start_values(struct tsc_signal_controller *controller)
{
	const u32 relative_ticks_to_start = mult_frac(
		TSC_GENX_START_OFFSET_MS, NS_PER_MS, TSC_NS_PER_TICK);

	const u32 current_ticks_lo = FIELD_GET(TSC_MTSCCNTCV0_CV,
		cdi_tsc_controller_readl(controller, TSC_MTSCCNTCV0));
	const u32 current_ticks_hi = FIELD_GET(TSC_MTSCCNTCV1_CV,
		cdi_tsc_controller_readl(controller, TSC_MTSCCNTCV1));

	const u64 current_ticks = ((u64)current_ticks_hi << 32) | current_ticks_lo;

	struct tsc_signal_generator *generator;

	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		u64 absolute_ticks_to_start = current_ticks + relative_ticks_to_start;

		if (generator->config.offset_ms != 0) {
			absolute_ticks_to_start += mult_frac(generator->config.offset_ms, NS_PER_MS, TSC_NS_PER_TICK);
		}

		cdi_tsc_generator_writel(generator, TSC_GENX_START0,
			FIELD_PREP(TSC_GENX_START0_LSB_VAL, lower_32_bits(absolute_ticks_to_start)));

		cdi_tsc_generator_writel(generator, TSC_GENX_START1,
			FIELD_PREP(TSC_GENX_START1_MSB_VAL, upper_32_bits(absolute_ticks_to_start)));
	}
}

static bool cdi_tsc_generator_is_running(struct tsc_signal_generator *generator)
{
	const u32 status = cdi_tsc_generator_readl(generator, TSC_GENX_STATUS);

	return FIELD_GET(TSC_GENX_STATUS_RUNNING, status) == 1;
}

static bool cdi_tsc_generator_is_waiting(struct tsc_signal_generator *generator)
{
	const u32 status = cdi_tsc_generator_readl(generator, TSC_GENX_STATUS);

	return FIELD_GET(TSC_GENX_STATUS_WAITING, status) == 1;
}

static inline bool cdi_tsc_generator_is_idle(struct tsc_signal_generator *generator)
{
	return !cdi_tsc_generator_is_running(generator) &&
		!cdi_tsc_generator_is_waiting(generator);
}

static int cdi_tsc_start_generators(struct tsc_signal_controller *controller)
{
	struct tsc_signal_generator *generator;
	int err;

	dev_info(controller->dev, "%s: Starting TSC Gen ...\n", __func__);

	/* A generator must be idle (e.g. neither running nor waiting) before starting */
	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		/* Try to free the gpio, so it can act as TSC */
		gpio_free(generator->config.gpio_pinmux);
		if (!cdi_tsc_generator_is_idle(generator)) {
			dev_err(controller->dev, "Generator %s is not idle\n", generator->of->full_name);
			return -EBUSY;
		}
	}

	err = cdi_tsc_program_generator_edges(controller);
	if (err != 0)
		return err;

	cdi_tsc_program_generator_start_values(controller);

	/* Start the generators */
	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		cdi_tsc_generator_writel(generator, TSC_GENX_CTRL,
			TSC_GENX_CTRL_INITIAL_VAL | TSC_GENX_CTRL_ENABLE);
	}

	return 0;
}

static int cdi_tsc_stop_generators(struct tsc_signal_controller *controller)
{
	struct tsc_signal_generator *generator;

	dev_info(controller->dev, "%s: TSC Gen Stop ...\n", __func__);
	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		cdi_tsc_generator_writel(generator, TSC_GENX_CTRL, TSC_GENX_CTRL_RST);

		/* Ensure the generator has stopped */
		if (!cdi_tsc_generator_is_idle(generator)) {
			dev_err(controller->dev, "Generator %s failed to stop\n",
				generator->of->full_name);
			return -EIO;
		}
		/* To avoid pin state in "high", When tsc gen stopped. Which inturn causes
		 * inconsistency in sensor streaming */
		gpio_request(generator->config.gpio_pinmux, "tsc-gen");
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int cdi_tsc_debugfs_init(struct tsc_signal_controller *controller)
{
	struct tsc_signal_generator *generator;

	controller->debugfs.d =
		debugfs_create_dir(controller->dev->of_node->full_name, NULL);
	if (IS_ERR(controller->debugfs.d))
		return PTR_ERR(controller->debugfs.d);

	list_for_each_entry(generator, &cam_sync->controller.generators, list) {
		generator->debugfs.regset_ro.regs = tsc_signal_generator_debugfs_regset;
		generator->debugfs.regset_ro.nregs = TSC_SIG_GEN_DEBUGFS_REGSET_SIZE;
		generator->debugfs.regset_ro.base = generator->base;

		debugfs_create_regset32(
				generator->of->full_name,
				0400,
				controller->debugfs.d,
				&generator->debugfs.regset_ro);
	}

	return 0;
}

static void cdi_tsc_debugfs_remove(struct tsc_signal_controller *controller)
{
	debugfs_remove_recursive(controller->debugfs.d);
	controller->debugfs.d = NULL;
}
#endif

static void sync_enable_irq(void)
{
    if (cam_sync->irq_enabled == false)
    {
        enable_irq(cam_sync->irq);
        cam_sync->irq_enabled = true;
    }
}

static void sync_disable_irq(void)
{
    if (cam_sync->irq_enabled == true)
    {
        disable_irq(cam_sync->irq);
        cam_sync->irq_enabled = false;
    }
}

static int set_pwm_period(cam_sync_t *cam_sync, unsigned long period)
{
    int ret = 0;
    struct pwm_state *state = &cam_sync->pwm_state;

    mutex_lock(&cam_sync->lock);

    if (period > cam_sync->pwm_state.duty_cycle)
    {
        state->enabled = true;
    }
    else
    {
        dev_err(cam_sync->dev, "pwm_period is too short\n");
        goto exit_set_pwm_err;
    }
    state->period = period;
    ret = pwm_apply_state(cam_sync->pwm, state);
    if (ret)
        dev_err(cam_sync->dev, "set pwm_period err \n");
    else
        cam_sync->pwm_period = period;
    dev_info(cam_sync->dev, "set pwm_period to %lldns\n", state->period);
exit_set_pwm_err:
    mutex_unlock(&cam_sync->lock);
    return ret;
}

static void cam_sync_stop(void)
{
    sync_disable_irq();

    cdi_tsc_stop_generators(&cam_sync->controller);
    if (cam_sync->trigger_type == GPIO_TRIGGER)
        hrtimer_cancel(&cam_sync->trigger_timer);
    else if (cam_sync->trigger_type == PWM_TRIGGER)
        pwm_disable(cam_sync->pwm);
    if (cam_sync->sync_in_gpios > 0 && cam_sync->sync_out_gpios > 0)
        del_timer(&cam_sync->delay_timer);
    if (cam_sync->sync_out_gpios > 0)
        gpio_set_value(cam_sync->sync_out_gpios, 1);

    dev_info(cam_sync->dev, "cam sync close\n");
}

static void cam_sync_set_mode(cs_param_t *param)
{
    struct tsc_signal_generator *generator;
    unsigned long period;

    if (cam_sync->sync_out_gpios > 0)
        gpio_set_value(cam_sync->sync_out_gpios, 0);

    if (m_lock == 1)
    {
        m_lock = 0;
        del_timer(&cam_sync->delay_timer);
    }

    if (param->mode == MODE_SYNC_IN)
    {
        if (cam_sync->trigger_type == GPIO_TRIGGER)
            hrtimer_cancel(&cam_sync->trigger_timer);
        else if (cam_sync->trigger_type == PWM_TRIGGER)
            pwm_disable(cam_sync->pwm);
        sync_enable_irq();
        dev_info(cam_sync->dev, "sync in trigger mode\n");
    }
    else
    {
        int offset = SYNC_HWTIMER_OFFSET_TIME;

        switch (param->fps)
        {
        case 6000:
            if (cam_sync->trigger_type == GPIO_TRIGGER)
                offset = 30;
            else if (cam_sync->trigger_type == PWM_TRIGGER)
                offset = 50;
            else
                offset = 20;
            break;

        case 3000:
            offset = 50;
            break;

        case 1500:
            offset = 50;
            break;

        case 2000:
            offset = 65;
            break;

        case 1000:
            offset = 50;
            break;

        case 600:
            offset = 50;
            break;

        case 500:
            offset = 50;
            break;

        default:
            offset = SYNC_HWTIMER_OFFSET_TIME;
            break;
        }
        sync_disable_irq();
        
        list_for_each_entry(generator, &cam_sync->controller.generators, list) {
            generator->config.freq_hz = (1000000 * TSC_FPS_SCALE) / (1000000 * FPS_SCALE / param->fps + offset);
            dev_info(cam_sync->dev, "freq_hz:%d\n", generator->config.freq_hz);
        }
        cdi_tsc_start_generators(&cam_sync->controller);

        cam_sync->ldelay = 1000000 * FPS_SCALE / param->fps - cam_sync->hdelay + offset; // us

        if (cam_sync->trigger_type == GPIO_TRIGGER)
        {
            ktime_t kt = ktime_set(0, cam_sync->ldelay * 1000); // ns
            cam_sync->is_high = 0;
            hrtimer_start(&cam_sync->trigger_timer, kt, HRTIMER_MODE_REL_PINNED_HARD);
        }
        else if (cam_sync->trigger_type == PWM_TRIGGER)
        {
            period = (cam_sync->ldelay + cam_sync->hdelay) * 1000;
            set_pwm_period(cam_sync, period);
            // dev_info(cam_sync->dev, "sync out mode pwm_period :%ldns\n", period);
        }
    }
}

static ssize_t cam_sync_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    int ret;
    // cam_sync_t *cam_sync = container_of(file->private_data, cam_sync_t, misc_dev);
    dev_info(cam_sync->dev, "cam-sync read:%d, %d\n", cam_sync->param.mode, cam_sync->param.fps);

    ret = copy_to_user(buf, &cam_sync->param, sizeof(cam_sync->param));
    if (ret)
    {
        dev_info(cam_sync->dev, "copy_to_user err:%d\n", ret);
        return ret;
    }
    return 0;
}

static ssize_t cam_sync_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    // cam_sync_t *cam_sync = container_of(file->private_data, cam_sync_t, misc_dev);
    cs_param_t param;
    int ret = copy_from_user(&param, buf, sizeof(param));
    if (ret)
    {
        dev_info(cam_sync->dev, "copy_from_user err:%d\n", ret);
        return ret;
    }
    dev_info(cam_sync->dev, "cam-sync write:%d, %d\n", param.mode, param.fps);

    if (param.mode > 2 || param.fps < 1 * FPS_SCALE || param.fps > 120 * FPS_SCALE) // 1~120fps
        return -1;

    cam_sync_set_mode(&param);
    cam_sync->param = param;

    return 0;
}

static long cam_sync_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    if (cmd == CAM_SYNC_START)
    {
        cs_param_t param;
        int ret = copy_from_user(&param, (void *)arg, sizeof(param));
        if (ret)
        {
            dev_info(cam_sync->dev, "copy_from_user err:%d\n", ret);
            return ret;
        }
        cam_sync_set_mode(&param);
        dev_info(cam_sync->dev, "cam sync start\n");
    }
    else if (cmd == CAM_SYNC_STOP)
    {
        cam_sync_stop();
        dev_info(cam_sync->dev, "cam sync stop\n");
    }
    else
    {
        return -1;
    }

    return 0;
}


int cam_sync_ioctl_for_kernel(unsigned int cmd, cs_param_t param)
{
    if (cmd == CAM_SYNC_START)
    {
        cam_sync_set_mode(&param);
        dev_info(cam_sync->dev, "cam sync start\n");
    }
    else if (cmd == CAM_SYNC_STOP)
    {
        cam_sync_stop();
        dev_info(cam_sync->dev, "cam sync stop\n");
    }
    else
    {
        return -1;
    }

    return 0;
}
EXPORT_SYMBOL(cam_sync_ioctl_for_kernel);

static int cam_sync_open(struct inode *inode, struct file *file)
{
    dev_info(cam_sync->dev, "cam sync open\n");
    return 0;
}

static int cam_sync_release(struct inode *inode, struct file *file)
{
    // cam_sync_t *cam_sync = container_of(file->private_data, cam_sync_t, misc_dev);
    cam_sync_stop();
    return 0;
}

static const struct file_operations cam_sync_fops = {
    .owner = THIS_MODULE,
    .llseek = no_llseek,
    .write = cam_sync_write,
    .read = cam_sync_read,
    .unlocked_ioctl = cam_sync_ioctl,
    .open = cam_sync_open,
    .release = cam_sync_release,
};

static enum hrtimer_restart cam_sync_hrtimer_irq(struct hrtimer *timer)
{
    ktime_t kt;

    gpio_set_value(cam_sync->sync_out_gpios, cam_sync->is_high ? 1 : 0);

    kt = ktime_set(0, (cam_sync->is_high ? cam_sync->hdelay : cam_sync->ldelay) * 1000); // ns
    hrtimer_forward(timer, timer->base->get_time(), kt);                                 // hrtimer_forward(trigger_timer, now, tick_period);
    cam_sync->is_high ^= 1;
    dev_dbg(cam_sync->dev, "htim\n");
    return HRTIMER_RESTART; // HRTIMER_NORESTART
}

static void cam_sync_in_timer_irq(struct timer_list *timer)
{
    gpio_set_value(cam_sync->sync_out_gpios, 0);
    del_timer(timer);
    m_lock = 0;
    dev_dbg(cam_sync->dev, "tim\n");
}

static irqreturn_t cam_sync_in_trigger_irq(int irq, void *data)
{
    if (m_lock)
        return IRQ_HANDLED;
    m_lock = 1;

    gpio_set_value(cam_sync->sync_out_gpios, 1);

    dev_dbg(cam_sync->dev, "tri\n");

    /* start trigger_timer */
    cam_sync->delay_timer.expires = jiffies + DELAY_TIME;
    add_timer(&cam_sync->delay_timer);

    return IRQ_HANDLED;
}

static struct miscdevice csync_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "camsync",
    .fops = &cam_sync_fops,
};

static int obc_cam_sync_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
	struct resource *res;
    int err = 0;
    struct device_node *node = dev->of_node;
    cam_sync = devm_kzalloc(dev, sizeof(cam_sync_t), GFP_KERNEL);
    if (!cam_sync)
    {
        dev_err(dev, "failed to malloc cam_sync_t\n");
        return -ENOMEM;
    }
    mutex_init(&cam_sync->lock);
    cam_sync->dev = dev;
    cam_sync->controller.opened = false;
	cam_sync->controller.dev = dev;

    INIT_LIST_HEAD(&cam_sync->controller.generators);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cam_sync->controller.base = devm_ioremap_resource(dev, res);
	if (IS_ERR(cam_sync->controller.base))
		return PTR_ERR(cam_sync->controller.base);

	err = cdi_tsc_find_and_add_generators(&cam_sync->controller);
	if (err != 0)
		return err;

#ifdef CONFIG_DEBUG_FS
	err = cdi_tsc_debugfs_init(&cam_sync->controller);
	if (err != 0)
		return err;
#endif

    cam_sync->sync_in_gpios = of_get_named_gpio(node, "sync-in-gpios", 0);
    if (cam_sync->sync_in_gpios < 0)
    {
        dev_info(dev, "sync-in-gpios not found\n");
    }

    cam_sync->sync_out_gpios = of_get_named_gpio(node, "sync-out-gpios", 0);
    if (cam_sync->sync_out_gpios < 0)
    {
        dev_info(dev, "sync-out-gpios not found\n");
    }
    else
    {
        gpio_request(cam_sync->sync_out_gpios, "sync-out");
        gpio_direction_output(cam_sync->sync_out_gpios, 0);
        cam_sync->hdelay = 1000;  // 1ms
        cam_sync->ldelay = 32333; // 32.333ms
        cam_sync->trigger_type = GPIO_TRIGGER;

        hrtimer_init(&cam_sync->trigger_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED_HARD);
        cam_sync->trigger_timer.function = cam_sync_hrtimer_irq;
        hrtimer_cancel(&cam_sync->trigger_timer);
    }

    if (cam_sync->sync_in_gpios > 0 && cam_sync->sync_out_gpios > 0)
    {
        gpio_request(cam_sync->sync_in_gpios, "sync-in");
        gpio_direction_input(cam_sync->sync_in_gpios);
        cam_sync->irq = gpio_to_irq(cam_sync->sync_in_gpios);
        dev_info(cam_sync->dev, "gpio to irq: %d -> %d\n", cam_sync->sync_in_gpios, cam_sync->irq);

        // disable_irq(cam_sync->irq);
        cam_sync->irq_enabled = false;
        timer_setup(&cam_sync->delay_timer, cam_sync_in_timer_irq, 0);
        err = request_irq(cam_sync->irq, cam_sync_in_trigger_irq, IRQF_TRIGGER_RISING, "cam_sync_in_trigger_irq", NULL);
        if (err)
        {
            dev_info(cam_sync->dev, "failed to request cam_sync_in_trigger_irq\n");
            return err;
        }
    }

    cam_sync->pwm = devm_of_pwm_get(dev, dev->of_node, NULL);
    if (IS_ERR(cam_sync->pwm))
        dev_info(dev, "Could not get PWM\n");
    else
    {
        cam_sync->trigger_type = PWM_TRIGGER;
        pwm_init_state(cam_sync->pwm, &cam_sync->pwm_state);
        cam_sync->pwm_state.duty_cycle = 1000000; // 1ms
        pwm_apply_state(cam_sync->pwm, &cam_sync->pwm_state);
    }
    /*
    if (cam_sync->sync_out_gpios < 0 && IS_ERR(cam_sync->pwm))
        return -EINVAL;
    */

    platform_set_drvdata(pdev, cam_sync);

    err = misc_register(&csync_misc_device);
    if (err)
    {
        dev_info(cam_sync->dev, "misc_register failed\n");
        return err;
    }

    dev_info(cam_sync->dev, "camsync misc register success!\n");

    return 0;
}

static int obc_cam_sync_remove(struct platform_device *pdev)
{
    // misc_deregister(&cam_sync->misc_dev);
    misc_deregister(&csync_misc_device);
    cdi_tsc_debugfs_remove(&cam_sync->controller);
    free_irq(cam_sync->irq, NULL);
    if (cam_sync->sync_in_gpios > 0)
        gpio_free(cam_sync->sync_in_gpios);
    if (cam_sync->sync_out_gpios > 0)
        gpio_free(cam_sync->sync_out_gpios);
    if (cam_sync->pwm != NULL)
        pwm_free(cam_sync->pwm);
    kfree(cam_sync);
    return 0;
}

static const struct of_device_id of_obc_cam_sync_match[] = {
    {
        .compatible = "orbbec,obc_cam_sync",
    },
    {},
};
MODULE_DEVICE_TABLE(of, of_obc_cam_sync_match);

static struct platform_driver obc_cam_sync_driver = {
    .probe = obc_cam_sync_probe,
    .remove = obc_cam_sync_remove,
    .driver = {
        .name = "obc_cam_sync",
        .of_match_table = of_obc_cam_sync_match,
    },
};

module_platform_driver(obc_cam_sync_driver);

MODULE_AUTHOR("xuanyuan@orbbec.com");
MODULE_AUTHOR("yanxiao@orbbec.com");
MODULE_DESCRIPTION(" orbbec mult-device-sync trigger");
MODULE_LICENSE("GPL");