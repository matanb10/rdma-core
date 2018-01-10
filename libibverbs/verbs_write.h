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

#ifndef __INFINIBAND_VERBS_WRITE_H
#define __INFINIBAND_VERBS_WRITE_H

#include <infiniband/verbs_ioctl.h>
#include <infiniband/driver.h>
#include <infiniband/kern-abi.h> /* Only needs rdma/ib_user_verbs.h */

#include <stdbool.h>

static inline struct ib_uverbs_cmd_hdr *get_req_hdr(void *req)
{
	return ((struct ib_uverbs_cmd_hdr *)req);
}

/*
 * The driver's command buffer can include UHW_IN and OUT elements, if so they
 * need to be placed according to these rules:
 *
 *  attr[0] should be UVERBS_UHW_IN. attr.data should point to the start of
 *  the UHW memory block, and attr.len should be the length of the UHW
 *  structure. There should be headroom allocated above the data pointer equal
 *  to the core header and core structure. This will automatically be used by
 *  the core code to build the request.
 *
 *  attr[1] should be UVERBS_UHW_OUT. This follows the same rules as attr[0],
 *  with headroom for the core response structure.
 *
 * The command buffer can omit any of the above two, shifting to fill the gap.
 * In which case there is no driver data passed in.
 */
static inline struct ib_uverbs_attr *
_write_get_uhw(struct ibv_command_buffer *link, unsigned int first,
	       uint16_t attr_id)
{
	if (!link)
		return NULL;

	if (&link->hdr.attrs[first] < link->next_attr &&
	    link->hdr.attrs[first].attr_id == attr_id)
		return &link->hdr.attrs[first];

	first = (first + 1) % 2;

	if (&link->hdr.attrs[first] < link->next_attr &&
	    link->hdr.attrs[first].attr_id == attr_id)
		return &link->hdr.attrs[first];

	return NULL;
}

/*
 * This helper is used in the driver compat wrappers to build the
 * command buffer from the legacy input pointers format.
 */
static inline void _write_set_uhw(struct ibv_command_buffer *cmdb, void *req,
				  size_t core_req_size, size_t req_size,
				  void *resp, size_t core_resp_size,
				  size_t resp_size)
{
	if (req && core_req_size < req_size)
		fill_attr_in(cmdb, UVERBS_UHW_IN,
			     (uint8_t *)req + core_req_size,
			     req_size - core_req_size);

	if (resp && core_resp_size < resp_size)
		fill_attr_in(cmdb, UVERBS_UHW_OUT,
			     (uint8_t *)resp + core_resp_size,
			     resp_size - core_resp_size);
}

/*
 * Within the command implementation we get a pointer to the request and
 * response buffers for the legacy interface. This pointer is either allocated
 * on the stack or arranged to be directly before the UHW memory (see
 * _write_get_uhw)
 */
static inline void *_write_get_req(struct ibv_command_buffer *link,
				   void *onstack, size_t size)
{
	struct ib_uverbs_attr *uhw = _write_get_uhw(link, 0, UVERBS_UHW_IN);
	struct ib_uverbs_cmd_hdr *hdr;

	if (uhw) {
		hdr = (void *)((uintptr_t)uhw->data - size - sizeof(hdr));
		assert((sizeof(hdr) + size + uhw->len) % 4 == 0);
		hdr->in_words = (sizeof(hdr) + size + uhw->len) / 4;
	} else {
		hdr = onstack;
		assert((sizeof(hdr) + size) % 4 == 0);
		hdr->in_words = (sizeof(hdr) + size) / 4;
	}

	return hdr;
}

