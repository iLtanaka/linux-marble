// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Adapted for Qualcomm downstream hypercall ABI: firmware expects
 * HVC with immediate function ID in the 0x60xx range.
 */

#include <linux/arm-smccc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gunyah.h>
#include <linux/uuid.h>

#define GUNYAH_VCPU_RUN_STATE_PSCI_SYSTEM_RESET	256

/* {c1d58fcd-a453-5fdb-9265-ce36673d5f14} */
static const uuid_t GUNYAH_UUID = UUID_INIT(0xc1d58fcd, 0xa453, 0x5fdb, 0x92,
					    0x65, 0xce, 0x36, 0x67, 0x3d, 0x5f,
					    0x14);

bool arch_is_gunyah_guest(void)
{
	return true;
}
EXPORT_SYMBOL_GPL(arch_is_gunyah_guest);

/*
 * The hypervisor is allowed to clobber every register except the callee-saved
 * ones. x0-x7 are always listed as hypercall operands below, so only x8 and
 * x9-x18 have to be listed as clobbers. Listing them matters: without it the
 * compiler may park a live C variable in one of these registers across the
 * hvc and read back whatever the hypervisor left there.
 */
#define GH_HVC_CLOBBERS_X18	"x18",
#define GH_HVC_CLOBBERS_BASE	"x9", "x10", "x11", "x12", "x13", "x14", \
				"x15", "x16", "x17", GH_HVC_CLOBBERS_X18 \
				"memory"
#define GH_HVC_CLOBBERS	"x8", GH_HVC_CLOBBERS_BASE
#define GH_HVC_CLOBBERS_NO_X8	GH_HVC_CLOBBERS_BASE

void gunyah_hypercall_hyp_identify(
	struct gunyah_hypercall_hyp_identify_resp *hyp_identity)
{
	register u64 x0 asm("x0") = 0;
	register u64 x1 asm("x1") = 0;
	register u64 x2 asm("x2") = 0;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6000\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	hyp_identity->api_info = x0;
	hyp_identity->flags[0] = x1;
	hyp_identity->flags[1] = x2;
	hyp_identity->flags[2] = x3;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_hyp_identify);

enum gunyah_error gunyah_hypercall_addrspc_modify_pages(u64 capid, u64 addr,
							u64 size, u64 flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = addr;
	register u64 x2 asm("x2") = size;
	register u64 x3 asm("x3") = flags;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6069\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_addrspc_modify_pages);

enum gunyah_error gunyah_hypercall_addrspc_configure_vmmio_range(u64 capid, u64 base,
								u64 size, u64 op)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = base;
	register u64 x2 asm("x2") = size;
	register u64 x3 asm("x3") = op;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6060\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4),
		  "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_addrspc_configure_vmmio_range);

enum gunyah_error gunyah_hypercall_bell_send(u64 capid, u64 new_flags, u64 *old_flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = new_flags;
	register u64 x2 asm("x2") = 0;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6012\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	if (x0 == GUNYAH_ERROR_OK && old_flags)
		*old_flags = x1;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_bell_send);

enum gunyah_error gunyah_hypercall_bell_set_mask(u64 capid, u64 enable_mask, u64 ack_mask)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = enable_mask;
	register u64 x2 asm("x2") = ack_mask;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6015\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_bell_set_mask);

/*
 * RM message-queue hypercall ABI:
 *   0 - reverse-engineered from the device hypervisor image
 *       send: x0=cap, x1=buffer IPA, x2=size, x3=flags, x4=0
 *       recv: x0=cap, x1=buffer IPA, x2=size, x3=0,
 *             x8=output-struct IPA (u32 size, u32 rsvd, u64 buf, u32 flags, ...)
 *   1 - published Gunyah API documentation
 *       send: x0=cap, x1=size, x2=data VMAddr, x3=flags, x1 out=NotFull
 *       recv: x0=cap, x1=data VMAddr, x2=MaximumSize, x1 out=Size,
 *             x2 out=NotEmpty
 *
 * Both variants are kept so a single boot can test either one.
 */
