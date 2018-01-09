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

int _ioctl_execute_or_fallback(struct ibv_context *context,
			       unsigned int cmd_id,
			       int (*write_fn)(struct ibv_context *context,
					       struct ibv_command_buffer *cmdb))
{
}

static int ibv_cmd_destroy_cq_write(struct ibv_cq *cq,
				    struct ibv_destroy_cq_resp resp);

int ibv_cmd_destroy_cq_write(struct ibv_context *context,
			     struct ibv_command_buffer *cmdb)
{
	struct ibv_destroy_cq      cmd;
	struct ibv_destroy_cq_resp resp;

	IBV_INIT_CMD_RESP(&cmd, sizeof(cmd), DESTROY_CQ,
			  cmdb->hdr.attrs[0]
			  &resp, sizeof(resp));
	cmd.cq_handle = cq->handle;
	cmd.reserved  = 0;

	if (write(cq->context->cmd_fd, &cmd, sizeof cmd) != sizeof cmd)
		return errno;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	return 0;
}

int ibv_cmd_destroy_cq_ioctl(struct ibv_cq *cq); // TMP
int ibv_cmd_destroy_cq_ioctl(struct ibv_cq *cq)
{
	DECLARE_COMMAND_BUFFER(cmdb, UVERBS_OBJECT_CQ, UVERBS_CQ_DESTROY, 2);
	struct ibv_destroy_cq_resp resp;
	int ret;

	fill_attr_out_ptr(cmdb, DESTROY_CQ_RESP, &resp);
	fill_attr_obj_in(cmdb, DESTROY_CQ_HANDLE, cq->handle);

	ret = ioctl_execute_or_fallback(cq->context, destroy_cq, cmdb);

	ret = execute_ioctl(cq->context, cmdb);
	if (ret)
		return ret;

	pthread_mutex_lock(&cq->mutex);
	while (cq->comp_events_completed  != resp.comp_events_reported ||
	       cq->async_events_completed != resp.async_events_reported)
		pthread_cond_wait(&cq->cond, &cq->mutex);
	pthread_mutex_unlock(&cq->mutex);

	return 0;
}
