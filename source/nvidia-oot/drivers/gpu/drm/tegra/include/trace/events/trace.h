#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra

#if !defined(DRM_TEGRA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define DRM_TEGRA_TRACE_H 1

#include <linux/device.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(register_access,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value),
	TP_STRUCT__entry(
		__field(struct device *, dev)
		__field(unsigned int, offset)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->offset = offset;
		__entry->value = value;
	),
	TP_printk("%s %04x %08x", dev_name(__entry->dev), __entry->offset,
		  __entry->value)
);

DEFINE_EVENT(register_access, dc_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dc_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, hdmi_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, hdmi_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, dsi_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dsi_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, dpaux_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dpaux_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, sor_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, sor_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

TRACE_EVENT(job_submit,
	TP_PROTO(struct device *dev, u32 class_id, u32 job_id, u32 num_fences, u64 hw_timestamp),
	TP_ARGS(dev, class_id, job_id, num_fences, hw_timestamp),
	TP_STRUCT__entry(
		__field(struct device *, dev)
		__field(u32, class_id)
		__field(u32, job_id)
		__field(u32, num_fences)
		__field(u64, hw_timestamp)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->class_id = class_id;
		__entry->job_id = job_id;
		__entry->num_fences = num_fences;
		__entry->hw_timestamp = hw_timestamp;
	),
	TP_printk("%s class=%02x id=%u fences=%u ts=%llu",
		dev_name(__entry->dev), __entry->class_id, __entry->job_id,
		__entry->num_fences, __entry->hw_timestamp
	)
);

DECLARE_EVENT_CLASS(job_fence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold),
	TP_STRUCT__entry(
		__field(u32, job_id)
		__field(u32, syncpt_id)
		__field(u32, threshold)
	),
	TP_fast_assign(
		__entry->job_id = job_id;
		__entry->syncpt_id = syncpt_id;
		__entry->threshold = threshold;
	),
	TP_printk("job_id=%u syncpt_id=%u threshold=%u",
		__entry->job_id, __entry->syncpt_id, __entry->threshold
	)
);

DEFINE_EVENT(job_fence, job_prefence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold));

DEFINE_EVENT(job_fence, job_postfence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold));

TRACE_EVENT(job_timestamps,
	TP_PROTO(u32 job_id, u64 begin, u64 end),
	TP_ARGS(job_id, begin, end),
	TP_STRUCT__entry(
		__field(u32, job_id)
		__field(u64, begin)
		__field(u64, end)
	),
	TP_fast_assign(
		__entry->job_id = job_id;
		__entry->begin = begin;
		__entry->end = end;
	),
	TP_printk("job_id=%u begin=%llu end=%llu",
		__entry->job_id, __entry->begin, __entry->end
	)
);

#endif /* DRM_TEGRA_TRACE_H */

/* This part must be outside protection */
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
