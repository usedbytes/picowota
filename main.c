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

#include "RP2040.h"
#include "pico/critical_section.h"
#include "pico/time.h"
#include "pico/util/queue.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/structs/dma.h"
#include "hardware/structs/watchdog.h"
#include "hardware/gpio.h"
#include "hardware/resets.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#if PICOWOTA_ENTRY_AUTORE == 1
#include "pico/multicore.h"
#endif

#include "tcp_comm.h"

#include "picowota/reboot.h"

#ifdef DEBUG
#include <stdio.h>
#include "pico/stdio_usb.h"
#define DBG_PRINTF_INIT() stdio_usb_init()
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF_INIT() { }
#define DBG_PRINTF(...) { }
#endif

#if PICOWOTA_WIFI_AP == 1
#include "dhcpserver.h"
static dhcp_server_t dhcp_server;
#endif

#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)

#ifndef PICOWOTA_WIFI_SSID
#warning "PICOWOTA_WIFI_SSID not defined"
#else
const char *wifi_ssid = STR(PICOWOTA_WIFI_SSID);
#endif

#ifndef PICOWOTA_WIFI_PASS
#warning "PICOWOTA_WIFI_PASS not defined"
#else
const char *wifi_pass = STR(PICOWOTA_WIFI_PASS);
#endif

critical_section_t critical_section;

#define EVENT_QUEUE_LENGTH 8
queue_t event_queue;

enum event_type {
	EVENT_TYPE_REBOOT = 1,
	EVENT_TYPE_GO,
	EVENT_TYPE_SERVER_DONE,
};

struct event {
	enum event_type type;
	union {
		struct {
			bool to_bootloader;
		} reboot;
		struct {
			uint32_t vtor;
		} go;
	};
};

#ifndef PICOWOTA_ENTRY_PIN
#define BOOTLOADER_ENTRY_PIN 15
#else
#define BOOTLOADER_ENTRY_PIN PICOWOTA_ENTRY_PIN
#endif

#define TCP_PORT 4242

struct image_header app_image_header;
#define IMAGE_HEADER_ADDR   ((uint32_t)&app_image_header)
#define IMAGE_HEADER_OFFSET (IMAGE_HEADER_ADDR - XIP_BASE)

#define WRITE_ADDR_MIN (IMAGE_HEADER_ADDR + FLASH_SECTOR_SIZE)
#define ERASE_ADDR_MIN (IMAGE_HEADER_ADDR)
#define FLASH_ADDR_MAX (XIP_BASE + PICO_FLASH_SIZE_BYTES)

#define CMD_SYNC          (('S' << 0) | ('Y' << 8) | ('N' << 16) | ('C' << 24))
#define RSP_SYNC          (('W' << 0) | ('O' << 8) | ('T' << 16) | ('A' << 24))
#define CMD_INFO          (('I' << 0) | ('N' << 8) | ('F' << 16) | ('O' << 24))

#define CMD_READ   (('R' << 0) | ('E' << 8) | ('A' << 16) | ('D' << 24))
#define CMD_CSUM   (('C' << 0) | ('S' << 8) | ('U' << 16) | ('M' << 24))
#define CMD_CRC    (('C' << 0) | ('R' << 8) | ('C' << 16) | ('C' << 24))
#define CMD_ERASE  (('E' << 0) | ('R' << 8) | ('A' << 16) | ('S' << 24))
#define CMD_WRITE  (('W' << 0) | ('R' << 8) | ('I' << 16) | ('T' << 24))
#define CMD_SEAL   (('S' << 0) | ('E' << 8) | ('A' << 16) | ('L' << 24))
#define CMD_GO     (('G' << 0) | ('O' << 8) | ('G' << 16) | ('O' << 24))
#define CMD_REBOOT (('B' << 0) | ('O' << 8) | ('O' << 16) | ('T' << 24))

static uint32_t handle_sync(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	return RSP_SYNC;
}

const struct comm_command sync_cmd = {
	.opcode = CMD_SYNC,
	.nargs = 0,
	.resp_nargs = 0,
	.size = NULL,
	.handle = &handle_sync,
};

static uint32_t size_read(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t size = args_in[1];
	if (size > TCP_COMM_MAX_DATA_LEN) {
		return TCP_COMM_RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = size;

	return TCP_COMM_RSP_OK;
}

static uint32_t handle_read(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	memcpy(resp_data_out, (void *)addr, size);

	return TCP_COMM_RSP_OK;
}

const struct comm_command read_cmd = {
	// READ addr len
	// OKOK [data]
	.opcode = CMD_READ,
	.nargs = 2,
	.resp_nargs = 0,
	.size = &size_read,
	.handle = &handle_read,
};

static uint32_t size_csum(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr & 0x3) || (size & 0x3)) {
		// Must be aligned
		return TCP_COMM_RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = 0;

	return TCP_COMM_RSP_OK;
}

