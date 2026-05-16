/*
 * SMART PLANT IRRIGATION SYSTEM
 * ATmega328P / Arduino UNO R3
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 
Definitiile pinurilor:
*/
// pentru ADC:
#define MOISTURE_CHANNEL 0 // A0
#define LIGHT_CHANNEL    1 // A1

// Pentru DHT11
#define DHT_DDR    DDRC
#define DHT_PORT   PORTC
#define DHT_PINREG PINC
#define DHT_PIN    PC2  // A2

// Pentru BUTON D4
#define BUTTON_DDR      DDRD
#define BUTTON_PORT     PORTD
#define BUTTON_PINREG   PIND
#define BUTTON_PIN      PD4

// PENTRU FAN D5
#define FAN_DDR    DDRD
#define FAN_PORT   PORTD
#define FAN_PIN    PD5

// Pentru PUMP D6
#define PUMP_DDR    DDRD
#define PUMP_PORT   PORTD
#define PUMP_PIN    PD6

// Pentru BUZZER D8
#define BUZZER_DDR  DDRB
#define BUZZER_PORT PORTB
#define BUZZER_PIN  PB0

// WATER SENSOR D9
#define WATER_DDR      DDRB
#define WATER_PORT     PORTB
#define WATER_PINREG   PINB
#define WATER_PIN      PB1

// GREEN LED D10 (System State)
#define GREEN_DDR  DDRB
#define GREEN_PORT PORTB
#define GREEN_PIN  PB2

// YELLOW LED D11 (Pump Indicator)
#define YELLOW_DDR  DDRB
#define YELLOW_PORT PORTB
#define YELLOW_PIN  PB3

// RED LED D12 (Fan Indicator)
#define RED_DDR  DDRB
#define RED_PORT PORTB
#define RED_PIN  PB4

/*
Constantele
*/
#define DEBOUNCE_MS            50
#define LONG_PRESS_MS          3000
#define SENSOR_PERIOD_MS       1000
#define DISPLAY_PERIOD_MS      2000
#define CONTROL_PERIOD_MS      500
#define LOG_PERIOD_MS          1000

#define FAN_ON_TEMP            28
#define FAN_OFF_TEMP           26

#define CLASSIC_DRY_PERCENT    40
#define ECO_DRY_PERCENT        35
#define ECO_CRITICAL_PERCENT   20
#define LIGHT_EVENING_RAW      450
#define PUMP_DURATION_CLASSIC  3000
#define PUMP_DURATION_ECO      1500
#define PUMP_COOLDOWN          10000

#define MOISTURE_DRY_RAW       996
#define MOISTURE_WET_RAW       385

/*
Variabile globale pentru stocarea starii sistemului, a senzorilor si a timpilor.
*/
volatile uint32_t g_millis = 0;
volatile uint8_t button_interrupt_flag = 0;

typedef enum {
    SYSTEM_OFF,
    MODE_CLASSIC,
    MODE_ECO
} SystemMode;

SystemMode currentMode = SYSTEM_OFF;

uint8_t pump_active = 0;
uint8_t fan_active = 0;
uint32_t pump_start_time = 0;
uint32_t last_pump_time = 0;

uint16_t moisture_raw = 0;
uint16_t light_raw = 0;
uint8_t moisture_percent = 0;
uint8_t temperature = 0;

int main() {
    while(1) {
    }
}