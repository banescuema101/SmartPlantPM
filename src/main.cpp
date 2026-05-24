/*
 * Smart Plant Irrigation System
 * Arduino UNO R3 (ATmega328P)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEMO_MODE 1

#if DEMO_MODE
uint8_t demo_temperature = 24;
#endif



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

// Pentru buton D4
#define BUTTON_DDR      DDRD
#define BUTTON_PORT     PORTD
#define BUTTON_PINREG   PIND
#define BUTTON_PIN      PD4

// Pentru fan D9
#define FAN_DDR    DDRB
#define FAN_PORT   PORTB
#define FAN_PIN    PB1 

// Pentru pump D6
#define PUMP_DDR    DDRD
#define PUMP_PORT   PORTD
#define PUMP_PIN    PD6

// Pentru buzzer D8
#define BUZZER_DDR  DDRB
#define BUZZER_PORT PORTB
#define BUZZER_PIN  PB0

// Pentru water sensor D5
#define WATER_DDR      DDRD
#define WATER_PORT     PORTD
#define WATER_PINREG   PIND
#define WATER_PIN      PD5

// Pentru GREEN LED D10 (care are rolul de a indica daca sistemul este pornit sau oprit)
#define GREEN_DDR  DDRB
#define GREEN_PORT PORTB
#define GREEN_PIN  PB2

// Pentru YELLOW LED D11 (Pump Indicator)
#define YELLOW_DDR  DDRB
#define YELLOW_PORT PORTB
#define YELLOW_PIN  PB3

// Pentru RED LED D12 (Fan Indicator)
#define RED_DDR  DDRB
#define RED_PORT PORTB
#define RED_PIN  PB4

/*
Constantele din program:
*/
#define DEBOUNCE_MS            50
#define LONG_PRESS_MS          3000
#define SENSOR_PERIOD_MS       1000
#define DISPLAY_PERIOD_MS      2000
#define CONTROL_PERIOD_MS      500
#define LOG_PERIOD_MS          1000

// am decomentat, pentru ca acum am adaugat partera de PWM.


// Daca temp e mai mare ca FAN_ON_TEMP porneste ventilatorul, daca e mai mica ca FAN_OFF_TEMP opreste-l.
// #define FAN_ON_TEMP            21
// #define FAN_OFF_TEMP           18

// Am comentat define-urile de mai jos,
// intrucat initial nu introdusesem mai multe categorii de plante,
// acum ca am 3 categorii: normale, cactus si tropicale, am mutat aceste valori
// in variabile care se seteaza in functie de profilul plantei selectat.
// si anume variabilele: plant_dry_percent si plant_pump_duration

// Modul ECO reduce consumul de apa prin scaderea duratei de functionare
// a pompei si prin utilizarea unor praguri mai conservative pentru udare.


// #define CLASSIC_DRY_PERCENT    40
// #define ECO_DRY_PERCENT        35
// #define ECO_CRITICAL_PERCENT   20
#define LIGHT_EVENING_RAW      900
// #define PUMP_DURATION_CLASSIC  3000
// #define PUMP_DURATION_ECO      1500
#define PUMP_COOLDOWN          10000

#define MOISTURE_DRY_RAW       996
#define MOISTURE_WET_RAW       385

/*
Variabile globale pentru stocarea starii sistemului, a senzorilor si a timpilor.
*/
volatile uint32_t g_millis = 0;
volatile uint8_t button_interrupt_flag = 0;


uint16_t light_lux = 0;

typedef enum {
    SYSTEM_OFF,
    MODE_CLASSIC,
    MODE_ECO
} SystemMode;

SystemMode currentMode = SYSTEM_OFF;
// pentru categoriile de plantute :)
typedef enum {
    PLANT_NORMAL,
    PLANT_CACTUS,
    PLANT_TROPICAL
} PlantProfile;

PlantProfile currentPlant = PLANT_NORMAL;

uint8_t plant_dry_percent = 40;
uint16_t plant_pump_duration = 3000;


uint8_t pump_active = 0;
uint8_t fan_active = 0;

