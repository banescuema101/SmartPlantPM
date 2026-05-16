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

/* ISR / TIME */
// pentru a evita folosirea lui delay() care blocheaza tot sistemul,
// folosesc un timer pentru a tine evidenta timpului si a genera intreruperi periodice.
ISR(TIMER0_COMPA_vect) {
    g_millis++;
}

ISR(PCINT2_vect) {
    button_interrupt_flag = 1;
}

uint32_t uptime_ms() {
    uint32_t value;
    cli();
    value = g_millis;
    sei();
    return value;
}

void TIMER0_init() {
    TCCR0A |= (1 << WGM01);
    OCR0A = 249;
    TCCR0B |= (1 << CS01) | (1 << CS00);
    TIMSK0 |= (1 << OCIE0A);
}

// Pentru a face somunicarea UART (Bluetooth) */
void UART_init(unsigned int ubrr) {
    UBRR0H = (unsigned char)(ubrr >> 8);
    UBRR0L = (unsigned char)ubrr;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void UART_sendChar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void UART_sendString(const char *str) {
    while (*str) UART_sendChar(*str++);
}

void BT_log_event(const char *event, const char *value) {
    char buffer[100];
    sprintf(buffer, "%lu,%s,%s\r\n", uptime_ms(), event, value);
    UART_sendString(buffer);
}

uint8_t UART_available() {
    return (UCSR0A & (1 << RXC0));
}

char UART_receiveChar() {
    if (UART_available()) {
        return UDR0;
    }
    return '\0';
}


/*
Partea de citire a senzorilor

*/
void ADC_init() {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t ADC_read(uint8_t channel) {
    ADMUX = (1 << REFS0) | (channel & 0x0F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

uint8_t DHT11_read(uint8_t *temp) {
    uint8_t data[5] = {0};
    DHT_DDR |= (1 << DHT_PIN);
    DHT_PORT &= ~(1 << DHT_PIN);
    _delay_ms(20);
    DHT_PORT |= (1 << DHT_PIN);
    _delay_us(40);
    DHT_DDR &= ~(1 << DHT_PIN);

    uint16_t timeout = 0;
    while (DHT_PINREG & (1 << DHT_PIN)) {
        if (++timeout > 100) {
            return 0;
        } else {
            _delay_us(1);
        }
    }
    timeout = 0;
    while (!(DHT_PINREG & (1 << DHT_PIN))) {
        if (++timeout > 100) {
            return 0;
        } else {
            _delay_us(1);
        }
    }
    timeout = 0;
    while (DHT_PINREG & (1 << DHT_PIN)) {
        if (++timeout > 100) {
            return 0;
        } else {
            _delay_us(1);
        }
    }

    for (uint8_t i = 0; i < 40; i++) {
        while (!(DHT_PINREG & (1 << DHT_PIN)));
        _delay_us(35);
        if (DHT_PINREG & (1 << DHT_PIN)) {
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
        while (DHT_PINREG & (1 << DHT_PIN));
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) return 0;
    *temp = data[2];
    return 1;
}

/* outputs si functii helper.*/
void buzzer_beep(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        BUZZER_PORT |= (1 << BUZZER_PIN);
        _delay_ms(120);
        BUZZER_PORT &= ~(1 << BUZZER_PIN);
        _delay_ms(120);
    }
}

void pump_on() {
    PUMP_PORT |= (1 << PUMP_PIN);
    YELLOW_PORT |= (1 << YELLOW_PIN);
    pump_active = 1;
    pump_start_time = uptime_ms();
    BT_log_event("PUMP", "ON");
}

void pump_off() {
    PUMP_PORT &= ~(1 << PUMP_PIN);
    YELLOW_PORT &= ~(1 << YELLOW_PIN);
    pump_active = 0;
    last_pump_time = uptime_ms();
    BT_log_event("PUMP", "OFF");
}

void fan_on() {
    FAN_PORT |= (1 << FAN_PIN);
    RED_PORT |= (1 << RED_PIN);
    fan_active = 1;
    BT_log_event("FAN", "ON");
}

void fan_off() {
    FAN_PORT &= ~(1 << FAN_PIN);
    RED_PORT &= ~(1 << RED_PIN);
    fan_active = 0;
    BT_log_event("FAN", "OFF");
}

uint8_t water_available() {
    return (WATER_PINREG & (1 << WATER_PIN));
}

uint8_t moisture_to_percent(uint16_t raw) {
    if (raw >= MOISTURE_DRY_RAW) {
        return 0;
    }
    if (raw <= MOISTURE_WET_RAW) {
        return 100;
    }
    return (MOISTURE_DRY_RAW - raw) * 100UL / (MOISTURE_DRY_RAW - MOISTURE_WET_RAW);
}


int main() {
    TIMER0_init();
    UART_init(103); 
    ADC_init();
    
    // Setez pinii de iesire ca OUTPUT
    FAN_DDR |= (1 << FAN_PIN);
    PUMP_DDR |= (1 << PUMP_PIN);
    BUZZER_DDR |= (1 << BUZZER_PIN);
    YELLOW_DDR |= (1 << YELLOW_PIN);
    RED_DDR |= (1 << RED_PIN);
    
    // Water sensor ca INPUT cu pull-up
    WATER_DDR &= ~(1 << WATER_PIN);
    WATER_PORT |= (1 << WATER_PIN); 

    sei(); // Activez intreruperile

    UART_sendString("TEST SENZORI PORNIT\r\n");
    buzzer_beep(1); // Testez buzzerul la pornire

    uint32_t ultimul_mesaj = 0;

    while(1) {
        uint32_t timp_curent = uptime_ms();

        // citesc si afisez la fiecare 2 secunde,
        // pentru a nu aglomera Bluetooth-ul cu prea multe mesaje, dar totusi sa avem o idee despre ce se intampla in sistem.
        if (timp_curent - ultimul_mesaj >= 2000) {
            ultimul_mesaj = timp_curent;
            
            // Citire senzori
            uint16_t moist_raw = ADC_read(MOISTURE_CHANNEL);
            uint8_t procent_apa = moisture_to_percent(moist_raw);
            uint8_t temp_val = 0;
            DHT11_read(&temp_val);

            // Trimit pe Serial
            char buf[60];
            sprintf(buf, "Umiditate: %d%% (%d raw) | Temp: %dC\r\n", procent_apa, moist_raw, temp_val);
            UART_sendString(buf);
            
            // Un mic test pentru pompa: daca pamantul e complet uscat (sau senzorul scos), aprind LED-ul si pompa scurt
            if (procent_apa < 10) {
                pump_on();
                _delay_ms(500); // doar pentru test si mergeee
                pump_off();
            }
        }
    }
}