static uint32_t handle_csum(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t dummy_dest;
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	int channel = dma_claim_unused_channel(true);

	dma_channel_config c = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_sniff_enable(&c, true);

	dma_hw->sniff_data = 0;
	dma_sniffer_enable(channel, 0xf, true);

	dma_channel_configure(channel, &c, &dummy_dest, (void *)addr, size / 4, true);

	dma_channel_wait_for_finish_blocking(channel);

	dma_sniffer_disable();
	dma_channel_unclaim(channel);

	*resp_args_out = dma_hw->sniff_data;

	return TCP_COMM_RSP_OK;
}

struct comm_command csum_cmd = {
	// CSUM addr len
	// OKOK csum
	.opcode = CMD_CSUM,
	.nargs = 2,
	.resp_nargs = 1,
	.size = &size_csum,
	.handle = &handle_csum,
};

static uint32_t size_crc(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr & 0x3) || (size & 0x3)) {
		// Must be aligned
		return TCP_COMM_RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = 0;

	return TCP_COMM_RSP_OK;
}

// ptr must be 4-byte aligned and len must be a multiple of 4
static uint32_t calc_crc32(void *ptr, uint32_t len)
{
	uint32_t dummy_dest, crc;

	int channel = dma_claim_unused_channel(true);
	dma_channel_config c = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_sniff_enable(&c, true);

	// Seed the CRC calculation
	dma_hw->sniff_data = 0xffffffff;

	// Mode 1, then bit-reverse the result gives the same result as
	// golang's IEEE802.3 implementation
	dma_sniffer_enable(channel, 0x1, true);
	dma_hw->sniff_ctrl |= DMA_SNIFF_CTRL_OUT_REV_BITS;

	dma_channel_configure(channel, &c, &dummy_dest, ptr, len / 4, true);

	dma_channel_wait_for_finish_blocking(channel);

	// Read the result before resetting
	crc = dma_hw->sniff_data ^ 0xffffffff;

	dma_sniffer_disable();
	dma_channel_unclaim(channel);

	return crc;
}

static uint32_t handle_crc(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	resp_args_out[0] = calc_crc32((void *)addr, size);

	return TCP_COMM_RSP_OK;
}

struct comm_command crc_cmd = {
	// CRCC addr len
	// OKOK crc
	.opcode = CMD_CRC,
	.nargs = 2,
	.resp_nargs = 1,
	.size = &size_crc,
	.handle = &handle_crc,
};

static uint32_t handle_erase(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr < ERASE_ADDR_MIN) || (addr + size >= FLASH_ADDR_MAX)) {
		// Outside flash
		return TCP_COMM_RSP_ERR;
	}

	if ((addr & (FLASH_SECTOR_SIZE - 1)) || (size & (FLASH_SECTOR_SIZE - 1))) {
		// Must be aligned
		return TCP_COMM_RSP_ERR;
	}

	critical_section_enter_blocking(&critical_section);
	flash_range_erase(addr - XIP_BASE, size);
	critical_section_exit(&critical_section);

	return TCP_COMM_RSP_OK;
}

struct comm_command erase_cmd = {
	// ERAS addr len
	// OKOK
	.opcode = CMD_ERASE,
	.nargs = 2,
	.resp_nargs = 0,
	.size = NULL,
	.handle = &handle_erase,
};

static uint32_t size_write(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr < WRITE_ADDR_MIN) || (addr + size >= FLASH_ADDR_MAX)) {
		// Outside flash
		return TCP_COMM_RSP_ERR;
	}

	if ((addr & (FLASH_PAGE_SIZE - 1)) || (size & (FLASH_PAGE_SIZE -1))) {
		// Must be aligned
		return TCP_COMM_RSP_ERR;
	}

	if (size > TCP_COMM_MAX_DATA_LEN) {
		return TCP_COMM_RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = size;
	*resp_data_len_out = 0;

	return TCP_COMM_RSP_OK;
}

static uint32_t handle_write(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	critical_section_enter_blocking(&critical_section);
	flash_range_program(addr - XIP_BASE, data_in, size);
	critical_section_exit(&critical_section);

	resp_args_out[0] = calc_crc32((void *)addr, size);

	return TCP_COMM_RSP_OK;
}

struct comm_command write_cmd = {
	// WRIT addr len [data]
	// OKOK crc
	.opcode = CMD_WRITE,
	.nargs = 2,
	.resp_nargs = 1,
	.size = &size_write,
	.handle = &handle_write,
};

