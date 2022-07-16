/**
 * Copyright (c) 2022 Brian Starkey <stark3y@gmail.com>
 *
 * Parts based on the Pico W tcp_server example:
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>

#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "tcp_comm.h"

#define DEBUG_printf printf
#define POLL_TIME_S 5

#define COMM_MAX_NARG     5
#define COMM_MAX_DATA_LEN 1024

#define COMM_RSP_OK       (('O' << 0) | ('K' << 8) | ('O' << 16) | ('K' << 24))
#define COMM_RSP_ERR      (('E' << 0) | ('R' << 8) | ('R' << 16) | ('!' << 24))

enum conn_state {
	CONN_STATE_WAIT_FOR_SYNC,
	CONN_STATE_READ_OPCODE,
	CONN_STATE_READ_ARGS,
	CONN_STATE_READ_DATA,
	CONN_STATE_HANDLE,
	CONN_STATE_WRITE_RESP,
	CONN_STATE_WRITE_ERROR,
	CONN_STATE_CLOSED,
};

struct tcp_comm_ctx {
	struct tcp_pcb *serv_pcb;
	volatile bool serv_done;
	enum conn_state conn_state;

	struct tcp_pcb *client_pcb;
	uint8_t buf[(sizeof(uint32_t) * (1 + COMM_MAX_NARG)) + COMM_MAX_DATA_LEN];
	uint16_t rx_bytes_received;
	uint16_t rx_bytes_remaining;

	uint16_t tx_bytes_sent;
	uint16_t tx_bytes_remaining;

	uint32_t resp_data_len;

	const struct comm_command *cmd;
	const struct comm_command *const *cmds;
	unsigned int n_cmds;
	uint32_t sync_opcode;
};

#define COMM_BUF_OPCODE(_buf)       ((uint32_t *)((uint8_t *)(_buf)))
#define COMM_BUF_ARGS(_buf)         ((uint32_t *)((uint8_t *)(_buf) + sizeof(uint32_t)))
#define COMM_BUF_BODY(_buf, _nargs) ((uint8_t *)(_buf) + (sizeof(uint32_t) * ((_nargs) + 1)))

static const struct comm_command *find_command_desc(struct tcp_comm_ctx *ctx, uint32_t opcode)
{
	unsigned int i;

	for (i = 0; i < ctx->n_cmds; i++) {
		if (ctx->cmds[i]->opcode == opcode) {
			return ctx->cmds[i];
		}
	}

	return NULL;
}

static bool is_error(uint32_t status)
{
	return status == COMM_RSP_ERR;
}

static int tcp_comm_sync_begin(struct tcp_comm_ctx *ctx);
static int tcp_comm_sync_complete(struct tcp_comm_ctx *ctx);
static int tcp_comm_opcode_begin(struct tcp_comm_ctx *ctx);
static int tcp_comm_opcode_complete(struct tcp_comm_ctx *ctx);
static int tcp_comm_args_begin(struct tcp_comm_ctx *ctx);
static int tcp_comm_args_complete(struct tcp_comm_ctx *ctx);
static int tcp_comm_data_begin(struct tcp_comm_ctx *ctx, uint32_t data_len);
static int tcp_comm_data_complete(struct tcp_comm_ctx *ctx);
static int tcp_comm_response_begin(struct tcp_comm_ctx *ctx);
static int tcp_comm_response_complete(struct tcp_comm_ctx *ctx);
static int tcp_comm_error_begin(struct tcp_comm_ctx *ctx);

static int tcp_comm_sync_begin(struct tcp_comm_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_WAIT_FOR_SYNC;
	ctx->rx_bytes_received = 0;
	ctx->rx_bytes_remaining = sizeof(uint32_t);

	DEBUG_printf("sync_begin %d\n", ctx->rx_bytes_remaining);
}

static int tcp_comm_sync_complete(struct tcp_comm_ctx *ctx)
{
	if (ctx->sync_opcode != *COMM_BUF_OPCODE(ctx->buf)) {
		DEBUG_printf("sync not correct: %c%c%c%c\n", ctx->buf[0], ctx->buf[1], ctx->buf[2], ctx->buf[3]);
		return tcp_comm_error_begin(ctx);
	}

	return tcp_comm_opcode_complete(ctx);
}

static int tcp_comm_opcode_begin(struct tcp_comm_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_READ_OPCODE;
	ctx->rx_bytes_received = 0;
	ctx->rx_bytes_remaining = sizeof(uint32_t);

	return 0;
}

static int tcp_comm_opcode_complete(struct tcp_comm_ctx *ctx)
{
	ctx->cmd = find_command_desc(ctx, *COMM_BUF_OPCODE(ctx->buf));
	if (!ctx->cmd) {
		DEBUG_printf("no command for '%c%c%c%c'\n", ctx->buf[0], ctx->buf[1], ctx->buf[2], ctx->buf[3]);
		return tcp_comm_error_begin(ctx);
	} else {
		DEBUG_printf("got command '%c%c%c%c'\n", ctx->buf[0], ctx->buf[1], ctx->buf[2], ctx->buf[3]);
	}

	return tcp_comm_args_begin(ctx);
}

static int tcp_comm_args_begin(struct tcp_comm_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_READ_ARGS;
	ctx->rx_bytes_received = 0;
	ctx->rx_bytes_remaining = ctx->cmd->nargs * sizeof(uint32_t);

	if (ctx->cmd->nargs == 0) {
		return tcp_comm_args_complete(ctx);
	}

	return 0;
}

static int tcp_comm_args_complete(struct tcp_comm_ctx *ctx)
{
	const struct comm_command *cmd = ctx->cmd;

	uint32_t data_len = 0;

	if (cmd->size) {
		uint32_t status = cmd->size(COMM_BUF_ARGS(ctx->buf),
					    &data_len,
					    &ctx->resp_data_len);
		if (is_error(status)) {
			return tcp_comm_error_begin(ctx);
		}
	}

	return tcp_comm_data_begin(ctx, data_len);
}

static int tcp_comm_data_begin(struct tcp_comm_ctx *ctx, uint32_t data_len)
{
	const struct comm_command *cmd = ctx->cmd;

	ctx->conn_state = CONN_STATE_READ_DATA;
	ctx->rx_bytes_received = 0;
	ctx->rx_bytes_remaining = data_len;

	if (data_len == 0) {
		return tcp_comm_data_complete(ctx);
	}


	return 0;
}

static int tcp_comm_data_complete(struct tcp_comm_ctx *ctx)
{
	const struct comm_command *cmd = ctx->cmd;

	if (cmd->handle) {
		uint32_t status = cmd->handle(COMM_BUF_ARGS(ctx->buf),
					      COMM_BUF_BODY(ctx->buf, cmd->nargs),
					      COMM_BUF_ARGS(ctx->buf),
					      COMM_BUF_BODY(ctx->buf, cmd->resp_nargs));
		if (is_error(status)) {
			return tcp_comm_error_begin(ctx);
		}

		*COMM_BUF_OPCODE(ctx->buf) = status;
	} else {
		// TODO: Should we just assert(desc->handle)?
		*COMM_BUF_OPCODE(ctx->buf) = COMM_RSP_OK;
	}

	return tcp_comm_response_begin(ctx);
}

static int tcp_comm_response_begin(struct tcp_comm_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_WRITE_RESP;
	ctx->tx_bytes_sent = 0;
	ctx->tx_bytes_remaining = ctx->resp_data_len + ((ctx->cmd->resp_nargs + 1) * sizeof(uint32_t));

	err_t err = tcp_write(ctx->client_pcb, ctx->buf, ctx->tx_bytes_remaining, 0);
	if (err != ERR_OK) {
		return -1;
	}

	return 0;
}

static int tcp_comm_error_begin(struct tcp_comm_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_WRITE_ERROR;
	ctx->tx_bytes_sent = 0;
	ctx->tx_bytes_remaining = sizeof(uint32_t);

	*COMM_BUF_OPCODE(ctx->buf) = COMM_RSP_ERR;

	err_t err = tcp_write(ctx->client_pcb, ctx->buf, ctx->tx_bytes_remaining, 0);
	if (err != ERR_OK) {
		return -1;
	}

	return 0;
}


static int tcp_comm_response_complete(struct tcp_comm_ctx *ctx)
{
	return tcp_comm_opcode_begin(ctx);
}

static int tcp_comm_rx_complete(struct tcp_comm_ctx *ctx)
{
	switch (ctx->conn_state) {
	case CONN_STATE_WAIT_FOR_SYNC:
		return tcp_comm_sync_complete(ctx);
	case CONN_STATE_READ_OPCODE:
		return tcp_comm_opcode_complete(ctx);
	case CONN_STATE_READ_ARGS:
		return tcp_comm_args_complete(ctx);
	case CONN_STATE_READ_DATA:
		return tcp_comm_data_complete(ctx);
	default:
		return -1;
	}
}

static int tcp_comm_tx_complete(struct tcp_comm_ctx *ctx)
{
	switch (ctx->conn_state) {
	case CONN_STATE_WRITE_RESP:
		return tcp_comm_response_complete(ctx);
	case CONN_STATE_WRITE_ERROR:
		return -1;
	default:
		return -1;
	}
}

static err_t tcp_comm_client_close(struct tcp_comm_ctx *ctx)
{
	err_t err = ERR_OK;

	cyw43_arch_gpio_put (0, false);
	ctx->conn_state = CONN_STATE_CLOSED;

	if (!ctx->client_pcb) {
		return err;
	}

	tcp_arg(ctx->client_pcb, NULL);
	tcp_poll(ctx->client_pcb, NULL, 0);
	tcp_sent(ctx->client_pcb, NULL);
	tcp_recv(ctx->client_pcb, NULL);
	tcp_err(ctx->client_pcb, NULL);
	err = tcp_close(ctx->client_pcb);
	if (err != ERR_OK) {
		DEBUG_printf("close failed %d, calling abort\n", err);
		tcp_abort(ctx->client_pcb);
		err = ERR_ABRT;
	}

	ctx->client_pcb = NULL;

	return err;
}

err_t tcp_comm_server_close(struct tcp_comm_ctx *ctx)
{
	err_t err = ERR_OK;

	err = tcp_comm_client_close(ctx);
	if ((err != ERR_OK) && ctx->serv_pcb) {
		tcp_arg(ctx->serv_pcb, NULL);
		tcp_abort(ctx->serv_pcb);
		ctx->serv_pcb = NULL;
		return ERR_ABRT;
	}

	if (!ctx->serv_pcb) {
		return err;
	}

	tcp_arg(ctx->serv_pcb, NULL);
	err = tcp_close(ctx->serv_pcb);
	if (err != ERR_OK) {
		tcp_abort(ctx->serv_pcb);
		err = ERR_ABRT;
	}
	ctx->serv_pcb = NULL;

	return err;
}

static void tcp_comm_server_complete(void *arg, int status)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;
	if (status == 0) {
		DEBUG_printf("server completed normally\n");
	} else {
		DEBUG_printf("server error %d\n", status);
	}

	tcp_comm_server_close(ctx);
	ctx->serv_done = true;
}

static err_t tcp_comm_client_complete(void *arg, int status)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;
	if (status == 0) {
		DEBUG_printf("conn completed normally\n");
	} else {
		DEBUG_printf("conn error %d\n", status);
	}
	return tcp_comm_client_close(ctx);
}

static err_t tcp_comm_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;
	DEBUG_printf("tcp_comm_server_sent %u\n", len);

	cyw43_arch_lwip_check();
	if (len > ctx->tx_bytes_remaining) {
		DEBUG_printf("tx len %d > remaining %d\n", len, ctx->tx_bytes_remaining);
		return tcp_comm_client_complete(ctx, ERR_ARG);
	}

	ctx->tx_bytes_remaining -= len;
	ctx->tx_bytes_sent += len;

	if (ctx->tx_bytes_remaining == 0) {
		int res = tcp_comm_tx_complete(ctx);
		if (res) {
			return tcp_comm_client_complete(ctx, ERR_ARG);
		}
	}

	return ERR_OK;
}

static err_t tcp_comm_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;
	if (!p) {
		DEBUG_printf("no pbuf\n");
		return tcp_comm_client_complete(ctx, 0);
	}

	// this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
	// can use this method to cause an assertion in debug mode, if this method is called when
	// cyw43_arch_lwip_begin IS needed
	cyw43_arch_lwip_check();
	if (p->tot_len > 0) {
		DEBUG_printf("tcp_comm_server_recv %d err %d\n", p->tot_len, err);

		if (p->tot_len > ctx->rx_bytes_remaining) {
			DEBUG_printf("more data than expected: %d vs %d\n", p->tot_len, ctx->rx_bytes_remaining);
			// TODO: Invoking the error response state here feels
			// like a bit of a layering violation, but this is a
			// protocol error, rather than a failure in the stack
			// somewhere, so it's nice to try and report it rather
			// than just dropping the connection.
			if (tcp_comm_error_begin(ctx)) {
				return tcp_comm_client_complete(ctx, ERR_ARG);
			}
			return ERR_OK;
		}

		// Receive the buffer
		if (pbuf_copy_partial(p, ctx->buf + ctx->rx_bytes_received, p->tot_len, 0) != p->tot_len) {
			DEBUG_printf("wrong copy len\n");
			return tcp_comm_client_complete(ctx, ERR_ARG);
		}

		ctx->rx_bytes_received += p->tot_len;
		ctx->rx_bytes_remaining -= p->tot_len;
		tcp_recved(tpcb, p->tot_len);

		if (ctx->rx_bytes_remaining == 0) {
			int res = tcp_comm_rx_complete(ctx);
			if (res) {
				return tcp_comm_client_complete(ctx, ERR_ARG);
			}
		}
	}
	pbuf_free(p);

	return ERR_OK;
}

static err_t tcp_comm_client_poll(void *arg, struct tcp_pcb *tpcb)
{
	DEBUG_printf("tcp_comm_server_poll_fn\n");
	return ERR_OK;
}

static void tcp_comm_client_err(void *arg, err_t err)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;

	DEBUG_printf("tcp_comm_err %d\n", err);

	ctx->client_pcb = NULL;
	ctx->conn_state = CONN_STATE_CLOSED;
	ctx->rx_bytes_remaining = 0;
	cyw43_arch_gpio_put (0, false);
}

static void tcp_comm_client_init(struct tcp_comm_ctx *ctx, struct tcp_pcb *pcb)
{
	ctx->client_pcb = pcb;
	tcp_arg(pcb, ctx);

	cyw43_arch_gpio_put (0, true);

	tcp_comm_sync_begin(ctx);

	tcp_sent(pcb, tcp_comm_client_sent);
	tcp_recv(pcb, tcp_comm_client_recv);
	tcp_poll(pcb, tcp_comm_client_poll, POLL_TIME_S * 2);
	tcp_err(pcb, tcp_comm_client_err);
}

static err_t tcp_comm_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err)
{
	struct tcp_comm_ctx *ctx = (struct tcp_comm_ctx *)arg;

	if (err != ERR_OK || client_pcb == NULL) {
		DEBUG_printf("Failure in accept\n");
		tcp_comm_server_complete(ctx, err);
		return ERR_VAL;
	}
	DEBUG_printf("Connection opened\n");

	if (ctx->client_pcb) {
		DEBUG_printf("Already have a connection\n");
		tcp_abort(client_pcb);
		return ERR_ABRT;
	}

	tcp_comm_client_init(ctx, client_pcb);

	return ERR_OK;
}

err_t tcp_comm_listen(struct tcp_comm_ctx *ctx, uint16_t port)
{
	DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), port);

	ctx->serv_done = false;

	struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (!pcb) {
		DEBUG_printf("failed to create pcb\n");
		return ERR_MEM;
	}

	err_t err = tcp_bind(pcb, NULL, port);
	if (err) {
		DEBUG_printf("failed to bind to port %d\n", port);
		tcp_abort(pcb);
		return err;
	}

	ctx->serv_pcb = tcp_listen_with_backlog_and_err(pcb, 1, &err);
	if (!ctx->serv_pcb) {
		DEBUG_printf("failed to listen: %d\n", err);
		return err;
	}

	tcp_arg(ctx->serv_pcb, ctx);
	tcp_accept(ctx->serv_pcb, tcp_comm_server_accept);

	return ERR_OK;
}

struct tcp_comm_ctx *tcp_comm_new(const struct comm_command *const *cmds,
		unsigned int n_cmds, uint32_t sync_opcode)
{
	struct tcp_comm_ctx *ctx = calloc(1, sizeof(struct tcp_comm_ctx));
	if (!ctx) {
		return NULL;
	}

	unsigned int i;
	for (i = 0; i < n_cmds; i++) {
		assert(cmds[i]->nargs <= MAX_NARG);
		assert(cmds[i]->resp_nargs <= MAX_NARG);
	}

	ctx->cmds = cmds;
	ctx->n_cmds = n_cmds;
	ctx->sync_opcode = sync_opcode;

	return ctx;
}

void tcp_comm_delete(struct tcp_comm_ctx *ctx)
{
	tcp_comm_server_close(ctx);
	free(ctx);
}

bool tcp_comm_server_done(struct tcp_comm_ctx *ctx)
{
	return ctx->serv_done;
}
