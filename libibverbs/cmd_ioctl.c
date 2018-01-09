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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <valgrind/memcheck.h>
#include <infiniband/driver.h>
#include "ibverbs.h"

#include <rdma/ib_user_ioctl_cmds.h>

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
	memset(&cmd->hdr, 0, sizeof(cmd->hdr));
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

/* Copy the link'd attrs back to their source and make all output buffers safe
 * for VALGRIND */
static void finalize_attrs(struct ibv_command_buffer *cmd)
{
	struct ib_uverbs_attr *end = cmd->next_attr;
	struct ibv_command_buffer *link;

	for (end = cmd->hdr.attrs; end != cmd->last_attr; end++)
		if (end->flags & UVERBS_ATTR_F_VALID_OUTPUT &&
		    end->len) {
			VALGRIND_MAKE_MEM_DEFINED((void *)(uintptr_t)end->data,
						  end->len);
		}

	for (link = cmd->next; link; link = link->next) {
		struct ib_uverbs_attr *cur;

		for (cur = link->hdr.attrs; cur != link->next_attr; cur++) {
			if (end->flags & UVERBS_ATTR_F_VALID_OUTPUT &&
			    end->len) {
				VALGRIND_MAKE_MEM_DEFINED(
					(void *)(uintptr_t)end->data, end->len);
			}

			*cur = *end++;
		}
	}
}

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd)
{
       struct verbs_context *vctx = verbs_get_ctx(context);

	prepare_attrs(cmd);
	cmd->hdr.length = sizeof(cmd->hdr) +
		sizeof(cmd->hdr.attrs[0]) * cmd->hdr.num_attrs;

       cmd->hdr.driver_id = vctx->priv->driver_id;
	if (ioctl(context->cmd_fd, RDMA_VERBS_IOCTL, &cmd->hdr))
		return errno;

	finalize_attrs(cmd);

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
		fill_attr_in(cmdb, UVERBS_ATTR_UHW_IN,
			     cmd_total_len - cmd_common_len,
			     (const uint8_t *)cmd + cmd_common_len);
	if (resp_common_len < resp_total_len)
		fill_attr_in(cmdb, UVERBS_ATTR_UHW_OUT,
			     resp_total_len - resp_common_len,
			     (const uint8_t *)resp + resp_common_len);

	return execute_ioctl(context, cmdb);
}