int gunyah_msgq_abi;
module_param_named(msgq_abi, gunyah_msgq_abi, int, 0644);
MODULE_PARM_DESC(msgq_abi,
		 "RM message-queue ABI: 0 = reverse-engineered, 1 = published doc");
EXPORT_SYMBOL_GPL(gunyah_msgq_abi);

enum gunyah_error gunyah_hypercall_msgq_send(u64 capid, size_t size, void *buff,
					     u64 tx_flags, bool *ready)
{
	bool re_abi = !gunyah_msgq_abi;
	u64 addr = re_abi ? virt_to_phys(buff) : (u64)buff;
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = re_abi ? addr : size;
	register u64 x2 asm("x2") = re_abi ? size : addr;
	register u64 x3 asm("x3") = tx_flags;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x601B\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_send abi=%d cap=%#llx size=%zu addr=%#llx flags=%llu resp x0=%#llx x1=%#llx\n",
		gunyah_msgq_abi, capid, size, addr, tx_flags, (u64)x0, (u64)x1);

	if (x0 == GUNYAH_ERROR_OK)
		/* The reverse-engineered ABI does not report fullness. */
		*ready = re_abi ? true : !!x1;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_send);

/*
 * Native Qualcomm ABI for 0x601C:
 *   x0 = queue cap, x1 = buffer IPA, x2 = MaximumSize, x3 = 0,
 *   x8 = IPA of a 32-byte guest output structure (caller-provided).
 *   Output structure: u32 size, u32 reserved, u64 buff, u32 flags, ...
 */
enum gunyah_error gunyah_hypercall_msgq_recv(u64 capid, void *buff, size_t size,
					     size_t *recv_size, bool *ready,
					     void *out)
{
	bool re_abi = !gunyah_msgq_abi;

	if (re_abi) {
		u64 *resp = out;
		u64 addr = virt_to_phys(buff);
		u64 out_ipa = virt_to_phys(resp);
		register u64 x0 asm("x0") = capid;
		register u64 x1 asm("x1") = addr;
		register u64 x2 asm("x2") = size;
		register u64 x3 asm("x3") = 0;
		register u64 x4 asm("x4") = 0;
		register u64 x5 asm("x5") = 0;
		register u64 x6 asm("x6") = 0;
		register u64 x7 asm("x7") = 0;
		register u64 x8 asm("x8") = out_ipa;

		asm volatile ("hvc #0x601C\n"
			: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
			  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7),
			  "+r"(x8)
			:
			: GH_HVC_CLOBBERS_NO_X8);

		pr_info("gunyah_hvc: msgq_recv abi=0 cap=%#llx addr=%#llx max=%zu out_ipa=%#llx resp x0=%#llx out=%#llx,%#llx,%#llx,%#llx\n",
			capid, addr, size, out_ipa, (u64)x0, resp[0],
			resp[1], resp[2], resp[3]);

		if (x0 == GUNYAH_ERROR_OK) {
			*recv_size = lower_32_bits(resp[0]);
			*ready = !!(resp[2] & 0x1);
		} else if (x0 == GUNYAH_ERROR_ARG_SIZE ||
			   x0 == GUNYAH_ERROR_BUSY ||
			   x0 == GUNYAH_ERROR_MSGQUEUE_EMPTY) {
			/* The device reports an empty queue as 2, 31 or 60. */
			return GUNYAH_ERROR_MSGQUEUE_EMPTY;
		}

		return x0;
	}

	{
		register u64 x0 asm("x0") = capid;
		register u64 x1 asm("x1") = (u64)buff;
		register u64 x2 asm("x2") = size;
		register u64 x3 asm("x3") = 0;
		register u64 x4 asm("x4") = 0;
		register u64 x5 asm("x5") = 0;
		register u64 x6 asm("x6") = 0;
		register u64 x7 asm("x7") = 0;

		asm volatile ("hvc #0x601C\n"
			: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
			  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
			:
			: GH_HVC_CLOBBERS);

		pr_info("gunyah_hvc: msgq_recv abi=1 cap=%#llx addr=%#llx max=%zu resp x0=%#llx size=%llu not_empty=%llu\n",
			capid, (u64)buff, size, (u64)x0, (u64)x1, (u64)x2);

		if (x0 == GUNYAH_ERROR_OK) {
			*recv_size = x1;
			*ready = !!x2;
		}

		return x0;
	}
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_recv);

