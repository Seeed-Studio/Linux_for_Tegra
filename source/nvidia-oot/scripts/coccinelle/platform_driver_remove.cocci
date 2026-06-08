// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// Options: --no-includes --include-headers --smpl-spacing

@match1@
identifier p, removefn;
@@
struct platform_driver p = {
	.remove = removefn,
};

@match2@
identifier p, removefn;
@@
struct platform_driver p = {
	.remove = __exit_p(removefn),
};

@match3@
@@
#include <nvidia/conftest.h>

@fix1 depends on match1@
identifier match1.p;
identifier match1.removefn;
fresh identifier removefn_wrapper = removefn ## "_wrapper";
@@
+#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
+static void removefn_wrapper(struct platform_device *pdev)
+{
+	removefn(pdev);
+}
+#else
+static int removefn_wrapper(struct platform_device *pdev)
+{
+	return removefn(pdev);
+}
+#endif
+
struct platform_driver p = {
-	.remove = removefn,
+	.remove = removefn_wrapper,
};

@fix2 depends on match2@
identifier match2.p;
identifier match2.removefn;
fresh identifier removefn_wrapper = removefn ## "_wrapper";
@@
+#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
+static void removefn_wrapper(struct platform_device *pdev)
+{
+	removefn(pdev);
+}
+#else
+static int removefn_wrapper(struct platform_device *pdev)
+{
+	return removefn(pdev);
+}
+#endif
+
struct platform_driver p = {
-	.remove = __exit_p(removefn),
+	.remove = __exit_p(removefn_wrapper),
};

@fix3 depends on (match1 || match2) && !match3@
@@
+#include <nvidia/conftest.h>
+
#include <...>

@fix4 depends on (match1 || match2) && (!match3 && !fix3)@
@@
+#include <nvidia/conftest.h>
+
#include "..."
