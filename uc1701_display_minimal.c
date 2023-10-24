// 
//            -----
//        5V |10  9 | GND
//        -- | 8  7 | --
//    (DIN)  | 6  5#| (RESET)
//  (LCD_A0) | 4  3 | (LCD_CS)
// (BTN_ENC) | 2  1 | --
//            ------
//             EXP1
//            -----
//        5V |10  9 | GND
//   (RESET) | 8  7 | --
//   (MOSI)  | 6  5#| (EN2)
//        -- | 4  3 | (EN1)
//  (LCD_SCK)| 2  1 | --
//            ------
//             EXP2
//
// For Pico W
// EXP1_3 (CS) <-> PIN22 (SPI0 CS)
// EXP1_4 (A0) <-> PIN26 (GP20)
// EXP2_6 (MOSI) <-> PIN19 (SPI0 TX)
// EXP2_2 (SCK) <-> PIN24 (SPI0 SCK)
// EXP1_5 (RST) <-> PIN27 (GP21)
// 
// For Fly ERCF
// EXP1_3 (CS) <-> P13
// EXP1_4 (A0) <-> P24
// EXP2_6 (MOSI) <-> P11
// EXP2_2 (SCK) <-> P10
// EXP1_5 (RST) <-> P25

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "u8g2.h"

#include "display_minimal.h"

#include "hardware/spi.h"

#ifdef RASPBERRYPI_PICO
#include "raspberrypi_pico_config.h"
#endif

#ifdef RASPBERRYPI_PICO_W
#include "raspberrypi_pico_w_config.h"
#endif

// Local variables
extern u8g2_t display_handler;

uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch(msg)
    {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:	// called once during init phase of u8g2/u8x8
            // We don't initialize here
            break;							// can be used to setup pins
        case U8X8_MSG_DELAY_NANO:			// delay arg_int * 1 nano second
            break;    
        case U8X8_MSG_DELAY_100NANO:		// delay arg_int * 100 nano seconds
            __asm volatile ("NOP\n");
            break;
        case U8X8_MSG_DELAY_10MICRO:		// delay arg_int * 10 micro seconds
            busy_wait_us(arg_int * 10ULL);
            break;
        case U8X8_MSG_DELAY_MILLI:			// delay arg_int * 1 milli second
        {
            sleep_ms(arg_int);
            break;
        }
        case U8X8_MSG_DELAY_I2C:				// arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            break;							// arg_int=1: delay by 5us, arg_int = 4: delay by 1.25us
        case U8X8_MSG_GPIO_D0:				// D0 or SPI clock pin: Output level in arg_int
            //case U8X8_MSG_GPIO_SPI_CLOCK:
            break;
        case U8X8_MSG_GPIO_D1:				// D1 or SPI data pin: Output level in arg_int
            //case U8X8_MSG_GPIO_SPI_DATA:
            break;
        case U8X8_MSG_GPIO_D2:				// D2 pin: Output level in arg_int
        break;
        case U8X8_MSG_GPIO_D3:				// D3 pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_D4:				// D4 pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_D5:				// D5 pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_D6:				// D6 pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_D7:				// D7 pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_E:				// E/WR pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_CS:				// CS (chip select) pin: Output level in arg_int
            gpio_put(DISPLAY0_CS_PIN, arg_int);
            break;
        case U8X8_MSG_GPIO_DC:				// DC (data/cmd, A0, register select) pin: Output level in arg_int
            gpio_put(DISPLAY0_A0_PIN, arg_int);
            break;
        case U8X8_MSG_GPIO_RESET:			// Reset pin: Output level in arg_int
            gpio_put(DISPLAY0_RESET_PIN, arg_int);
            break;
        case U8X8_MSG_GPIO_CS1:				// CS1 (chip select) pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_CS2:				// CS2 (chip select) pin: Output level in arg_int
            break;
        case U8X8_MSG_GPIO_I2C_CLOCK:		// arg_int=0: Output low at I2C clock pin
            break;							// arg_int=1: Input dir with pullup high for I2C clock pin
        case U8X8_MSG_GPIO_I2C_DATA:			// arg_int=0: Output low at I2C data pin
            break;							// arg_int=1: Input dir with pullup high for I2C data pin
        case U8X8_MSG_GPIO_MENU_SELECT:
            u8x8_SetGPIOResult(u8x8, /* get menu select pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_NEXT:
            u8x8_SetGPIOResult(u8x8, /* get menu next pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_PREV:
            u8x8_SetGPIOResult(u8x8, /* get menu prev pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_HOME:
            u8x8_SetGPIOResult(u8x8, /* get menu home pin state */ 0);
            break;
        default:
            u8x8_SetGPIOResult(u8x8, 1);			// default return value
        break;
    }
    return 1;
}

uint8_t u8x8_byte_pico_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) 
{
    //uint8_t *data;
    //uint8_t internal_spi_mode;

    switch (msg)
    {
        case U8X8_MSG_BYTE_SEND:
            // taskENTER_CRITICAL();
            spi_write_blocking(DISPlAY0_SPI, (uint8_t *) arg_ptr, arg_int);
            // taskEXIT_CRITICAL();
            break;
        case U8X8_MSG_BYTE_INIT:
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
            break;
        case U8X8_MSG_BYTE_SET_DC:
            u8x8_gpio_SetDC(u8x8, arg_int);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);  
            u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns, NULL);
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns, NULL);
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
            break;
        default:
            return 0;
    }
    return 1;
}


void display_minimal_init() {
    // Configure 12864
    printf("Initializing Display -- ");
    // Initialize SPI engine
    spi_init(DISPlAY0_SPI, 4000 * 1000);

    // Configure port for SPI
    // gpio_set_function(DISPLAY0_RX_PIN, GPIO_FUNC_SPI);  // Rx
    gpio_set_function(DISPLAY0_SCK_PIN, GPIO_FUNC_SPI);  // CSn
    gpio_set_function(DISPLAY0_SCK_PIN, GPIO_FUNC_SPI);  // SCK
    gpio_set_function(DISPLAY0_TX_PIN, GPIO_FUNC_SPI);  // Tx

    // Configure property for CS
    gpio_init(DISPLAY0_CS_PIN);
    gpio_set_dir(DISPLAY0_CS_PIN, GPIO_OUT);
    gpio_put(DISPLAY0_CS_PIN, 1);

    // Configure property for A0 (D/C)
    gpio_init(DISPLAY0_A0_PIN);
    gpio_set_dir(DISPLAY0_A0_PIN, GPIO_OUT);
    gpio_put(DISPLAY0_A0_PIN, 0);

    // Configure property for RESET
    gpio_init(DISPLAY0_RESET_PIN);
    gpio_set_dir(DISPLAY0_RESET_PIN, GPIO_OUT);
    gpio_put(DISPLAY0_RESET_PIN, 0);

    u8g2_Setup_uc1701_mini12864_f(
        &display_handler, 
        U8G2_R0, 
        u8x8_byte_pico_hw_spi, 
        u8x8_gpio_and_delay
    );

    // Display something
    // Initialize Screen
    u8g2_InitDisplay(&display_handler);
    u8g2_SetPowerSave(&display_handler, 0);
    u8g2_SetContrast(&display_handler, 255);

    // Clear 
    u8g2_ClearBuffer(&display_handler);
    u8g2_ClearDisplay(&display_handler);

    printf("done\n");
}
