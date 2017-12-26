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

#include <unistd.h>
#include <config.h>
#include <valgrind/memcheck.h>
#include <infiniband/verbs_ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <infiniband/verbs.h>
#include <infiniband/driver.h>
#include <rdma/ib_user_ioctl_cmds.h>
#include "ibverbs.h"

struct ibv_ioctl_cmd {
	struct ib_uverbs_ioctl_hdr	hdr;
	struct ib_uverbs_attr		attrs[0];
};

int _ioctl_init_final_cmdb(struct ibv_command_buffer *cmd,
			   uint16_t object_id, uint16_t method_id,
			   struct ibv_command_buffer *next,
			   size_t num_attrs)
{
	struct ibv_command_buffer *cmdb_iter = next;
	struct ib_uverbs_attr *attr_iter = cmd->hdr.attrs;

	if (!next)
		return _ioctl_init_cmdb(cmd, object_id, method_id, num_attrs);

	_ioctl_verify_link(next, object_id, method_id);

	cmd->next = next;
	cmd->base_attr = cmd->hdr.attrs;

	while (cmdb_iter) {
		size_t num_attrs_in_iter = _ioctl_get_num_attrs(cmdb_iter);

		memcpy(attr_iter, cmdb_iter->base_attr,
		       sizeof(*attr_iter) * num_attrs_in_iter);
		cmd->base_attr += num_attrs_in_iter;
		num_attrs -= num_attrs_in_iter;
		cmdb_iter = cmdb_iter->next;
	}

	__ioctl_init_cmdb(cmd, object_id, method_id, cmd->base_attr, num_attrs);

	return 0;
}

/*
 * Automatically annotate pointers passed as OUT to the kernel as filled in
 * upon success
 */
static void annotate_buffers(struct ibv_command_buffer *cmd)
{
	struct ibv_command_buffer *copy_dst = cmd->next;
	struct ib_uverbs_attr *attr_dst =
		copy_dst == NULL ? NULL : copy_dst->hdr.attrs;
	struct ib_uverbs_attr *attr_src = cmd->hdr.attrs;

	while (attr_src != cmd->base_attr) {

		if (attr_src->flags & UVERBS_ATTR_F_VALID_OUTPUT) {
			*attr_dst = *attr_src;
			VALGRIND_MAKE_MEM_DEFINED((void *)(uintptr_t)attr_src->data,
						  attr_src->len);
		}

		attr_src++;

		if (++attr_dst == copy_dst->last_attr) {
			copy_dst = copy_dst->next;
			if (copy_dst)
				attr_dst = copy_dst->hdr.attrs;
		}
	}
}

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd)
{
	struct verbs_context *vctx = verbs_get_ctx(context);

	cmd->hdr.driver_id = vctx->priv->driver_id;
	cmd->hdr.num_attrs = _ioctl_get_num_attrs(cmd);
	cmd->hdr.length = sizeof(cmd->hdr) +
		sizeof(cmd->hdr.attrs[0]) * cmd->hdr.num_attrs;

	if (ioctl(context->cmd_fd, RDMA_VERBS_IOCTL, &cmd->hdr))
		return errno;

	annotate_buffers(cmd);

	return 0;
}

/*
 * Fill in the legacy driver specific structures. These all follow the
 * 'common' structures which we do not use anymore.
 */
int _execute_ioctl_legacy(struct ibv_context *context,
			  struct ibv_command_buffer *cmdb, const void *cmd,
			  size_t cmd_common_len, size_t cmd_total_len,
			  void *resp, size_t resp_common_len,
			  size_t resp_total_len)
{
	if (cmd_common_len < cmd_total_len)
		fill_attr_in(cmdb, UVERBS_UHW_IN,
			     cmd_total_len - cmd_common_len,
			     (const uint8_t *)cmd + cmd_common_len);
	if (resp_common_len < resp_total_len)
		fill_attr_in(cmdb, UVERBS_UHW_OUT,
			     resp_total_len - resp_common_len,
			     (const uint8_t *)resp + resp_common_len);

	return execute_ioctl(context, cmdb);
}
