// #define F_CPU 9600000UL

#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#include "mcp23x08.h"

#define FALSE 0
#define TRUE !(FALSE)

#define VIBRATOR_PIN PB0
#define VIBRATE_PULSE 255

#define LEDS_PIN_DATA PB1
#define LEDS_PIN_SERIALCLOCK PB2
#define LEDS_PIN_CHIPSELECT PB3
#define MCP23X08_SLAVE_ADDRESS_A0 0
#define MCP23X08_SLAVE_ADDRESS_A1 0
#define NUM_LEDS 6
#define NUM_LED_PATTERNS 4
#define ANIMATION_REPETITION 5

#define PHOTOCELL_PIN PB4
#define PHOTOCELL_ACTIVATE_THRESHOLD 330 // TODO: make this a diff from ambient?

uint8_t isAnimating = FALSE;
uint16_t currentAmbient = 800;
mcp23s08Device mcp23s08;

inline void ledsWrite(uint8_t leds) {
    MCP23S08_GpioWrite(&mcp23s08, leds);
}

void ledPattern_Alternating(void) {
    ledsWrite(0b01101010);
    _delay_ms(500);
    ledsWrite(0b01010101);
    _delay_ms(500);
}

void ledPattern_KITT(void) {
    int i = 0;
    for (; i < NUM_LEDS; i++) {
        ledsWrite(((1 << i)|(1 << NUM_LEDS)));
        _delay_ms(100);
    }
    i -= 2;
    for (; i >= 0; i--) {
        ledsWrite(((1 << i)|(1 << NUM_LEDS)));
        _delay_ms(100);
    }
}

void ledPattern_LandingStrip(void) {
    uint8_t pattern = 0;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        pattern |= (1 << i);
        ledsWrite((pattern|(1 << NUM_LEDS)));
        _delay_ms(150);
    }
    ledsWrite(0b01000000);
    _delay_ms(150);
}

void ledPattern_Blinky(void) {
    ledsWrite(0b01111111);
    _delay_ms(500);
    ledsWrite(0b01000000);
    _delay_ms(500);
}

uint16_t readPhotocell(void) {
    uint16_t photocellVal = 0;
    for (uint8_t i = 0; i < 8; i++) {
        ADCSRA |= (1 << ADSC); // Start the conversion

        while (ADCSRA & (1 << ADSC)); // Wait for conversion

        photocellVal += ADC;
    }

    return (uint16_t)(photocellVal / 8);
}

void animateLeds(void) {
    uint16_t rand = readPhotocell() % (NUM_LED_PATTERNS-1);
    isAnimating = TRUE;
    for (uint8_t i = 0; i < ANIMATION_REPETITION && isAnimating; i++) {
        switch (rand) {
        case 0: ledPattern_Alternating(); break;
        case 1: ledPattern_KITT(); break;
        case 2: ledPattern_LandingStrip(); break;
        case 3: ledPattern_Blinky(); break;
        default: break;
        }
    }
}

inline void vibrate(uint16_t pulse) {
    OCR0A = pulse;
    _delay_ms(500);
    OCR0A = 0x00;
}

inline int isTouching(void) {
    // TODO: better way to determine this?
    if (readPhotocell() < PHOTOCELL_ACTIVATE_THRESHOLD) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void analyze_and_activate(void) {
    if (isTouching()) {
        // if (isAnimating) {
        //     isAnimating = FALSE;
        // }
        vibrate(VIBRATE_PULSE);
        animateLeds();
    } else { // Turn off all LEDs
        // isAnimating = FALSE;
        ledsWrite(0x00);
    }
}

ISR(WDT_vect) {
    ADCSRA |= (1 << ADEN);  // Enable ADC
    analyze_and_activate();
    ADCSRA &= ~(1 << ADEN);  // Disable ADC
}

inline void init_pins(void) {
    // Vibrator
    TCCR0B |= (1 << CS01); // clock/8 (See 11-9)
    TCCR0A |= (1 << WGM01) | (1 << WGM00); // Set for fast PWM with 0xFF Max (See 11-8)
    TCCR0A |= (1 << COM0A1); // Clear on compare match (See 11-5)

    DDRB |= (1 << VIBRATOR_PIN);

    // LEDS
    mcp23s08.spi.chipSelect = LEDS_PIN_CHIPSELECT;
    mcp23s08.spi.serialClock = LEDS_PIN_SERIALCLOCK;
    mcp23s08.spi.serialDataInput = LEDS_PIN_DATA;

    MCP23S08_Init(MCP23X08_SLAVE_ADDRESS_A0, MCP23X08_SLAVE_ADDRESS_A1, &mcp23s08);
    MCP23S08_IodirWrite(&mcp23s08, 0x00); // Set all pins as output pins
    ledsWrite(0x00); // Start them off

    // The photocell ADC. Enable ADC2 / PB4 as an ADC pin
    ADMUX |= (0 << REFS0) | (1 << MUX1) | (0 << MUX0);
    ADCSRA |= (1 << ADPS1) | (1 << ADPS0) | (1 << ADEN); // Enable ADC and set prescaler to clock/128
}

inline void init_interrupts(void) {
    // See table 8-2 on datasheet
    WDTCR |= (1 << WDP2) | (1 << WDP0); // Sleep for ~30s
    // WDTCR |= (1 << WDP2); // Sleep for ~15s
    WDTCR |= (1 << WDTIE); // Enable watchdog timer

    sei();

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}

int __attribute__((OS_main)) main(void) {
    init_pins();
    init_interrupts();

    ADCSRA &= ~(1 << ADEN);  // Disable ADC (to save power)

    currentAmbient = readPhotocell();

    for (;;) {
        sleep_mode();
    }

    return 0;
}