#define DECLARE_LEGACY_REQ_BUF(_name, _link, _struct)                          \
	_struct __##_name##_onstack;                                           \
	_struct *_name =                                                       \
		_write_get_req(_link, &__##_name##_onstack, sizeof(_struct));

static inline void *_write_get_resp(struct ibv_command_buffer *link,
				    struct ib_uverbs_cmd_hdr *hdr,
				    void *onstack, size_t resp_size,
				    __u64 *hdr_resp_ptr)
{
	struct ib_uverbs_attr *uhw = _write_get_uhw(link, 1, UVERBS_UHW_OUT);
	void *resp_start;

	if (uhw) {
		resp_start = (void *)((uintptr_t)uhw->data - resp_size);
		assert((resp_size + uhw->len) % 4 == 0);
		hdr->out_words = (resp_size + uhw->len) / 4;
	} else {
		resp_start = onstack;
		assert(resp_size % 4 == 0);
		hdr->out_words = resp_size / 4;
	}

	*hdr_resp_ptr = ioctl_ptr_to_u64(resp_start);
	return resp_start;
}

#define DECLARE_LEGACY_RESP_BUF(_name, _link, _req, _struct)                   \
	_struct __##_name##_onstack;                                           \
	_struct *_name = _write_get_resp(_link, get_req_hdr(_req),             \
					 &__##_name##_onstack,                 \
					 sizeof(_struct), &(_req)->response);

/*
 * This macro creates 'req' and 'resp' pointers in the local stack frame that point
 * to the core code write command structures _rep_struct and _resp_struct.
 *
 * This should be done before calling execute_write_bufs
 */
#define DECLARE_LEGACY_UHW_BUFS(_link, _req_struct, _resp_struct)              \
	DECLARE_LEGACY_REQ_BUF(req, _link, _req_struct);                       \
	DECLARE_LEGACY_RESP_BUF(resp, _link, req, _resp_struct);

/*
 * This macro is used to implement the compatability command call wrappers.
 * Compatability calls do not accept a command_buffer, and cannot use the new
 * attribute id mechanism.
 */
#define DECLARE_CMD_BUFFER_COMPAT(_name, _object_id, _method_id)               \
	DECLARE_COMMAND_BUFFER(_name, _object_id, _method_id, 2);              \
	_write_set_uhw(_name, cmd, sizeof(*cmd), cmd_size, resp,               \
		       sizeof(*resp), resp_size);

/*
 * The fallback scheme keeps track of which ioctls succeed in a per-context
 * bitmap.  If ENOSYS is seen then the ioctl is never retried. The caller must
 * use an if statement and then perform the write path instead.
 *
 * cmd_name should be the name of the function op from verbs_context_ops
 * that is being implemented.
 */
#define _CMD_BIT(cmd_name)                                                     \
	(offsetof(struct verbs_context_ops, cmd_name) / sizeof(void *))

bool _execute_ioctl_fallback(struct ibv_context *ctx, unsigned int cmd_bit,
			     struct ibv_command_buffer *cmdb, int *ret);

#define execute_ioctl_fallback(ctx, cmd_name, cmdb, ret)                       \
	_execute_ioctl_fallback(ctx, _CMD_BIT(cmd_name), cmdb, ret)

/* These helpers replace the raw write() and IBV_INIT_CMD macros */
int _execute_write_raw(unsigned int cmdnum, struct ibv_context *ctx,
			      struct ib_uverbs_cmd_hdr *req, void *resp);

/* For users of DECLARE_LEGACY_UHW_BUFS */
#define execute_write_bufs(cmdnum, ctx, req, resp)		\
	_execute_write_raw(cmdnum, ctx, get_req_hdr(req), resp)

static inline int _execute_write(unsigned int cmdnum, struct ibv_context *ctx,
				 void *req, size_t req_len, void *resp,
				 size_t resp_len, __u64 *hdr_resp_ptr)
{
	struct ib_uverbs_cmd_hdr *hdr = get_req_hdr(req);

	*hdr_resp_ptr = ioctl_ptr_to_u64(resp);
	hdr->in_words = req_len / 4;
	hdr->out_words = resp_len / 4;
	return _execute_write_raw(cmdnum, ctx, hdr, resp);
}

/* For users with no UHW bufs */
#define execute_write(cmdnum, ctx, req, resp)                                  \
	_execute_write(cmdnum, ctx, &req, sizeof(req), &resp, sizeof(resp),    \
		       &(req).response);

#endif
