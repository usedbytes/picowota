/**
 * Copyright (c) 2022 Brian Starkey <stark3y@gmail.com>
 *
 * Based on the Pico W tcp_server example:
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

extern const char *wifi_ssid;
extern const char *wifi_pass;

#define TCP_PORT 4242
#define DEBUG_printf printf
#define POLL_TIME_S 5

#define MAX_LEN 2048

enum conn_state {
	CONN_STATE_WAIT_FOR_SYNC,
	CONN_STATE_READ_OPCODE,
	CONN_STATE_READ_ARGS,
	CONN_STATE_READ_DATA,
	CONN_STATE_HANDLE,
	CONN_STATE_WRITE_RESP,
	CONN_STATE_ERROR,
	CONN_STATE_CLOSED,
};

struct tcp_ctx {
	struct tcp_pcb *serv_pcb;
	volatile bool serv_done;

	struct tcp_pcb *conn_pcb;
	uint8_t buf[MAX_LEN];
	size_t bytes_remaining;
	enum conn_state conn_state;
};

static err_t tcp_conn_close(struct tcp_ctx *ctx)
{
	err_t err = ERR_OK;

	cyw43_arch_gpio_put (0, false);
	ctx->conn_state = CONN_STATE_CLOSED;

	if (!ctx->conn_pcb) {
		return err;
	}

	tcp_arg(ctx->conn_pcb, NULL);
	tcp_poll(ctx->conn_pcb, NULL, 0);
	tcp_sent(ctx->conn_pcb, NULL);
	tcp_recv(ctx->conn_pcb, NULL);
	tcp_err(ctx->conn_pcb, NULL);
	err = tcp_close(ctx->conn_pcb);
	if (err != ERR_OK) {
		DEBUG_printf("close failed %d, calling abort\n", err);
		tcp_abort(ctx->conn_pcb);
		err = ERR_ABRT;
	}

	ctx->conn_pcb = NULL;

	return err;
}

static err_t tcp_server_close(void *arg)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;
	err_t err = ERR_OK;

	err = tcp_conn_close(ctx);
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

static void tcp_server_complete(void *arg, int status)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;
	if (status == 0) {
		DEBUG_printf("server completed normally\n");
	} else {
		DEBUG_printf("server error %d\n", status);
	}

	tcp_server_close(ctx);
	ctx->serv_done = true;
}

static err_t tcp_conn_complete(void *arg, int status)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;
	if (status == 0) {
		DEBUG_printf("conn completed normally\n");
	} else {
		DEBUG_printf("conn error %d\n", status);
	}
	return tcp_conn_close(ctx);
}

static err_t tcp_conn_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;
	DEBUG_printf("tcp_server_sent %u\n", len);

	return ERR_OK;
}

static err_t tcp_conn_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;
	if (!p) {
		return tcp_conn_complete(ctx, 0);
	}

	// this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
	// can use this method to cause an assertion in debug mode, if this method is called when
	// cyw43_arch_lwip_begin IS needed
	cyw43_arch_lwip_check();
	if (p->tot_len > 0) {
		DEBUG_printf("tcp_server_recv %d err %d\n", p->tot_len, err);

		// Receive the buffer
		pbuf_copy_partial(p, ctx->buf, p->tot_len, 0);

		ctx->buf[p->tot_len] = '\0';
		printf("%s\n", ctx->buf);
		tcp_recved(tpcb, p->tot_len);
	}
	pbuf_free(p);

	return ERR_OK;
}

static err_t tcp_conn_poll(void *arg, struct tcp_pcb *tpcb)
{
	DEBUG_printf("tcp_server_poll_fn\n");
	return ERR_OK;
}

static void tcp_conn_err(void *arg, err_t err)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;

	DEBUG_printf("tcp_conn_err %d\n", err);

	ctx->conn_pcb = NULL;
	ctx->conn_state = CONN_STATE_CLOSED;
	ctx->bytes_remaining = 0;
	cyw43_arch_gpio_put (0, false);
}

static void tcp_conn_wait_for_sync(struct tcp_ctx *ctx)
{
	ctx->conn_state = CONN_STATE_WAIT_FOR_SYNC;
	ctx->bytes_remaining = 4;
}

static void tcp_conn_init(struct tcp_ctx *ctx, struct tcp_pcb *pcb)
{
	ctx->conn_pcb = pcb;
	tcp_arg(pcb, ctx);
	tcp_sent(pcb, tcp_conn_sent);
	tcp_recv(pcb, tcp_conn_recv);
	tcp_poll(pcb, tcp_conn_poll, POLL_TIME_S * 2);
	tcp_err(pcb, tcp_conn_err);

	cyw43_arch_gpio_put (0, true);

	tcp_conn_wait_for_sync(ctx);
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err)
{
	struct tcp_ctx *ctx = (struct tcp_ctx *)arg;

	if (err != ERR_OK || client_pcb == NULL) {
		DEBUG_printf("Failure in accept\n");
		tcp_server_complete(ctx, err);
		return ERR_VAL;
	}
	DEBUG_printf("Connection opened\n");

	if (ctx->conn_pcb) {
		DEBUG_printf("Already have a connection\n");
		tcp_abort(client_pcb);
		return ERR_ABRT;
	}

	tcp_conn_init(ctx, client_pcb);

	return ERR_OK;
}

static err_t tcp_server_listen(struct tcp_ctx *ctx)
{
	DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

	ctx->serv_done = false;

	struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (!pcb) {
		DEBUG_printf("failed to create pcb\n");
		return ERR_MEM;
	}

	err_t err = tcp_bind(pcb, NULL, TCP_PORT);
	if (err) {
		DEBUG_printf("failed to bind to port %d\n", TCP_PORT);
		tcp_abort(pcb);
		return err;
	}

	ctx->serv_pcb = tcp_listen_with_backlog_and_err(pcb, 1, &err);
	if (!ctx->serv_pcb) {
		DEBUG_printf("failed to listen: %d\n", err);
		return err;
	}

	tcp_arg(ctx->serv_pcb, ctx);
	tcp_accept(ctx->serv_pcb, tcp_server_accept);

	return ERR_OK;
}

int main()
{
	stdio_init_all();

	sleep_ms(1000);

	if (cyw43_arch_init()) {
		printf("failed to initialise\n");
		return 1;
	}

	cyw43_arch_enable_sta_mode();

	printf("Connecting to WiFi...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(wifi_ssid, wifi_pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
		printf("failed to connect.\n");
		return 1;
	} else {
		printf("Connected.\n");
	}

	struct tcp_ctx tcp = { 0 };

	for ( ; ; ) {
		err_t err = tcp_server_listen(&tcp);
		if (err != ERR_OK) {
			printf("Failed to start server: %d\n", err);
			sleep_ms(1000);
			continue;
		}

		while (!tcp.serv_done) {
			sleep_ms(1000);
		}
	}

	cyw43_arch_deinit();
	return 0;
}