struct image_header {
	uint32_t vtor;
	uint32_t size;
	uint32_t crc;
	uint8_t pad[FLASH_PAGE_SIZE - (3 * 4)];
};
static_assert(sizeof(struct image_header) == FLASH_PAGE_SIZE, "image_header must be FLASH_PAGE_SIZE bytes");

static bool image_header_ok(struct image_header *hdr)
{
	uint32_t *vtor = (uint32_t *)hdr->vtor;

	uint32_t calc = calc_crc32((void *)hdr->vtor, hdr->size);

	// CRC has to match
	if (calc != hdr->crc) {
		return false;
	}

	// Stack pointer needs to be in RAM
	if (vtor[0] < SRAM_BASE) {
		return false;
	}

	// Reset vector should be in the image, and thumb (bit 0 set)
	if ((vtor[1] < hdr->vtor) || (vtor[1] > hdr->vtor + hdr->size) || !(vtor[1] & 1)) {
		return false;
	}

	// Looks OK.
	return true;
}


static uint32_t handle_seal(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	struct image_header hdr = {
		.vtor = args_in[0],
		.size = args_in[1],
		.crc = args_in[2],
	};

	if ((hdr.vtor & 0xff) || (hdr.size & 0x3)) {
		// Must be aligned
		return TCP_COMM_RSP_ERR;
	}

	if (!image_header_ok(&hdr)) {
		return TCP_COMM_RSP_ERR;
	}

	critical_section_enter_blocking(&critical_section);
	flash_range_erase(IMAGE_HEADER_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(IMAGE_HEADER_OFFSET, (const uint8_t *)&hdr, sizeof(hdr));
	critical_section_exit(&critical_section);

	struct image_header *check = &app_image_header;
	if (memcmp(&hdr, check, sizeof(hdr))) {
		return TCP_COMM_RSP_ERR;
	}

	return TCP_COMM_RSP_OK;
}

struct comm_command seal_cmd = {
	// SEAL vtor len crc
	// OKOK
	.opcode = CMD_SEAL,
	.nargs = 3,
	.resp_nargs = 0,
	.size = NULL,
	.handle = &handle_seal,
};

static void disable_interrupts(void)
{
	SysTick->CTRL &= ~1;

	NVIC->ICER[0] = 0xFFFFFFFF;
	NVIC->ICPR[0] = 0xFFFFFFFF;
}

static void reset_peripherals(void)
{
    reset_block(~(
            RESETS_RESET_IO_QSPI_BITS |
            RESETS_RESET_PADS_QSPI_BITS |
            RESETS_RESET_SYSCFG_BITS |
            RESETS_RESET_PLL_SYS_BITS
    ));
}

static void jump_to_vtor(uint32_t vtor)
{
	// Derived from the Leaf Labs Cortex-M3 bootloader.
	// Copyright (c) 2010 LeafLabs LLC.
	// Modified 2021 Brian Starkey <stark3y@gmail.com>
	// Originally under The MIT License
	uint32_t reset_vector = *(volatile uint32_t *)(vtor + 0x04);

	SCB->VTOR = (volatile uint32_t)(vtor);

	asm volatile("msr msp, %0"::"g"
			(*(volatile uint32_t *)vtor));
	asm volatile("bx %0"::"r" (reset_vector));
}


static uint32_t handle_go(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	struct event ev = {
		.type = EVENT_TYPE_GO,
		.go = {
			.vtor = args_in[0],
		},
	};

	if (!queue_try_add(&event_queue, &ev)) {
		return TCP_COMM_RSP_ERR;
	}

	return TCP_COMM_RSP_OK;
}

struct comm_command go_cmd = {
	// GOGO vtor
	// NO RESPONSE
	.opcode = CMD_GO,
	.nargs = 1,
	.resp_nargs = 0,
	.size = NULL,
	.handle = &handle_go,
};

static uint32_t handle_info(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	resp_args_out[0] = WRITE_ADDR_MIN;
	resp_args_out[1] = (XIP_BASE + PICO_FLASH_SIZE_BYTES) - WRITE_ADDR_MIN;
	resp_args_out[2] = FLASH_SECTOR_SIZE;
	resp_args_out[3] = FLASH_PAGE_SIZE;
	resp_args_out[4] = TCP_COMM_MAX_DATA_LEN;

	return TCP_COMM_RSP_OK;
}

const struct comm_command info_cmd = {
	// INFO
	// OKOK flash_start flash_size erase_size write_size max_data_len
	.opcode = CMD_INFO,
	.nargs = 0,
	.resp_nargs = 5,
	.size = NULL,
	.handle = &handle_info,
};

static uint32_t size_reboot(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	*data_len_out = 0;
	*resp_data_len_out = 0;

	return TCP_COMM_RSP_OK;
}

static uint32_t handle_reboot(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	struct event ev = {
		.type = EVENT_TYPE_REBOOT,
		.reboot = {
			.to_bootloader = !!args_in[0],
		},
	};

	if (!queue_try_add(&event_queue, &ev)) {
		return TCP_COMM_RSP_ERR;
	}

	return TCP_COMM_RSP_OK;
}

struct comm_command reboot_cmd = {
	// BOOT to_bootloader
	// NO RESPONSE
	.opcode = CMD_REBOOT,
	.nargs = 1,
	.resp_nargs = 0,
	.size = &size_reboot,
	.handle = &handle_reboot,
};

static bool should_stay_in_bootloader()
{
	bool wd_says_so = (watchdog_hw->scratch[5] == PICOWOTA_BOOTLOADER_ENTRY_MAGIC) &&
		(watchdog_hw->scratch[6] == ~PICOWOTA_BOOTLOADER_ENTRY_MAGIC);

	return !gpio_get(BOOTLOADER_ENTRY_PIN) || wd_says_so;
}

static void network_deinit()
{
#if PICOWOTA_WIFI_AP == 1
	dhcp_server_deinit(&dhcp_server);
#endif
	cyw43_arch_deinit();
}

#if PICOWOTA_ENTRY_AUTORE == 1
void core1_entry()
{
    // timout 5 minutes
    sleep_ms(1000 * 60 * 5);
    //auto reset(to bootloader)
    picowota_reboot(true);
}
#endif

int main()
{
	err_t err;

	gpio_init(BOOTLOADER_ENTRY_PIN);
	gpio_pull_up(BOOTLOADER_ENTRY_PIN);
	gpio_set_dir(BOOTLOADER_ENTRY_PIN, 0);

	sleep_ms(10);

	if (!should_stay_in_bootloader() && image_header_ok(&app_image_header)) {
		uint32_t vtor = *(uint32_t *)IMAGE_HEADER_ADDR;
		disable_interrupts();
		reset_peripherals();
		jump_to_vtor(vtor);
		return 0;
	}

	DBG_PRINTF_INIT();

	queue_init(&event_queue, sizeof(struct event), EVENT_QUEUE_LENGTH);

	if (cyw43_arch_init()) {
		DBG_PRINTF("failed to initialise\n");
		return 1;
	}

#if PICOWOTA_WIFI_AP == 1
	cyw43_arch_enable_ap_mode(wifi_ssid, wifi_pass, CYW43_AUTH_WPA2_AES_PSK);
	DBG_PRINTF("Enabled the WiFi AP.\n");

	ip4_addr_t gw, mask;
	IP4_ADDR(&gw, 192, 168, 4, 1);
	IP4_ADDR(&mask, 255, 255, 255, 0);

	dhcp_server_t dhcp_server;
	dhcp_server_init(&dhcp_server, &gw, &mask);
	DBG_PRINTF("Started the DHCP server.\n");
#else
	cyw43_arch_enable_sta_mode();

	DBG_PRINTF("Connecting to WiFi...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(wifi_ssid, wifi_pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
		DBG_PRINTF("failed to connect.\n");
		return 1;
	} else {
		DBG_PRINTF("Connected.\n");
	}
#endif

#if PICOWOTA_ENTRY_AUTORE == 1
    multicore_launch_core1(core1_entry);
#endif

    critical_section_init(&critical_section);

	const struct comm_command *cmds[] = {
		&sync_cmd,
		&read_cmd,
		&csum_cmd,
		&crc_cmd,
		&erase_cmd,
		&write_cmd,
		&seal_cmd,
		&go_cmd,
		&info_cmd,
		&reboot_cmd,
	};

	struct tcp_comm_ctx *tcp = tcp_comm_new(cmds, sizeof(cmds) / sizeof(cmds[0]), CMD_SYNC);

	struct event ev = {
		.type = EVENT_TYPE_SERVER_DONE,
	};

	queue_add_blocking(&event_queue, &ev);

	for ( ; ; ) {
		while (queue_try_remove(&event_queue, &ev)) {
			switch (ev.type) {
			case EVENT_TYPE_SERVER_DONE:
				err = tcp_comm_listen(tcp, TCP_PORT);
				if (err != ERR_OK) {
					DBG_PRINTF("Failed to start server: %d\n", err);
				}
				break;
			case EVENT_TYPE_REBOOT:
				tcp_comm_server_close(tcp);
				network_deinit();
				picowota_reboot(ev.reboot.to_bootloader);
				/* Should never get here */
				break;
			case EVENT_TYPE_GO:
				tcp_comm_server_close(tcp);
				network_deinit();
				disable_interrupts();
				reset_peripherals();
				jump_to_vtor(ev.go.vtor);
				/* Should never get here */
				break;
			};
		}

		cyw43_arch_poll();
		sleep_ms(5);
	}

	network_deinit();
	return 0;
}
