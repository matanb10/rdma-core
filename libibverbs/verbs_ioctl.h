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

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_user_ioctl_verbs.h>
#include <rdma/ib_user_ioctl_verbs.h>

struct ibv_command_buffer {
	struct ibv_command_buffer	*next;
	struct ib_uverbs_attr		*base_attr;
	struct ib_uverbs_attr		*next_attr;
	struct ib_uverbs_attr		*last_attr;
	struct ib_uverbs_ioctl_hdr	hdr;
};

static inline int _ioctl_is_cmdb_final(struct ibv_command_buffer *cmdb)
{
	return cmdb->base_attr != cmdb->hdr.attrs;
}

/*
 * Constructing an array of ibv_command_buffer is a reasonable way to expand
 * the VLA in hdr.attrs on the stack and also allocate some internal state in
 * a single contiguos stack memory region. It will over-allocate the region in
 * some cases, but this approach allows the number of elements to be dynamic,
 * and not fixed as a compile time constant.
 */

#define _IOCTL_NUM_CMDB(_num_attrs)					      \
	((sizeof(struct ibv_command_buffer) +                                  \
	  sizeof(struct ib_uverbs_attr) * (_num_attrs) +                      \
	  sizeof(struct ibv_command_buffer) - 1) /                             \
	 sizeof(struct ibv_command_buffer))

/*
 * C99 does not permit an initializer for VLAs, so this function does the init
 * instead. It is called in the wonky way so that DELCARE_COMMAND_BUFFER can
 * still be a 'variable', and we so we don't require C11 mode.
 */

static inline void __ioctl_init_cmdb(struct ibv_command_buffer *cmd,
				     uint16_t object_id, uint16_t method_id,
				     struct ib_uverbs_attr *base_attr,
				     size_t num_attrs)
{
	memset(&cmd->hdr, 0, sizeof(cmd->hdr));
	cmd->hdr.object_id = object_id;
	cmd->hdr.method_id = method_id;
	memset(base_attr, 0, sizeof(*base_attr) * num_attrs);

	cmd->next_attr = base_attr;
	cmd->base_attr = base_attr;
	cmd->last_attr = base_attr + num_attrs;

}

static inline int _ioctl_init_cmdb(struct ibv_command_buffer *cmd,
				   uint16_t object_id, uint16_t method_id,
				   size_t num_attrs)
{

	struct ib_uverbs_attr *attrs = cmd->hdr.attrs;

	__ioctl_init_cmdb(cmd, object_id, method_id, attrs, num_attrs);
	cmd->next = NULL;

	return 0;
}

static inline void _ioctl_verify_link(struct ibv_command_buffer *cmd,
				      uint16_t object_id, uint16_t method_id)
{
	assert(!_ioctl_is_cmdb_final(cmd));
	assert(cmd->hdr.object_id == object_id);
	assert(cmd->hdr.method_id == method_id);
}

static inline void ioctl_link_cmd(struct ibv_command_buffer *cmd,
				  struct ibv_command_buffer *link)
{
	assert(!_ioctl_is_cmdb_final(link));
	_ioctl_verify_link(cmd, link->hdr.object_id, link->hdr.method_id);

	cmd->next = link;
}

static inline unsigned short
_ioctl_get_num_attrs(struct ibv_command_buffer *cmd)
{
	return cmd->next_attr - cmd->hdr.attrs;
}

/*
 * Currently this isn't exported to drivers, but it can be exported if
 * required.
 */
int _ioctl_init_final_cmdb(struct ibv_command_buffer *cmd,
			   uint16_t object_id, uint16_t method_id,
			   struct ibv_command_buffer *next,
			   size_t num_attrs);

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version requires _num_attrs to be a compile
 * time constant.
 */
#define DECLARE_COMMAND_BUFFER(_name, _object_id, _method_id, _num_attrs)      \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs)] = {       \
		{.hdr = {.object_id = (_object_id),                            \
			 .method_id = (_method_id)},			       \
		 .next_attr = _name[0].hdr.attrs,			       \
		 .base_attr = _name[0].hdr.attrs,			       \
		 .last_attr = _name[0].hdr.attrs + _num_attrs}}

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version doesn't requires _num_attrs to be a
 * compile time constant.
 */
#define DECLARE_COMMAND_BUFFER_FLEX(_name, _object_id, _method_id,	       \
				    _num_attrs)				       \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs)];          \
	int __attribute__((unused)) __##_name##dummy = _ioctl_init_cmdb(       \
		_name, _object_id, _method_id, _num_attrs)

