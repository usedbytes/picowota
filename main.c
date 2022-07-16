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

#include "tcp_comm.h"

extern const char *wifi_ssid;
extern const char *wifi_pass;

#define TCP_PORT 4242

#define MAX_LEN 2048

#define CMD_SYNC          (('S' << 0) | ('Y' << 8) | ('N' << 16) | ('C' << 24))
#define RSP_SYNC          (('W' << 0) | ('O' << 8) | ('T' << 16) | ('A' << 24))

static uint32_t handle_sync(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	return RSP_SYNC;
}

const struct comm_command util_sync_cmd = {
	.opcode = CMD_SYNC,
	.nargs = 0,
	.resp_nargs = 0,
	.size = NULL,
	.handle = &handle_sync,
};

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

	const struct comm_command *cmds[] = {
		&util_sync_cmd,
	};

	struct tcp_comm_ctx *tcp = tcp_comm_new(cmds, 1, CMD_SYNC);

	for ( ; ; ) {
		err_t err = tcp_comm_listen(tcp, TCP_PORT);
		if (err != ERR_OK) {
			printf("Failed to start server: %d\n", err);
			sleep_ms(1000);
			continue;
		}

		while (!tcp_comm_server_done(tcp)) {
			cyw43_arch_poll();
			sleep_ms(10);
		}
	}

	cyw43_arch_deinit();
	return 0;
}
