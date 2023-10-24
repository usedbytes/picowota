#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "hardware/watchdog.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"

#include "eeprom.h"


extern bool cat24c256_eeprom_erase();
extern void cat24c256_eeprom_init();
extern bool cat24c256_write(uint16_t data_addr, uint8_t * data, size_t len);
extern bool cat24c256_read(uint16_t data_addr, uint8_t * data, size_t len);

eeprom_metadata_t metadata;

uint32_t rnd(void){
    int k, random=0;
    volatile uint32_t *rnd_reg=(uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
    
    for(k=0;k<32;k++){
    
    random = random << 1;
    random=random + (0x00000001 & (*rnd_reg));

    }
    return random;
}

bool eeprom_init(void) {
    bool is_ok = true;

    cat24c256_eeprom_init();

    // Read data revision, if match then move forward
    is_ok = eeprom_read(EEPROM_METADATA_BASE_ADDR, (uint8_t *) &metadata, sizeof(eeprom_metadata_t));
    if (!is_ok) {
        printf("Unable to read from EEPROM at address %x\n", EEPROM_METADATA_BASE_ADDR);
        return false;
    }

    if (metadata.eeprom_metadata_rev != EEPROM_METADATA_REV) {
        return false;    
    }

    return is_ok;
}

bool eeprom_read(uint16_t data_addr, uint8_t * data, size_t len) {
    bool is_ok;

    is_ok = cat24c256_read(data_addr, data, len);

    return is_ok;
}