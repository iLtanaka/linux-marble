// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gunyah: " fmt

#include <linux/gunyah.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

struct gunyah_info_desc {
	__le16 id;
	__le16 owner;
	__le32 size;
	__le32 offset;
#define INFO_DESC_VALID		BIT(31)
	__le32 flags;
};

static void *info_area;

void *gunyah_get_info(u16 owner, u16 id, size_t *size)
{
	struct gunyah_info_desc *desc = info_area;
	__le16 le_owner = cpu_to_le16(owner);
	__le16 le_id = cpu_to_le16(id);

	if (!desc)
		return ERR_PTR(-ENOENT);

	for (desc = info_area; le32_to_cpu(desc->offset); desc++) {
		if (!(le32_to_cpu(desc->flags) & INFO_DESC_VALID))
			continue;
		mb();
		if (le_owner == desc->owner && le_id == desc->id) {
			if (size)
				*size = le32_to_cpu(desc->size);
			return info_area + le32_to_cpu(desc->offset);
		}
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(gunyah_get_info);

/**
 * gunyah_map_addrspace_info_area - Map the addrspace info area
 *
 * Maps the hypervisor-provided addrspace info area into kernel space.
 * Typically used once during the initialization and after hibernation
 * restore (PM_POST_HIBERNATION) or in other scenarios where the info area
 * needs to be re-initialized so as to establish the communincation with
 * the resource manager.
 */
int gunyah_map_addrspace_info_area(void)
{
	/*
	 * marble firmware (SM7475) does not implement the info area HVC
	 * (hvc #0x606A traps with Undefined Instruction). Skip the call
	 * entirely so rsc_mgr falls back to /hypervisor DTS node.
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(gunyah_map_addrspace_info_area);

/**
 * gunyah_unmap_addrspace_info_area - Unmap the addrspace info area
 *
 * Unmaps the previously mapped addrspace info area. Commonly used
 * before hibernation (PM_HIBERNATION_PREPARE) or when the mapping
 * is no longer needed.
 */
void gunyah_unmap_addrspace_info_area(void)
{
	memunmap(info_area);
	info_area = NULL;
}
EXPORT_SYMBOL_GPL(gunyah_unmap_addrspace_info_area);

static int __init gunyah_init(void)
{
	struct gunyah_hypercall_hyp_identify_resp gunyah_api;

	if (!arch_is_gunyah_guest())
		return -ENODEV;

	gunyah_hypercall_hyp_identify(&gunyah_api);

	pr_info("Running under Gunyah hypervisor %llx/v%u\n",
		FIELD_GET(GUNYAH_API_INFO_VARIANT_MASK, gunyah_api.api_info),
		gunyah_api_version(&gunyah_api));

	/* Might move this out to individual drivers if there's ever an API version bump */
	/* Version check disabled: firmware on marble does not implement API V1 SMCCC call */

	return gunyah_map_addrspace_info_area();
}
core_initcall(gunyah_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Driver");
