/*
 * Copyright (c) 2018 Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __INFINIBAND_VERBS_IOCTL_H
#define __INFINIBAND_VERBS_IOCTL_H

#include <stdint.h>
#include <assert.h>
#include <rdma/rdma_user_ioctl.h>
#include <rdma/ib_user_ioctl_verbs.h>
#include <infiniband/verbs.h>

static inline uint64_t ioctl_ptr_to_u64(const void *ptr)
{
	if (sizeof(ptr) == sizeof(uint64_t))
		return (uintptr_t)ptr;

	/*
	 * Some CPU architectures require sign extension when converting from
	 * a 32 bit to 64 bit pointer.  This should match the kernel
	 * implementation of compat_ptr() for the architecture.
	 */
#if defined(__tilegx__)
	return (int64_t)(intptr_t)ptr;
#else
	return (uintptr_t)ptr;
#endif
}

/*
 * The command buffer is organized as a linked list of blocks of attributes.
 * Each stack frame allocates its block and then calls up toward to core code
 * which will do the ioctl. The frame that does the ioctl calls the special
 * FINAL varient which will allocate enough space to linearize the attribute
 * buffer for the kernel.
 *
 * The current range of attributes to fill is next_attr -> last_attr.
 */

struct ibv_command_buffer {
	struct ibv_command_buffer *next;
	struct ib_uverbs_attr *next_attr;
	struct ib_uverbs_attr *last_attr;
	struct ib_uverbs_ioctl_hdr hdr;
};

/*
 * Constructing an array of ibv_command_buffer is a reasonable way to expand
 * the VLA in hdr.attrs on the stack and also allocate some internal state in
 * a single contiguous stack memory region. It will over-allocate the region in
 * some cases, but this approach allows the number of elements to be dynamic,
 * and not fixed as a compile time constant.
 */
#define _IOCTL_NUM_CMDB(_num_attrs)                                            \
	((sizeof(struct ibv_command_buffer) +                                  \
	  sizeof(struct ib_uverbs_attr) * (_num_attrs) +                       \
	  sizeof(struct ibv_command_buffer) - 1) /                             \
	 sizeof(struct ibv_command_buffer))

unsigned int _ioctl_final_num_attrs(unsigned int num_attrs,
				    struct ibv_command_buffer *link);

/*
 * C99 does not permit an initializer for VLAs, so this function does the init
 * instead. It is called in the wonky way so that DELCARE_COMMAND_BUFFER can
 * still be a 'variable', and we so we don't require C11 mode.
 */
int _ioctl_init_final_cmdb(struct ibv_command_buffer *cmd, uint16_t object_id,
			   uint16_t method_id, struct ibv_command_buffer *next,
			   size_t num_attrs, size_t total_attrs);

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version requires _num_attrs to be a compile
 * time constant. _link is a previous COMMAND_BUFFER in the call chain.
 */
#define DECLARE_COMMAND_BUFFER_LINK(_name, _object_id, _method_id, _num_attrs, \
				    _link)                                     \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs)] = {       \
		{.hdr =                                                        \
			 {                                                     \
				 .object_id = (_object_id),                    \
				 .method_id = (_method_id),                    \
				 .num_attrs = _IOCTL_NUM_CMDB(_num_attrs),     \
			 },                                                    \
		 .next = _link,						       \
		 .next_attr = _name[0].hdr.attrs,                              \
		 .last_attr = _name[0].hdr.attrs + _num_attrs},                \
	}

#define DECLARE_COMMAND_BUFFER(_name, _object_id, _method_id, _num_attrs)      \
	DECLARE_COMMAND_BUFFER_LINK(_name, _object_id, _method_id, _num_attrs, \
				    NULL)

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version is used only by the function that
 * calls execute_ioctl. It allocates enough storage to linearize all of the
 * linked COMMAND_BUFFER's to be passed to the kernel.
 */
#define DECLARE_COMMAND_BUFFER_FINAL(_name, _object_id, _method_id,            \
				     _num_attrs, _link)                        \
	unsigned int __##_name##total =                                        \
		_ioctl_final_num_attrs(_num_attrs, _link);                     \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(__##_name##total)];    \
	int __attribute__((unused)) __##_name##dummy =                         \
		_ioctl_init_final_cmdb(_name, _object_id, _method_id, _link,   \
				       _num_attrs, __##_name##total)

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd);

static inline struct ib_uverbs_attr *
_ioctl_next_attr(struct ibv_command_buffer *cmd, uint16_t attr_id)
{
	struct ib_uverbs_attr *attr;

	assert(cmd->next_attr < cmd->last_attr);
	attr = cmd->next_attr++;
	attr->attr_id = attr_id;

	/*
	 * All attributes default to mandatory. Wrapper the fill_* call in
	 * attr_optional() to make it optional.
	 */
	attr->flags = UVERBS_ATTR_F_MANDATORY;

	return attr;
}

/* Make the attribute optional. */
static inline struct ib_uverbs_attr *attr_optional(struct ib_uverbs_attr *attr)
{
	attr->flags &= ~UVERBS_ATTR_F_MANDATORY;
	return attr;
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_IDR */
static inline struct ib_uverbs_attr *
fill_attr_in_obj(struct ibv_command_buffer *cmd, uint16_t attr_id, uint32_t idr)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->data = idr;
	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_out_obj(struct ibv_command_buffer *cmd, uint16_t attr_id)
{
	return fill_attr_in_obj(cmd, attr_id, 0);
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_IN */
static inline struct ib_uverbs_attr *
fill_attr_in(struct ibv_command_buffer *cmd, uint16_t attr_id, const void *data,
	     size_t len)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);

	attr->len = len;
	if (len <= sizeof(uint64_t))
		memcpy(&attr->data, data, len);
	else
		attr->data = ioctl_ptr_to_u64(data);

	return attr;
}

#define fill_attr_in_ptr(cmd, attr_id, ptr)                                    \
	fill_attr_in(cmd, attr_id, ptr, sizeof(*ptr))

/* Send attributes of various inline kernel types */

static inline struct ib_uverbs_attr *
fill_attr_in_uint64(struct ibv_command_buffer *cmd, uint16_t attr_id,
		    uint64_t data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->len = sizeof(data);
	attr->data = data;

	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_in_uint32(struct ibv_command_buffer *cmd, uint16_t attr_id,
		    uint32_t data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->len = sizeof(data);
	attr->data = data;

	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_in_fd(struct ibv_command_buffer *cmd, uint16_t attr_id, int fd)
{
	struct ib_uverbs_attr *attr;

	if (fd == -1)
		return NULL;

	attr = _ioctl_next_attr(cmd, attr_id);
	attr->data = fd;
	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_out_fd(struct ibv_command_buffer *cmd, uint16_t attr_id, int fd)
{
	struct ib_uverbs_attr *attr;

	attr = _ioctl_next_attr(cmd, attr_id);
	attr->data = 0;
	return attr;
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_OUT */
static inline struct ib_uverbs_attr *
fill_attr_out(struct ibv_command_buffer *cmd, uint16_t attr_id, void *data,
	      size_t len)
{
	struct ib_uverbs_attr *attr;

	attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);
	attr->len = len;
	attr->data = (uintptr_t)data;

	return attr;
}

#define fill_attr_out_ptr(cmd, attr_id, ptr)                                 \
	fill_attr_out(cmd, attr_id, ptr, sizeof(*(ptr)))

#endif
