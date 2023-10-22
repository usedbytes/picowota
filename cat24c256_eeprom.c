#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"

#ifdef RASPBERRYPI_PICO
#include "raspberrypi_pico_config.h"
#endif

#ifdef RASPBERRYPI_PICO_W
#include "raspberrypi_pico_w_config.h"
#endif
#include "eeprom.h"

// Include only for PICO board with specific flash chip
#include "pico/unique_id.h"


#define PAGE_SIZE   64  // 64 byte page size


void cat24c256_eeprom_init() {
    // Initialize I2C bus with 400k baud rate
    i2c_init(EEPROM_I2C, 400 * 1000);

    // Initialize PINs as I2C function
    gpio_set_function(EEPROM_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(EEPROM_SCL_PIN, GPIO_FUNC_I2C);

    gpio_pull_up(EEPROM_SDA_PIN);
    gpio_pull_up(EEPROM_SCL_PIN);

    // Make the I2C pins available to picotool
    // bi_decl(bi_2pins_with_func(EEPROM_SDA_PIN, EEPROM_SCL_PIN, GPIO_FUNC_I2C));
}


bool _cat24c256_write_page(uint16_t data_addr, uint8_t * data, size_t len){
    uint8_t buf[len + 2];  // Include first two bytes for address
    buf[0] = (data_addr >> 8) & 0xFF; // High byte of address
    buf[1] = data_addr & 0xFF; // Low byte of address

    // Copy data to buffer
    memcpy(&buf[2], data, len);

    // Send to the EEPROM
    int ret;
    ret = i2c_write_blocking(EEPROM_I2C, EEPROM_ADDR, buf, len + 2, false);
    return ret != PICO_ERROR_GENERIC;
}


bool cat24c256_write(uint16_t base_addr, uint8_t * data, size_t len) {
    uint16_t num_pages = len / PAGE_SIZE;
    bool is_ok;

    for (uint16_t page = 0; page <= num_pages; page += 1) {
        uint16_t offset = page * PAGE_SIZE;
        size_t write_size = PAGE_SIZE;
        if (page == num_pages) {
            write_size = len % PAGE_SIZE;
        }

        is_ok = _cat24c256_write_page(base_addr + offset, data + offset, write_size);
        busy_wait_us(5 * 1000ULL);
        if (!is_ok) {
            return false;
        }
    }

    return true;
}


bool cat24c256_read(uint16_t data_addr, uint8_t * data, size_t len) {
    uint8_t buf[2];  // Include first two bytes for address
    buf[0] = (data_addr >> 8) & 0xFF; // High byte of address
    buf[1] = data_addr & 0xFF; // Low byte of address

    i2c_write_blocking(EEPROM_I2C, EEPROM_ADDR, buf, 2, true);

    int bytes_read;
    bytes_read = i2c_read_blocking(EEPROM_I2C, EEPROM_ADDR, data, len, false);

    return bytes_read == len;
}


bool cat24c256_eeprom_erase() {
    uint8_t dummy_buffer[PAGE_SIZE];
    memset(dummy_buffer, 0xff, PAGE_SIZE);

    
    for (size_t page=0; page < 512; page++) {
        size_t page_offset = page * PAGE_SIZE;
        cat24c256_write(page_offset, dummy_buffer, PAGE_SIZE);
    }
    return true;
}
