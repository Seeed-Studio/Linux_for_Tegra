#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <asm/ioctl.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pwm.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

//--------------------------------------------------------------------------
#define DELAY_TIME  1 // 20ms

#define FPS_SCALE   100 //0.01 fps
#define TSC_FPS_SCALE   1000 //0.0001 fps

#define SYNC_HWTIMER_OFFSET_TIME 100 // 100us

//--------------------------------------------------------------------------
#define CAM_SYNC_START  _IOW('c', 1, int)
#define CAM_SYNC_STOP   _IOW('c', 2, int)

//--------------------------------------------------------------------------


//--------------------------------------------------------------------------
#define TSC_TICKS_PER_HZ			(31250000ULL)
#define TSC_NS_PER_TICK				(32)
#define NS_PER_MS				(1000000U)

#define TSC_MTSCCNTCV0				(0x10)
#define TSC_MTSCCNTCV0_CV			GENMASK(31, 0)

#define TSC_MTSCCNTCV1				(0x14)
#define TSC_MTSCCNTCV1_CV			GENMASK(31, 0)

#define TSC_GENX_CTRL				(0x00)
#define TSC_GENX_CTRL_RST			(0x00)
#define TSC_GENX_CTRL_INITIAL_VAL		BIT(1)
#define TSC_GENX_CTRL_ENABLE			BIT(0)

#define TSC_GENX_START0				(0x04)
#define TSC_GENX_START0_LSB_VAL			GENMASK(31, 0)

#define TSC_GENX_START1				(0x08)
#define TSC_GENX_START1_MSB_VAL			GENMASK(23, 0)

#define TSC_GENX_STATUS				(0x0C)
#define TSC_GENX_STATUS_INTERRUPT_STATUS	BIT(6)
#define TSC_GENX_STATUS_VALUE			BIT(5)
#define TSC_GENX_STATUS_EDGE_ID			GENMASK(4, 2)
#define TSC_GENX_STATUS_RUNNING			BIT(1)
#define TSC_GENX_STATUS_WAITING			BIT(0)

#define TSC_GENX_EDGE0				(0x18)
#define TSC_GENX_EDGE1				(0x1C)
#define TSC_GENX_EDGE2				(0x20)
#define TSC_GENX_EDGE3				(0x24)
#define TSC_GENX_EDGE4				(0x28)
#define TSC_GENX_EDGE5				(0x2C)
#define TSC_GENX_EDGE6				(0x30)
#define TSC_GENX_EDGE7				(0x34)

#define TSC_GENX_EDGEX_INTERRUPT_EN		BIT(31)
#define TSC_GENX_EDGEX_STOP			BIT(30)
#define TSC_GENX_EDGEX_TOGGLE			BIT(29)
#define TSC_GENX_EDGEX_LOOP			BIT(28)
#define TSC_GENX_EDGEX_OFFSET			GENMASK(27, 0)

/* Time (ms) offset for the TSC signal generators */
#define TSC_GENX_START_OFFSET_MS		(100)

/**
 * struct tsc_signal_generator : Generator context.
 * @base: ioremapped register base.
 * @of: Generator device node.
 * @config:
 *   @freq_hz: Frequency (hz) of the generator.
 *   @duty_cycle: Duty cycle (%) of the generator.
 *   @offset_ms: Offset (ms) to shift the signal by.
 * @debugfs:
 *   @regset_ro: Debug FS read-only register set.
 * @list: List node
 */
struct tsc_signal_generator {
	void __iomem *base;
	struct device_node *of;
	struct {
		u32 freq_hz;
		u32 duty_cycle;
		u32 offset_ms;
		u32 gpio_pinmux;
	} config;
	struct {
		struct debugfs_regset32 regset_ro;
	} debugfs;
	struct list_head list;
};

/**
 * struct tsc_signal_controller : Controller context
 * @dev: device.
 * @base: ioremapped register base.
 * @debugfs:
 *   @d: dentry to debugfs directory.
 * @generators: Linked list of child generators.
 */
struct tsc_signal_controller {
	struct device *dev;
	void __iomem *base;
	struct {
		struct dentry *d;
	} debugfs;
	struct list_head generators;
	bool opened;

};


typedef enum
{
    MODE_SYNC_IN,
    MODE_SYNC_OUT
} cs_mode_e;

typedef enum
{
    NO_TRIGGER,
    GPIO_TRIGGER,
    PWM_TRIGGER
} trigger_type_t;

typedef struct
{
    uint8_t mode;
    uint16_t fps; 
} cs_param_t;

typedef struct
{
    struct mutex lock;
    struct tsc_signal_controller controller;
    struct device *dev;
    struct regulator *reg;
    struct pwm_device *pwm;
    struct pwm_state pwm_state;
    struct hrtimer trigger_timer;
    struct timer_list delay_timer;
    cs_param_t param;
    unsigned long pwm_period;
    unsigned long pwm_value;
    uint32_t trigger_type;
    uint32_t irq; /* IRQ number */
    unsigned long hdelay;
    unsigned long ldelay;
    int sync_in_gpios;
    int sync_out_gpios;
    bool irq_enabled;
    uint8_t is_high;
    // ktime_t kt;
} cam_sync_t;

cam_sync_t *cam_sync = NULL;
int use_fsycn_single_device_number = 0;
int cam_sync_ioctl_for_kernel(unsigned int cmd, cs_param_t param);