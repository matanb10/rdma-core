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

#include <infiniband/verbs_ioctl.h>
#include <infiniband/verbs_write.h>
#include "ibverbs.h"

#include <ccan/build_assert.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <valgrind/memcheck.h>

/* Number of attrs in this and all the link'd buffers */
unsigned int _ioctl_final_num_attrs(unsigned int num_attrs,
				   struct ibv_command_buffer *link)
{
	for (; link; link = link->next)
		num_attrs += link->next_attr - link->hdr.attrs;

	return num_attrs;
}

int _ioctl_init_final_cmdb(struct ibv_command_buffer *cmd, uint16_t object_id,
			   uint16_t method_id, struct ibv_command_buffer *next,
			   size_t num_attrs, size_t total_attrs)
{
	cmd->hdr.object_id = object_id;
	cmd->hdr.method_id = method_id;
	cmd->next = next;
	cmd->next_attr = cmd->hdr.attrs;
	cmd->last_attr = cmd->hdr.attrs + total_attrs;

	/* Only zero the attrs in num_attrs as the ones beyond that will be
	 * filled during execute_ioctl
	 */
	memset(cmd->next_attr, 0, sizeof(*cmd->next_attr) * num_attrs);

	return 0;
}

/* Linearize the link'd buffers into this one */
static void prepare_attrs(struct ibv_command_buffer *cmd)
{
	struct ib_uverbs_attr *end = cmd->next_attr;
	struct ibv_command_buffer *link;

	for (link = cmd->next; link; link = link->next) {
		struct ib_uverbs_attr *cur;

		assert(cmd->hdr.object_id == link->hdr.object_id);
		assert(cmd->hdr.method_id == link->hdr.method_id);

		for (cur = link->hdr.attrs; cur != link->next_attr; cur++)
			*end++ = *cur;

		assert(end <= cmd->last_attr);
	}

	cmd->hdr.num_attrs = end - cmd->hdr.attrs;
}

static void finalize_attr(struct ib_uverbs_attr *attr)
{
	/* Only matches UVERBS_ATTR_TYPE_PTR_OUT */
	if (attr->flags & UVERBS_ATTR_F_VALID_OUTPUT && attr->len)
		VALGRIND_MAKE_MEM_DEFINED((void *)(uintptr_t)attr->data,
					  attr->len);
}

/*
 * Copy the link'd attrs back to their source and make all output buffers safe
 * for VALGRIND
 */
static void finalize_attrs(struct ibv_command_buffer *cmd)
{
	struct ib_uverbs_attr *end = cmd->next_attr;
	struct ibv_command_buffer *link;

	for (end = cmd->hdr.attrs; end != cmd->last_attr; end++)
		finalize_attr(end);

	for (link = cmd->next; link; link = link->next) {
		struct ib_uverbs_attr *cur;

		for (cur = link->hdr.attrs; cur != link->next_attr; cur++) {
			finalize_attr(end);
			*cur = *end++;
		}
	}
}

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd)
{
	prepare_attrs(cmd);
	cmd->hdr.length = sizeof(cmd->hdr) +
		sizeof(cmd->hdr.attrs[0]) * cmd->hdr.num_attrs;
	cmd->hdr.reserved = 0;

	if (ioctl(context->cmd_fd, RDMA_VERBS_IOCTL, &cmd->hdr))
		return errno;

	finalize_attrs(cmd);

	return 0;
}

/*
 * Check if the command buffer provided by the driver includes anything that
 * is not compatible with the legacy interface.  If so, then
 * _execute_ioctl_fallback indicates it handled the call and sets the error code
 */
static bool is_legacy_not_possible(struct ibv_command_buffer *cmdb, int *ret)
{
	struct ib_uverbs_attr *cur;

	if (!cmdb->next)
		return false;

	if (cmdb->next->next_attr - cmdb->next->hdr.attrs > 2)
		goto not_supp;

	for (cur = cmdb->next->hdr.attrs; cur != cmdb->next->next_attr; cur++) {
		if (cur->attr_id != UVERBS_UHW_IN &&
		    cur->attr_id != UVERBS_UHW_OUT)
			goto not_supp;
	}

	return false;

not_supp:
	errno = EOPNOTSUPP;
	*ret = EOPNOTSUPP;
	return true;
}

/*
 * Used to support callers that have a fallback to the old write ABI
 * interface.
 */
bool _execute_ioctl_fallback(struct ibv_context *ctx, unsigned int cmd_bit,
			     struct ibv_command_buffer *cmdb, int *ret)
{
	uint64_t cmd_val = 1ULL << cmd_bit;

	BUILD_ASSERT(sizeof(struct verbs_context_ops) / sizeof(void *) < 64);

	struct verbs_ex_private *priv =
		container_of(ctx, struct verbs_context, context)->priv;

	if (!(priv->enabled_ioctls & cmd_val))
		return is_legacy_not_possible(cmdb, ret);

	*ret = execute_ioctl(ctx, cmdb);

	if (*ret == ENOSYS) {
		priv->enabled_ioctls &= ~cmd_val;
		return is_legacy_not_possible(cmdb, ret);
	}

	return true;
}

int _execute_write_raw(unsigned int cmdnum, struct ibv_context *ctx,
		       struct ib_uverbs_cmd_hdr *req, void *resp)
{
	struct ib_uverbs_cmd_hdr *hdr = req;

	hdr->command = cmdnum;

	if (write(ctx->cmd_fd, req, req->in_words * 4) != req->in_words * 4)
		return errno;

	VALGRIND_MAKE_MEM_DEFINED(resp, req->out_words * 4);

	return 0;
}