// adaug si cele 2 variabile pentru PWM:
uint8_t fan_target_speed = 0;
uint8_t fan_current_speed = 0;


uint32_t pump_start_time = 0;
uint32_t last_pump_time = 0;

uint16_t moisture_raw = 0;
uint16_t light_raw = 0;
uint8_t moisture_percent = 0;
uint8_t temperature = 0;

// ISR pe Timer0, care se declanseaza la fiecare 1ms (configurat in TIMER0_init).
// pentru a evita folosirea lui delay() care blocheaza tot sistemul,
// folosesc un timer pentru a tine evidenta timpului si a genera intreruperi periodice.
ISR(TIMER0_COMPA_vect) {
    g_millis++;
}

// ISR pentru buton, care seteaza un flag atunci cand butonul este apasat sau eliberat.
ISR(PCINT2_vect) {
    button_interrupt_flag = 1;
}

// Functia helper ca sa obtin timpul de uptime in milisecunde, folosind variabila incrementata de ISR-ul Timer0.
uint32_t uptime_ms() {
    uint32_t value;
    cli();
    value = g_millis;
    sei();
    return value;
}

void TIMER0_init() {
    // aici configurez Timer0 pentru a genera o intrerupere la fiecare 1ms, folosind CTC mode si un prescaler de 64.
    TCCR0A |= (1 << WGM01);
    OCR0A = 249;
    TCCR0B |= (1 << CS01) | (1 << CS00);
    TIMSK0 |= (1 << OCIE0A);
}


// Functiile pentru pwm, pentru ca la ventilator, eu nu imi doresc
// doar ON/OFF, ci sa pot regla viteza in functie de temperatura,
// astfel incat daca e putin peste pragul de pornire, sa mearga
// mai incet.

void pwm_init(void)
{
    // D9 = PB1 = OC1A pe Arduino UNO
    // acesta este pinul controlat de Timer1 prin OCR1A
    FAN_DDR |= (1 << FAN_PIN);

    // resetez registrele Timer1 inainte de configurare
    TCCR1A = 0;
    TCCR1B = 0;

    // Fast PWM 8-bit
    TCCR1A |= (1 << WGM10);
    TCCR1B |= (1 << WGM12);

    // PWM non-inverting pe OC1A
    TCCR1A |= (1 << COM1A1);

    // prescaler 64
    TCCR1B |= (1 << CS11) | (1 << CS10);

    // duty cycle initial 0%
    OCR1A = 0;
}

void fan_set_speed(uint8_t percent)
{
    if (percent > 100)
        percent = 100;

    // Convertesc procentul in valoare PWM (0-255)
    OCR1A = (percent * 255UL) / 100;
    fan_active = (percent > 0);

    if (fan_active) {
        RED_PORT &= ~(1 << RED_PIN);   // LED-ul rosu ON
    } else {
        RED_PORT |= (1 << RED_PIN);    // LED-ul rosu OFF
    }
}





// Pentru a face comunicarea UART (Bluetooth) */
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
    // char buffer[100];
    // sprintf(buffer, "%lu,%s,%s\r\n", uptime_ms(), event, value);
    // UART_sendString(buffer);
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

