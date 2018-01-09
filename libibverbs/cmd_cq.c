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

#include <infiniband/verbs_write.h>

#include <ccan/container_of.h>

#include <stdbool.h>

static int xibv_cmd_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel, int comp_vector,
			      struct ibv_cq *cq,
			      struct ibv_command_buffer *link)
{
	DECLARE_COMMAND_BUFFER_FINAL(cmdb, UVERBS_OBJECT_CQ, UVERBS_METHOD_CQ_CREATE,
				     6, link);
	struct ib_uverbs_attr *handle;
	uint32_t resp_cqe;
	int ret;

	cq->context = context;

	handle = fill_attr_out_obj(cmdb, UVERBS_ATTR_CREATE_CQ_HANDLE);
	fill_attr_out_ptr(cmdb, UVERBS_ATTR_CREATE_CQ_RESP_CQE, &resp_cqe);

	fill_attr_in_uint32(cmdb, UVERBS_ATTR_CREATE_CQ_CQE, cqe);
	fill_attr_in_uint64(cmdb, UVERBS_ATTR_CREATE_CQ_USER_HANDLE, (uintptr_t)cq);
	if (channel)
		fill_attr_in_fd(cmdb, UVERBS_ATTR_CREATE_CQ_COMP_CHANNEL, channel->fd);
	fill_attr_in_uint32(cmdb, UVERBS_ATTR_CREATE_CQ_COMP_VECTOR, comp_vector);

	if (!execute_ioctl_fallback(cq->context, create_cq, cmdb, &ret)) {
		DECLARE_LEGACY_UHW_BUFS(link, struct ibv_create_cq,
					struct ibv_create_cq_resp);

		*req = (struct ibv_create_cq){
			.user_handle = (uintptr_t)cq,
			.cqe = cqe,
			.comp_vector = comp_vector,
			.comp_channel = channel ? channel->fd : -1,
		};

		ret = execute_write_bufs(IB_USER_VERBS_CMD_DESTROY_CQ,
					 cq->context, req, resp);

		cq->handle = resp->cq_handle;
		cq->cqe = resp->cqe;

		return ret;
	}

	cq->handle = handle->data;
	cq->cqe = resp_cqe;

	return ret;
}

int ibv_cmd_create_cq(struct ibv_context *context, int cqe,
		      struct ibv_comp_channel *channel, int comp_vector,
		      struct ibv_cq *cq, struct ibv_create_cq *cmd,
		      size_t cmd_size, struct ibv_create_cq_resp *resp,
		      size_t resp_size)
{
	DECLARE_CMD_BUFFER_COMPAT(cmdb, UVERBS_OBJECT_CQ, UVERBS_METHOD_CQ_CREATE);

	return xibv_cmd_create_cq(context, cqe, channel, comp_vector, cq, cmdb);
}

int ibv_cmd_destroy_cq(struct ibv_cq *cq)
{
	DECLARE_COMMAND_BUFFER(cmdb, UVERBS_OBJECT_CQ, UVERBS_METHOD_CQ_DESTROY, 2);
	struct ibv_destroy_cq_resp resp;
	int ret;

	fill_attr_out_ptr(cmdb, UVERBS_ATTR_DESTROY_CQ_RESP, &resp);
	fill_attr_in_obj(cmdb, UVERBS_ATTR_DESTROY_CQ_HANDLE, cq->handle);

	if (!execute_ioctl_fallback(cq->context, destroy_cq, cmdb, &ret)) {
		struct ibv_destroy_cq cmd = {.cq_handle = cq->handle};

		ret = execute_write(IB_USER_VERBS_CMD_DESTROY_CQ, cq->context,
				    cmd, resp);
	}

	if (ret)
		return ret;

	pthread_mutex_lock(&cq->mutex);
	while (cq->comp_events_completed != resp.comp_events_reported ||
	       cq->async_events_completed != resp.async_events_reported)
		pthread_cond_wait(&cq->cond, &cq->mutex);
	pthread_mutex_unlock(&cq->mutex);

	return 0;
}
