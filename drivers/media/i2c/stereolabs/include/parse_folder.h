/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>

// Function to find the associated video device for a given sub-device name
static int find_video_device_by_subdev_name(struct v4l2_device *v4l2_dev, const char *subdev_name, struct kobject *kobj) {
	struct media_device *mdev;
	struct video_device *vdev;
	struct media_entity *entity;
	char searched_name[32] = "vi-output, ";
	const char *str;
	int dev_id;

	if (IS_ERR_OR_NULL(v4l2_dev))
	{
		pr_err("No v4l2_dev found and associated in tegracam_v4l2subdev_register(..).\n");
		pr_err("This most certainly means that you have an error in your dts,\n");
		pr_err("often related to tegra-camera-platform node, nvcsi or vi node addresses.\n");
		pr_err("It can also be due to status = \"disabled\" properties when using overlays.\n");
		return -ENOENT;
	}

	mdev = v4l2_dev->mdev;

	strcat(searched_name, subdev_name);

	media_device_for_each_entity(entity, mdev) {
		
		if (!IS_ERR_OR_NULL(entity))
		{

			vdev = media_entity_to_video_device(entity);
			if (!IS_ERR_OR_NULL(vdev))
			{
				
				if (!strcmp(searched_name, vdev->name))
				{
				
					if (unlikely(IS_ERR_OR_NULL(vdev->dev.kobj.name)))
					{
						pr_info("null str...\n");
						return -ENODEV;
					}

					str = kobject_name(&vdev->dev.kobj);

					pr_info("Video device name is %s\n", str);

					/* remove the "video" string and get only the device id */
					sscanf(str+5, "%d", &dev_id);

					pr_info("Video device ID is %d\n", dev_id);

					*kobj = vdev->dev.kobj;

					return dev_id;
				}
			}
		}
	}
    return -ENOENT;
}