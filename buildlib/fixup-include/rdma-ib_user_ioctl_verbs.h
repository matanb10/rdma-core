/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
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

#ifndef IB_USER_IOCTL_VERBS_H
#define IB_USER_IOCTL_VERBS_H

#ifdef __KERNEL__
#define RDMA_UAPI_TYPE(_type)		ib_uverbs_ ## _type
#define RDMA_UAPI_CONST(_const)		IB_UVERBS ## _const
#else
#define RDMA_UAPI_TYPE(_type)		ibv_ ## _type
#define RDMA_UAPI_CONST(_const)		IBV_ ## _const
#endif

#if UINTPTR_MAX == UINT64_MAX
#define RDMA_UAPI_PTR(_type, _name)	_type _name
#elif UINTPTR_MAX == UINT32_MAX
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define RDMA_UAPI_PTR(_type, _name) union {
				       struct {_type _name;		\
					       __u32 _name ##_reserved;	\
				       };				\
				       __u64 _name ## _dummy;}
#else
#define RDMA_UAPI_PTR(_type, _name) union {
				       struct {__u32 _name ##_reserved;	\
					       _type _name;		\
				       };				\
				       __u64 _name ## _dummy;}
#endif
#else
#error "Pointer size not supported"
#endif

enum RDMA_UAPI_TYPE(flow_action_esp_keymat) {
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_KEYMAT_AES_GCM),
};

enum RDMA_UAPI_TYPE(flow_action_esp_keymat_aes_gcm_iv_algo) {
	RDMA_UAPI_CONST(FLOW_ACTION_IV_ALGO_SEQ),
};

struct RDMA_UAPI_TYPE(flow_action_esp_keymat_aes_gcm) {
	__u64	iv;
	__u32	iv_algo; /* Use enum ib_uverbs_flow_action_iv_algo */

	__u32   salt;
	__u32	icv_len;

	__u32	key_len;
	__u32   aes_key[256 / 32];
};

enum RDMA_UAPI_TYPE(flow_action_esp_replay) {
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_REPLAY_NONE),
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_REPLAY_BMP),
};

struct RDMA_UAPI_TYPE(flow_action_esp_replay_bmp) {
	__u32	size;
};

enum RDMA_UAPI_TYPE(flow_action_esp_flags) {
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_INLINE_CRYPTO)	= 0,	/* Default */
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_FULL_OFFLAOD)	= 1UL << 0,

	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_TUNNEL)		= 0,	/* Default */
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_TRANSPORT)	= 1UL << 1,

	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_DECRYPT)		= 0,	/* Default */
	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_ENCRYPT)		= 1UL << 2,

	RDMA_UAPI_CONST(FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW)	= 1UL << 3,
};

struct RDMA_UAPI_TYPE(flow_action_esp_encap) {
	RDMA_UAPI_PTR(struct RDMA_UAPI_TYPE(flow_action_esp_encap) *, mask_ptr);
	RDMA_UAPI_PTR(struct RDMA_UAPI_TYPE(flow_action_esp_encap) *, val_ptr);
	RDMA_UAPI_PTR(struct RDMA_UAPI_TYPE(flow_action_esp_encap) *, next_ptr);
	__u16	len;		/* Len of mask and pointer (separately) */
	__u16	type;		/* Use flow_spec enum */
};

struct RDMA_UAPI_TYPE(flow_action_esp) {
	__u32	spi;
	__u32	seq;
	__u32	tfc_pad;
	__u32	flags;
	__u64	hard_limit_pkts;
};

#endif