/* Make the attribute optional. */
static inline struct ib_uverbs_attr *attr_optional(struct ib_uverbs_attr *attr)
{
	attr->flags &= ~UVERBS_ATTR_F_MANDATORY;
	return attr;
}

static inline unsigned short
_ioctl_get_total_num_attrs(struct ibv_command_buffer *cmd)
{
	unsigned short num = 0;

	do {
		num += _ioctl_get_num_attrs(cmd);
		cmd = cmd->next;
	} while(cmd);

	return num;
}

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version gets another command buffer (_link)
 * and serialize it into this buffer.
 */
#define DECLARE_COMMAND_BUFFER_FINAL(_name, _object_id, _method_id,	       \
				    _num_attrs,_link)			       \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs) +	       \
							_ioctl_get_total_num_attrs(_link)]; \
	int __attribute__((unused)) __##_name##dummy = _ioctl_init_final_cmdb( \
		_name, _object_id, _method_id, _link, _num_attrs)

/* Used in places that call execute_ioctl_legacy */
#define DECLARE_COMMAND_BUFFER_LEGACY(name, object_id, method_id, num_attrs)   \
	DECLARE_COMMAND_BUFFER(name, object_id, method_id, (num_attrs) + 2)

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd);

/*
 * execute, including the legacy driver specific attributes that use the
 * UVERBS_UHW_IN, UVERBS_UHW_OUT
 */
int _execute_ioctl_legacy(struct ibv_context *context,
			  struct ibv_command_buffer *cmdb, const void *cmd,
			  size_t cmd_common_len, size_t cmd_total_len,
			  void *resp, size_t resp_common_len,
			  size_t resp_total_len);
#define execute_ioctl_legacy(context, cmdb, cmd, cmd_size, resp, resp_size)    \
	_execute_ioctl_legacy(context, cmdb, cmd, sizeof(*(cmd)), cmd_size,    \
			      resp, sizeof(*(resp)), resp_size)

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

/* Send attributes of kernel type UVERBS_ATTR_TYPE_IDR */
static inline struct ib_uverbs_attr *
fill_attr_obj_in(struct ibv_command_buffer *cmd, uint16_t attr_id, uint32_t idr)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->data = idr;
	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_obj_out(struct ibv_command_buffer *cmd, uint16_t attr_id)
{
	return fill_attr_obj_in(cmd, attr_id, 0);
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_IN */
static inline struct ib_uverbs_attr *
fill_attr_in(struct ibv_command_buffer *cmd, uint16_t attr_id, size_t len,
	     const void *data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);

	attr->len = len;
	if (len <= sizeof(uint64_t))
		memcpy(&attr->data, data, len);
	else
		attr->data = (uintptr_t)data;

	return attr;
}

#define fill_attr_in_ptr(cmd, attr_id, ptr)                                    \
	fill_attr_in(cmd, attr_id, sizeof(*ptr), ptr)

static inline struct ib_uverbs_attr *
fill_attr_uint64(struct ibv_command_buffer *cmd, uint16_t attr_id, uint64_t data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->len = sizeof(data);
	attr->data = data;

	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_uint32(struct ibv_command_buffer *cmd, uint16_t attr_id, uint32_t data)
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

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_OUT */
static inline struct ib_uverbs_attr *
fill_attr_out(struct ibv_command_buffer *cmd, uint16_t attr_id, size_t len,
	      void *data)
{
	struct ib_uverbs_attr *attr;

	attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);
	attr->len = len;
	attr->data = (uintptr_t)data;

	return attr;
}

#define fill_attr_out_ptr(cmd, attr_id, ptr)                                 \
	fill_attr_out(cmd, attr_id, sizeof(*(ptr)), (ptr))

static inline struct ib_uverbs_attr *
fill_attr_in_enum(struct ibv_command_buffer *cmd, uint16_t attr_id,
		  uint8_t elem_id, size_t len, const void *data)
{
	struct ib_uverbs_attr *attr;

	attr = fill_attr_in(cmd, attr_id, len, data);
	attr->attr_data.enum_data.elem_id = elem_id;

	return attr;
}

#define fill_attr_in_enum_ptr(cmd, attr_id, elem_id, ptr)		       \
	fill_attr_in_enum(cmd, attr_id, elem_id, sizeof(*(ptr)), (ptr))

#endif