enum gunyah_error gunyah_hypercall_msgq_flush(u64 capid)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = 0;
	register u64 x2 asm("x2") = 0;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x601D\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_flush cap=%#llx resp=%#llx\n", capid, (u64)x0);

	/* A hypercall missing from the table leaves the registers untouched. */
	if (x0 == capid)
		return GUNYAH_ERROR_UNIMPLEMENTED;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_flush);

enum gunyah_error gunyah_hypercall_msgq_configure_send(u64 capid,
						       u64 max_msg_size,
						       u64 flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = max_msg_size;
	register u64 x2 asm("x2") = flags;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x601F\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_configure_send cap=%#llx max=%llu resp=%#llx\n",
		capid, max_msg_size, (u64)x0);

	if (x0 == capid)
		return GUNYAH_ERROR_UNIMPLEMENTED;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_configure_send);

enum gunyah_error gunyah_hypercall_msgq_configure_receive(u64 capid,
							  u64 max_msg_size,
							  u64 flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = max_msg_size;
	register u64 x2 asm("x2") = flags;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6020\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_configure_receive cap=%#llx max=%llu resp=%#llx\n",
		capid, max_msg_size, (u64)x0);

	if (x0 == capid)
		return GUNYAH_ERROR_UNIMPLEMENTED;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_configure_receive);

enum gunyah_error gunyah_hypercall_addrspace_map(u64 capid, u64 extent_capid, u64 vbase,
					u32 extent_attrs, u32 flags, u64 offset, u64 size)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = extent_capid;
	register u64 x2 asm("x2") = vbase;
	register u64 x3 asm("x3") = extent_attrs;
	register u64 x4 asm("x4") = flags;
	register u64 x5 asm("x5") = offset;
	register u64 x6 asm("x6") = size;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x602B\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_addrspace_map);

enum gunyah_error gunyah_hypercall_addrspace_unmap(u64 capid, u64 extent_capid, u64 vbase,
					u32 flags, u64 offset, u64 size)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = extent_capid;
	register u64 x2 asm("x2") = vbase;
	register u64 x3 asm("x3") = flags;
	register u64 x4 asm("x4") = offset;
	register u64 x5 asm("x5") = size;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x602C\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_addrspace_unmap);

enum gunyah_error gunyah_hypercall_memextent_donate(u32 options, u64 from_capid, u64 to_capid,
						u64 offset, u64 size)
{
	register u64 x0 asm("x0") = options;
	register u64 x1 asm("x1") = from_capid;
	register u64 x2 asm("x2") = to_capid;
	register u64 x3 asm("x3") = offset;
	register u64 x4 asm("x4") = size;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6061\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_memextent_donate);

enum gunyah_error
gunyah_hypercall_vcpu_run(u64 capid, unsigned long *resume_data,
			  struct gunyah_hypercall_vcpu_run_resp *resp)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = resume_data[0];
	register u64 x2 asm("x2") = resume_data[1];
	register u64 x3 asm("x3") = resume_data[2];
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x6065\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4),
		  "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	if (x0 == GUNYAH_ERROR_OK) {
		resp->sized_state = x1;
		resp->state_data[0] = x2;
		resp->state_data[1] = x3;
		resp->state_data[2] = x4;
	}

	if (resp->sized_state == GUNYAH_VCPU_RUN_STATE_PSCI_SYSTEM_RESET)
		resp->sized_state = GUNYAH_VCPU_STATE_SYSTEM_OFF;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_vcpu_run);

enum gunyah_error
gunyah_hypercall_addrspace_find_info_area(unsigned long *ipa, unsigned long *size)
{
	register u64 x0 asm("x0") = 0;
	register u64 x1 asm("x1") = 0;
	register u64 x2 asm("x2") = 0;
	register u64 x3 asm("x3") = 0;
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x606A\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	if (x0 == GUNYAH_ERROR_OK) {
		*ipa = x1;
		*size = x2;
	}

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_addrspace_find_info_area);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Hypervisor Hypercalls");
