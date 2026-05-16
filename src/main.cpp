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


/*
PARTEA DE LCD I2C

*/

#define LCD_ADDR 0x27 // Adresa standard I2C LCD

void I2C_init() {
    TWSR = 0x00;
    TWBR = 72; // SCL = 100kHz la F_CPU=16MHz
    TWCR = (1 << TWEN);
}

void I2C_start() {
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void I2C_stop() {
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void I2C_write(uint8_t data) {
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void I2C_lcd_write(uint8_t data) {
    I2C_start();
    I2C_write(LCD_ADDR << 1);
    I2C_write(data);
    I2C_stop();
}

void LCD_command(uint8_t cmd) {
    uint8_t data_u = (cmd & 0xF0);
    uint8_t data_l = ((cmd << 4) & 0xF0);
    I2C_lcd_write(data_u | 0x0C); // EN=1, RS=0, BL=1
    I2C_lcd_write(data_u | 0x08); // EN=0, RS=0, BL=1
    I2C_lcd_write(data_l | 0x0C);
    I2C_lcd_write(data_l | 0x08);
}

void LCD_data(uint8_t data) {
    uint8_t data_u = (data & 0xF0);
    uint8_t data_l = ((data << 4) & 0xF0);
    I2C_lcd_write(data_u | 0x0D); // EN=1, RS=1, BL=1
    I2C_lcd_write(data_u | 0x09); // EN=0, RS=1, BL=1
    I2C_lcd_write(data_l | 0x0D);
    I2C_lcd_write(data_l | 0x09);
}

void LCD_init() {
    _delay_ms(50);
    LCD_command(0x33);
    LCD_command(0x32);
    LCD_command(0x28); // 4-bit mode, 2 lines, 5x8 font
    LCD_command(0x0C); // Display ON, Cursor OFF
    LCD_command(0x01); // Clear display
    _delay_ms(2);
}

void LCD_printLine(const char *msg) {
    LCD_command(0x01); // Clear
    _delay_ms(2);
    while (*msg) {
        LCD_data(*msg++);
    }
}


// Partea principala, unde fac logica propriu zisa: 

void BUTTON_interrupt_init() {
    BUTTON_DDR &= ~(1 << BUTTON_PIN);
    BUTTON_PORT |= (1 << BUTTON_PIN); // Pull-up intern
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT20);
}

void task_read_sensors() {
    moisture_raw = ADC_read(MOISTURE_CHANNEL);
    light_raw = ADC_read(LIGHT_CHANNEL);
    moisture_percent = moisture_to_percent(moisture_raw);
    DHT11_read(&temperature);
}

void task_control() {
    uint32_t now = uptime_ms();

    // ---------- FAN-ul
    if (currentMode != SYSTEM_OFF) {
        if (!fan_active && temperature >= FAN_ON_TEMP) {
            fan_on();
        }
        if (fan_active && temperature <= FAN_OFF_TEMP) {
            fan_off();
        }
    } else {
        if (fan_active) {
            fan_off();
        }
    }

    // ---------- PUMP TIMER
    if (pump_active) {
        uint32_t duration = (currentMode == MODE_ECO) ? PUMP_DURATION_ECO : PUMP_DURATION_CLASSIC;
        if (now - pump_start_time >= duration) {
            pump_off();
        }
    }

    // ---------- PUMP CONDITIONS
    if (!pump_active && currentMode != SYSTEM_OFF && (now - last_pump_time > PUMP_COOLDOWN)) {
        uint8_t need_water = 0;

        if (currentMode == MODE_CLASSIC && moisture_percent < CLASSIC_DRY_PERCENT) {
            need_water = 1;
        } else if (currentMode == MODE_ECO) {
            if (moisture_percent < ECO_CRITICAL_PERCENT) {
                need_water = 1;
            } else if (moisture_percent < ECO_DRY_PERCENT && light_raw < LIGHT_EVENING_RAW) {
                need_water = 1;
            }
        }

        if (need_water) {
            if (water_available()) {
                pump_on();
            } else {
                LCD_printLine("LOW WATER");
                BT_log_event("ERROR", "LOW_WATER");
                buzzer_beep(2);
            }
        }
    }
}

void task_display() {
    static uint8_t page = 0;
    char buffer[17];

    if (currentMode == SYSTEM_OFF) {
        LCD_printLine("SYSTEM OFF");
        return;
    }

    if (pump_active && fan_active) {
        LCD_printLine("IRIGARE+VENT");
        return;
    }
    if (pump_active) {
        LCD_printLine("PLANTA SE UDA");
        return;
    }
    if (fan_active) {
        LCD_printLine("VENTILATIE");
        return;
    }

    if (page == 0) {
        sprintf(buffer, "Umiditate: %d%%", moisture_percent);
    } else if (page == 1) {
        sprintf(buffer, "Temp: %dC", temperature);
    } else {
        sprintf(buffer, "Lumina: %d", light_raw);
    }

    LCD_printLine(buffer);
    page = (page >= 2) ? 0 : page + 1;
}

void handle_button() {
    static uint8_t last_state = 1;
    static uint32_t last_debounce = 0;
    static uint32_t press_start = 0;
    uint32_t now = uptime_ms();

    if (!button_interrupt_flag) return;
    button_interrupt_flag = 0;

    if (now - last_debounce < DEBOUNCE_MS) {
        return;
    }
    last_debounce = now;

    uint8_t current_state = (BUTTON_PINREG & (1 << BUTTON_PIN)) ? 1 : 0;

    if (last_state == 1 && current_state == 0) {
        press_start = now; // Pressed
    }

    if (last_state == 0 && current_state == 1) { // Released
        uint32_t duration = now - press_start;

        if (duration >= LONG_PRESS_MS) {
            currentMode = MODE_ECO;
            GREEN_PORT |= (1 << GREEN_PIN); // LED Verde ON
            LCD_printLine("MODE ECO");
            BT_log_event("MODE", "ECO");
            buzzer_beep(1);
        } else {
            if (currentMode == SYSTEM_OFF) {
                currentMode = MODE_CLASSIC;
                GREEN_PORT |= (1 << GREEN_PIN); // LED Verde ON
                LCD_printLine("MODE CLASSIC");
                BT_log_event("MODE", "CLASSIC");
                buzzer_beep(1);
            } else {
                currentMode = SYSTEM_OFF;
                GREEN_PORT &= ~(1 << GREEN_PIN); // LED Verde OFF
                pump_off();
                fan_off();
                LCD_printLine("SYSTEM OFF");
                BT_log_event("MODE", "OFF");
                buzzer_beep(2);
            }
        }
    }
    last_state = current_state;
}

// pentru a nu aglomera Bluetooth-ul cu prea multe mesaje,
// dar totusi sa avem o idee despre ce se intampla in sistem, am creat acest task care trimite periodic starea senzorilor si a sistemului.
void task_logger() {
    char buffer[60];
    sprintf(buffer, "%d,%d,%d,%d,%d\r\n", 
            moisture_percent, temperature, light_raw, pump_active, fan_active);
    UART_sendString(buffer);
}

void task_bluetooth_commands() {
    char cmd = UART_receiveChar();
    
    // Din aplicatia bluetooth (asta urmeaza sa fac in andorid
    // studio -> kotlin)
    // primesc comenzi pentru a schimba modul de functionare al sistemului.
    // Comanda 'S' = toggle SYSTEM_OFF / MODE_CLASSIC
    // Comanda 'E' = MODE_ECO
    if (cmd != '\0') {
        if (cmd == 'S') { 
            if (currentMode == SYSTEM_OFF) {
                currentMode = MODE_CLASSIC;
                GREEN_PORT |= (1 << GREEN_PIN);
                LCD_printLine("MODE CLASSIC");
                buzzer_beep(1);
            } else {
                currentMode = SYSTEM_OFF;
                GREEN_PORT &= ~(1 << GREEN_PIN);
                pump_off();
                fan_off();
                LCD_printLine("SYSTEM OFF");
                buzzer_beep(2);
            }
        } 
        else if (cmd == 'E') { 
            if (currentMode != SYSTEM_OFF) {
                currentMode = MODE_ECO;
                GREEN_PORT |= (1 << GREEN_PIN);
                LCD_printLine("MODE ECO");
                buzzer_beep(1);
            }
        }
    }
}




// intializez porturile hardware, senzorii, led-urile, pompa, fanul
// si intreruperile necesare pentru buton si timer.
void hardware_init() {
    ADC_init();
    UART_init(103); // 9600 baud
    I2C_init();
    LCD_init();
    TIMER0_init();
    BUTTON_interrupt_init();

    // Outputuri:
    FAN_DDR |= (1 << FAN_PIN);
    PUMP_DDR |= (1 << PUMP_PIN);
    BUZZER_DDR |= (1 << BUZZER_PIN);
    GREEN_DDR |= (1 << GREEN_PIN);
    YELLOW_DDR |= (1 << YELLOW_PIN);
    RED_DDR |= (1 << RED_PIN);

    // Initializarea Starii (cu toate Oprite)
    FAN_PORT &= ~(1 << FAN_PIN);
    PUMP_PORT &= ~(1 << PUMP_PIN);
    BUZZER_PORT &= ~(1 << BUZZER_PIN);
    GREEN_PORT &= ~(1 << GREEN_PIN);
    YELLOW_PORT &= ~(1 << YELLOW_PIN);
    RED_PORT &= ~(1 << RED_PIN);

    // Water sensor input cu pull-up.
    WATER_DDR &= ~(1 << WATER_PIN);
    WATER_PORT |= (1 << WATER_PIN); 

    sei(); // Activarea pt intreruperi globale
}

int main() {
    hardware_init();

    LCD_printLine("SMART PLANT");
    BT_log_event("SYSTEM", "BOOT");
    buzzer_beep(2);

    uint32_t last_sensor = 0;
    uint32_t last_display = 0;
    uint32_t last_control = 0;
    uint32_t last_log = 0;

    while (1) {
        uint32_t now = uptime_ms();

        handle_button();

        /* LEGARE CU APLICATIA ANDROID*/
        task_bluetooth_commands();

        if (now - last_sensor >= SENSOR_PERIOD_MS) {
            last_sensor = now;
            task_read_sensors();
        }

        if (now - last_control >= CONTROL_PERIOD_MS) {
            last_control = now;
            task_control();
        }

        if (now - last_display >= DISPLAY_PERIOD_MS) {
            last_display = now;
            task_display();
        }

        if (now - last_log >= LOG_PERIOD_MS) {
            last_log = now;
            task_logger();
        }
    }
}

