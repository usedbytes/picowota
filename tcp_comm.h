/**
 * Copyright (c) 2022 Brian Starkey <stark3y@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __TCP_COMM_H__
#define __TCP_COMM_H__

#include <stdint.h>
#include <stdbool.h>

#define TCP_COMM_MAX_DATA_LEN 1024
#define TCP_COMM_RSP_OK       (('O' << 0) | ('K' << 8) | ('O' << 16) | ('K' << 24))
#define TCP_COMM_RSP_ERR      (('E' << 0) | ('R' << 8) | ('R' << 16) | ('!' << 24))


struct comm_command {
	uint32_t opcode;
	uint32_t nargs;
	uint32_t resp_nargs;
	uint32_t (*size)(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out);
	uint32_t (*handle)(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
};

struct tcp_comm_ctx;

err_t tcp_comm_listen(struct tcp_comm_ctx *ctx, uint16_t port);
err_t tcp_comm_server_close(struct tcp_comm_ctx *ctx);
bool tcp_comm_server_done(struct tcp_comm_ctx *ctx);

struct tcp_comm_ctx *tcp_comm_new(const struct comm_command *const *cmds,
		unsigned int n_cmds, uint32_t sync_opcode);
void tcp_comm_delete(struct tcp_comm_ctx *ctx);

#endif /* __TCP_COMM_H__ */