// Functia DHT11_read citeste temperatura de la senzorul DHT11,
// si salveaza in variabila temp: 1 daca citirea a fost reusita,
// sau 0 in caz de eroare sau timeout.
uint8_t DHT11_read(uint8_t *temp) {

    // DHT11 trimite 40 de biti: 8 pentru umiditate intreaga,
    // 8 pentru umiditate zecimala, 8 pentru temperatura intreaga,
    // 8 pentru temperatura zecimala si 8 pentru checksum.
    // si de aceea am initializat un array de 5 bytes pentru
    // a stoca datele primite.
    uint8_t data[5] = {0};
    // setez pinul DHT ca output, ca sa trimit semnalul de start.
    DHT_DDR |= (1 << DHT_PIN);

    // trag pinul pe LOW timp de 20ms pentru a semnala startul
    // comunicatiei catre DHT11.
    DHT_PORT &= ~(1 << DHT_PIN);
    _delay_ms(20);
    // acum il ridic pe high.
    DHT_PORT |= (1 << DHT_PIN);
    // astept 40 microsecunde, conform protocolului DHT11
    // inainte de a schimba pinul pe input pentru a citi raspunsul senzorului.
    _delay_us(40);
    DHT_DDR &= ~(1 << DHT_PIN);

    uint16_t timeout = 0;
    // astept ca DHT11 sa raspunda cu un semnal LOW, apoi HIGH, apoi LOW din nou,
    // fiecare cu un timeout pentru a evita blocarea in caz de eroare.
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

    // si aici efectiv incep sa citesc cei 40 de biti trimisi de DHT11.
    for (uint8_t i = 0; i < 40; i++) {
        while (!(DHT_PINREG & (1 << DHT_PIN)));
        _delay_us(35);
        if (DHT_PINREG & (1 << DHT_PIN)) {
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
        while (DHT_PINREG & (1 << DHT_PIN));
    }
    // DHT11 trimite checksum pentru verificarea integritatii datelor.
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

// functie pentru a porni pompa, care seteaza pinul pompei pe HIGH
// ,aprinde led-ul galben, seteaza variabila pump_active, salveaza timpul de start 
// pentru a putea opri pompa dupa durata setata,
// si trimite un mesaj de log prin Bluetooth.
void pump_on() {
    PUMP_PORT |= (1 << PUMP_PIN);
    YELLOW_PORT &= ~(1 << YELLOW_PIN);
    pump_active = 1;
    pump_start_time = uptime_ms();
    BT_log_event("PUMP", "ON");
}

void pump_off() {
    PUMP_PORT &= ~(1 << PUMP_PIN);
    YELLOW_PORT |= (1 << YELLOW_PIN);
    pump_active = 0;
    last_pump_time = uptime_ms();
    BT_log_event("PUMP", "OFF");
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

#define LCD_ADDR 0x27 // Adresa I2C LCD

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

// Am preluat secventele de functii de mai jos de pe urmatorul link de github:
// https://github.com/eshansurendra/liquid_crystal_i2c_avr/blob/main/src/main.c

// trimite o comanda catre LCD.
// Comenzile controleaza LCD-ul.
// clear, cursor, mod afisare etc.
void LCD_command(uint8_t cmd) {
    // impart octetul in 2 jumatati, pentru modul 4-bit al LCD-ului.
    uint8_t data_u = (cmd & 0xF0);
    uint8_t data_l = ((cmd << 4) & 0xF0);
    // EN = 1 -> activare transfer
    I2C_lcd_write(data_u | 0x0C); // EN=1, RS=0, BL=1
    // EN = 0 -> concfirmare transfer.
    I2C_lcd_write(data_u | 0x08); // EN=0, RS=0, BL=1
    I2C_lcd_write(data_l | 0x0C);
    I2C_lcd_write(data_l | 0x08);
}

// Trimit un caracter catre LCD pentru afisare.
void LCD_data(uint8_t data) {
    uint8_t data_u = (data & 0xF0);
    uint8_t data_l = ((data << 4) & 0xF0);
    I2C_lcd_write(data_u | 0x0D); // EN=1, RS=1, BL=1
    I2C_lcd_write(data_u | 0x09); // EN=0, RS=1, BL=1
    I2C_lcd_write(data_l | 0x0D);
    I2C_lcd_write(data_l | 0x09);
}

void LCD_init() {
    // astept ca display-ul sa porneasca.
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


// initializez intreruperea pentru buton, setand pinul
// ca input cu pull-up, si configurand intreruperea pe schimbare 
// de stare (PCINT20 pentru PD4).
void BUTTON_interrupt_init() {
    BUTTON_DDR &= ~(1 << BUTTON_PIN);
    BUTTON_PORT |= (1 << BUTTON_PIN); // Pull-up intern
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT20);
}

// functia care citeste senzorii de umiditate, lumina si temperatura.
void task_read_sensors() {
    moisture_raw = ADC_read(MOISTURE_CHANNEL);
    light_raw = ADC_read(LIGHT_CHANNEL);
    light_lux = (1023 - light_raw) * 1000UL / 1023;
    moisture_percent = moisture_to_percent(moisture_raw);

#if DEMO_MODE
    temperature = demo_temperature;
#else
    DHT11_read(&temperature);
#endif
}

// pentru a putea avea profile diferite, in functie de tipul plantei.
// de exemplu pentru un cactus, pragul de "uscat" e de 20% umiditate, iar
// pompa va porni doar 1 secunda atunci cand umiditatea scade sub 20%, in
// timp ce pentru o planta tropicala, pragul de uscat e de 65%, iar
// pompa porneste timp de 4 secunde, pentru a oferi mai multa apa.
void set_plant_profile(char cmd) {
    if (cmd == 'N') {
        currentPlant = PLANT_NORMAL;
        plant_dry_percent = 40;
        plant_pump_duration = 3000;


        demo_temperature = 24;
        LCD_printLine("PLANTA NORMALA");
    } 
    else if (cmd == 'C') {
        currentPlant = PLANT_CACTUS;
        plant_dry_percent = 20;
        plant_pump_duration = 1000;
        demo_temperature = 30;
        LCD_printLine("CACTUS");
    } 
    else if (cmd == 'F') {
        currentPlant = PLANT_TROPICAL;
        plant_dry_percent = 65;
        plant_pump_duration = 4000;
        demo_temperature = 36;
        LCD_printLine("TROPICALA");
    }

    buzzer_beep(1);
}


void task_control() {
    uint32_t now = uptime_ms();

    // FAN-ul
    // initial am avut doar ON/OFF pentru ventilator, dar acum ca am adaugat si variabila pentru viteza,
    // am modificat logica astfel incat sa regleze viteza 
    // in functie de temperatura, nu doar sa porneasca sau sa opreasca.
    // if (currentMode != SYSTEM_OFF) {
    //     if (!fan_active && temperature >= FAN_ON_TEMP) {
    //         fan_on();
    //     }
    //     if (fan_active && temperature <= FAN_OFF_TEMP) {
    //         fan_off();
    //     }
    // } else {
    //     if (fan_active) {
    //         fan_off();
    //     }
    // }

    // FAN ajutandu ma de PWM
    if (currentMode == SYSTEM_OFF) {
        fan_target_speed = 0;
    } else {
        if (temperature < 25) {
            fan_target_speed = 0;
        } else if (temperature < 30) {
            fan_target_speed = 30;
        } else if (temperature < 35) {
            fan_target_speed = 60;
        } else {
            fan_target_speed = 100;
        }
    }

    // AM COMENTAT partrea de sus DOAR pentru test:
    // pentru test las cele 5 linii de jos, si comentezi partea de deasupra.
    // if (currentMode != SYSTEM_OFF) {
    //     fan_target_speed = 70;
    // }
    // else {
    //     fan_target_speed = 0;
    // }







    // Pump Timer
    if (pump_active) {
        uint32_t duration = plant_pump_duration;

        if (currentMode == MODE_ECO) {
            duration = plant_pump_duration / 2;
        }
        if (now - pump_start_time >= duration) {
            pump_off();
        }
    }

    // Pump conditions:
    if (!pump_active && currentMode != SYSTEM_OFF && (now - last_pump_time > PUMP_COOLDOWN)) {
        uint8_t need_water = 0;

        if (currentMode == MODE_CLASSIC && moisture_percent < plant_dry_percent) {
            need_water = 1;
            // in modul eco, sunt mai conservativ cu udarea, 
            // pentru a economisi apa, astfel incat
            // pompa porneste doar daca umditatea scade sub pragul de uscat minus 10%, 
            // sau daca e seara (lumina scade sub un prag) si umiditatea e sub pragul de uscat.
        } else if (currentMode == MODE_ECO) {
            if (moisture_percent < plant_dry_percent - 10) {
                need_water = 1;
            } else if (moisture_percent < plant_dry_percent && light_lux < 1000 - LIGHT_EVENING_RAW) {
                need_water = 1;
            }
        }
        // in caz in care teoretic, conform conditiilor, ar trebui
        // sa porneasca pompa, dar senzorul de apa
        // indica ca nu mai este apa disponibila, atunci afisez
        // un mesaj de eroare pe LCD, trimit un log prin Bluetooth si emit
        // si un semnal sonor cu buzzer-ul pentru a atrage atentia userului.
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

// adaug un task separat pentru cresterea / scaderea treptata
// a vitezei ventilatorului, astfel incat sa nu fie schimbari
// bruste care ar putea fi deranjante pentru planta sau pentru
// utilizator.
void task_fan_pwm()
{
    static uint8_t startup_boost = 0;

    // boost initial pentru pornirea ventilatorului
    if (fan_current_speed == 0 && fan_target_speed > 0 && !startup_boost) {
        fan_current_speed = 100;
        startup_boost = 1;
        fan_set_speed(fan_current_speed);
        return;
    }

    // ajustare graduala spre viteza dorita
    if (fan_current_speed < fan_target_speed) {
        fan_current_speed++;
    }
    else if (fan_current_speed > fan_target_speed) {
        fan_current_speed--;
    }

    // reset boost daca ventilatorul este complet oprit
    if (fan_target_speed == 0 && fan_current_speed == 0) {
        startup_boost = 0;
    }

    fan_set_speed(fan_current_speed);
}


void task_display() {
    // page pentru a alterna intre afisarea umiditatii, temperaturii
    // si luminii, a.i sa putem vedea toate informatiile relevante
    // fara a aglomera ecranul cu prea multe date in acelasi timp.
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
        sprintf(buffer, "Lux: %d", light_lux);
    }

    LCD_printLine(buffer);
    page = (page >= 2) ? 0 : page + 1;
}

// aici folosesc debounde software, detectarea short pressului si
// detectarea long press-ului pentru a schimba intre modurile
// de functionare ale sistemului
void handle_button() {
    // ultima stare a butonului (1 = neapasat, 0 = apasat).
    static uint8_t last_state = 1;
    // timpul ultimei intreruperi valide, pentru debounce.
    static uint32_t last_debounce = 0;
    // momentul in care butonul a fost apasat.
    static uint32_t press_start = 0;

    uint32_t now = uptime_ms();

    // daca nu exista vreun eveniment de intrerupere, ies din functie.
    if (!button_interrupt_flag) return;
    
    // resetez flag-ul de intrerupere
    button_interrupt_flag = 0;

    // debounce software: ignor apasarile foarte apropiate.
    if (now - last_debounce < DEBOUNCE_MS) {
        return;
    }
    last_debounce = now;

    // citesc starea actuala a butonului.
    uint8_t current_state = (BUTTON_PINREG & (1 << BUTTON_PIN)) ? 1 : 0;

    // detectez in momentul apasarii: tranzitie HIGH -> LOW
    if (last_state == 1 && current_state == 0) {
        // si salvez momentul inceperii apasarii:
        press_start = now; // Pressed
    }

    // aici detectez eliberarea butonului, tranizita LOW -> HIGH.
    if (last_state == 0 && current_state == 1) {
        // calculez durata apasarii:
        uint32_t duration = now - press_start;
        // daca e LONG PRESS -> activez modul ECO.
        if (duration >= LONG_PRESS_MS) {
            currentMode = MODE_ECO;
            GREEN_PORT &= ~(1 << GREEN_PIN); // LED Verde ON
            LCD_printLine("MODE ECO");
            BT_log_event("MODE", "ECO");
            buzzer_beep(1);
        } else {
            // SHORT PRESS:
            if (currentMode == SYSTEM_OFF) {
                currentMode = MODE_CLASSIC;
                GREEN_PORT &= ~(1 << GREEN_PIN); // LED Verde ON
                LCD_printLine("MODE CLASSIC");
                BT_log_event("MODE", "CLASSIC");
                buzzer_beep(1);
            } else {
                // daca sistemul era deja pornit, un short
                // pres va opri complet sistemul.
                currentMode = SYSTEM_OFF;
                GREEN_PORT |= (1 << GREEN_PIN); // LED Verde OFF
                pump_off();

                fan_target_speed = 0;
                fan_current_speed = 0;
                fan_set_speed(0);

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
    // // verific daca exista date primite prin Bluetooth
    while (UART_available()) {
        // citesc un caracter primit.
        char cmd = UART_receiveChar();

        // trimit inapoi comanda pentru debugging:
        UART_sendString("CMD:");
        UART_sendChar(cmd);
        UART_sendString("\r\n");

        // ignor ENTER / newline
        if (cmd == '\n' || cmd == '\r') {
            continue;
        }
        // comanda S -> pornire / oprire sistem
        if (cmd == 'S') {
            // daca sistemul este oprit, il pornesc in modul
            // clasic.
            if (currentMode == SYSTEM_OFF) {
                currentMode = MODE_CLASSIC;
                GREEN_PORT &= ~(1 << GREEN_PIN); // ON

                LCD_printLine("MODE CLASSIC");
                // BT_log_event("MODE", "CLASSIC");
                buzzer_beep(1);
            } else {
                currentMode = SYSTEM_OFF;
                GREEN_PORT |= (1 << GREEN_PIN); // OFF

                pump_off();
                fan_target_speed = 0;
                fan_current_speed = 0;
                fan_set_speed(0);

                LCD_printLine("SYSTEM OFF");
                // BT_log_event("MODE", "OFF");
                buzzer_beep(2);
            }
        }
        else if (cmd == 'E') {
            currentMode = MODE_ECO;
            GREEN_PORT &= ~(1 << GREEN_PIN); // ON
            LCD_printLine("MODE ECO");
            BT_log_event("MODE", "ECO");
            buzzer_beep(1);
        }
        // setarea profilului plantei
        else if (cmd == 'N' || cmd == 'C' || cmd == 'F') {
            set_plant_profile(cmd);
        }

    }
}



// intializez porturile hardware, senzorii, led-urile, pompa, fanul
// si intreruperile necesare pentru buton si timer
void hardware_init() {
    ADC_init();
    UART_init(103); // 9600 baud
    I2C_init();
    LCD_init();
    TIMER0_init();
    BUTTON_interrupt_init();
    pwm_init();


    // Outputurile
    FAN_DDR |= (1 << FAN_PIN);
    PUMP_DDR |= (1 << PUMP_PIN);
    BUZZER_DDR |= (1 << BUZZER_PIN);
    GREEN_DDR |= (1 << GREEN_PIN);
    YELLOW_DDR |= (1 << YELLOW_PIN);
    RED_DDR |= (1 << RED_PIN);

    // initializarea starii (cu toate oprite)
    FAN_PORT &= ~(1 << FAN_PIN);
    PUMP_PORT &= ~(1 << PUMP_PIN);
    BUZZER_PORT &= ~(1 << BUZZER_PIN);
    GREEN_PORT |= (1 << GREEN_PIN);
    YELLOW_PORT |= (1 << YELLOW_PIN);
    RED_PORT |= (1 << RED_PIN);

    // Water sensor input cu pull-up
    WATER_DDR &= ~(1 << WATER_PIN);
    WATER_PORT |= (1 << WATER_PIN); 

    sei(); // activarea pt intreruperi globale
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

    // pentru partea de pwm:
    uint32_t last_fan_pwm = 0;

    while (1) {
        uint32_t now = uptime_ms();
        handle_button();

        /* Legarea cu aplicatia andorid*/
        task_bluetooth_commands();

        if (now - last_sensor >= SENSOR_PERIOD_MS) {
            last_sensor = now;
            task_read_sensors();
        }

        if (now - last_control >= CONTROL_PERIOD_MS) {
            last_control = now;
            task_control();
        }

        // task_fan_pwm:
        if (now - last_fan_pwm >= 50) {
            last_fan_pwm = now;
            task_fan_pwm();
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

