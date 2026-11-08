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

#define GH_HVC_CLOBBERS	"x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", \
			"x16", "x17", "memory"

void gunyah_hypercall_hyp_identify(
	struct gunyah_hypercall_hyp_identify_resp *hyp_identity)
{
	register u64 x0 asm("x0") = 0;
	register u64 x1 asm("x1") = 0;
	register u64 x2 asm("x2") = 0;
	register u64 x3 asm("x3") = 0;

	asm volatile ("hvc #0x6000\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
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

	asm volatile ("hvc #0x6069\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
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

	asm volatile ("hvc #0x6060\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4)
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

	asm volatile ("hvc #0x6012\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
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

	asm volatile ("hvc #0x6015\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
		:
		: GH_HVC_CLOBBERS);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_bell_set_mask);

/*
 * Native Qualcomm ABI for 0x601B:
 *   x0 = queue cap, x1 = buffer IPA, x2 = size, x3 = flags (bit0 only),
 *   x4 = 0. The buffer is consumed as an IPA by the hypervisor, so the
 *   kernel virtual address has to be converted before the call.
 */
enum gunyah_error gunyah_hypercall_msgq_send(u64 capid, size_t size, void *buff,
					     u64 tx_flags, bool *ready)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = virt_to_phys(buff);
	register u64 x2 asm("x2") = size;
	register u64 x3 asm("x3") = tx_flags & BIT(0);
	register u64 x4 asm("x4") = 0;
	register u64 x5 asm("x5") = 0;
	register u64 x6 asm("x6") = 0;
	register u64 x7 asm("x7") = 0;

	asm volatile ("hvc #0x601B\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_send cap=%#llx ipa=%#llx size=%zu flags=%llu resp x0=%#llx x1=%#llx\n",
		capid, (u64)virt_to_phys(buff), size, tx_flags & BIT(0),
		(u64)x0, (u64)x1);

	if (x0 == GUNYAH_ERROR_OK)
		*ready = !!x1;

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_send);

/*
 * The native 0x601C call does not return the result in x1/x2: it writes a
 * response structure to the IPA passed in guest x8. Only x0 carries the
 * error code.
 */
struct gunyah_msgq_recv_resp {
	u32 recv_size;
	u32 reserved0;
	u64 ready_or_len;
	u32 flags;
	u32 reserved1;
};

enum gunyah_error gunyah_hypercall_msgq_recv(u64 capid, void *buff, size_t size,
					     size_t *recv_size, bool *ready)
{
	struct gunyah_msgq_recv_resp resp = { 0 };
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = virt_to_phys(buff);
	register u64 x2 asm("x2") = size;
	register u64 x3 asm("x3") = 0;
	register u64 x8 asm("x8") = virt_to_phys(&resp);

	asm volatile ("hvc #0x601C\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x8)
		:
		: "x9", "x10", "x11", "x12", "x13", "x14", "x15",
		  "x16", "x17", "memory");

	pr_info("gunyah_hvc: msgq_recv cap=%#llx ipa=%#llx size=%zu resp x0=%#llx size=%u ready=%#llx flags=%#x\n",
		capid, (u64)virt_to_phys(buff), size, (u64)x0,
		resp.recv_size, resp.ready_or_len, resp.flags);

	if (x0 == GUNYAH_ERROR_OK) {
		*recv_size = resp.recv_size;
		/*
		 * The native "more messages pending" bit is not documented;
		 * drain until the queue reports GUNYAH_ERROR_MSGQUEUE_EMPTY.
		 */
		*ready = true;
	}

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_recv);

/* x0 = queue cap, x1 = max message size, x2 = flags (bit0 = 0), x3 = 0 */
enum gunyah_error gunyah_hypercall_msgq_configure_send(u64 capid,
						       u64 max_msg_size,
						       u64 flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = max_msg_size;
	register u64 x2 asm("x2") = flags;
	register u64 x3 asm("x3") = 0;

	asm volatile ("hvc #0x601F\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_configure_send cap=%#llx max=%llu resp=%#llx\n",
		capid, max_msg_size, (u64)x0);

	return x0;
}
EXPORT_SYMBOL_GPL(gunyah_hypercall_msgq_configure_send);

/* x0 = queue cap, x1 = max message size, x2 = flags, x3 = 0 */
enum gunyah_error gunyah_hypercall_msgq_configure_receive(u64 capid,
							  u64 max_msg_size,
							  u64 flags)
{
	register u64 x0 asm("x0") = capid;
	register u64 x1 asm("x1") = max_msg_size;
	register u64 x2 asm("x2") = flags;
	register u64 x3 asm("x3") = 0;

	asm volatile ("hvc #0x6020\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
		:
		: GH_HVC_CLOBBERS);

	pr_info("gunyah_hvc: msgq_configure_recv cap=%#llx max=%llu resp=%#llx\n",
		capid, max_msg_size, (u64)x0);

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

	asm volatile ("hvc #0x602C\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5), "+r"(x6)
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

	asm volatile ("hvc #0x6061\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
		  "+r"(x4), "+r"(x5)
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

	asm volatile ("hvc #0x6065\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4)
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

	asm volatile ("hvc #0x606A\n"
		: "+r"(x0), "+r"(x1), "+r"(x2)
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
