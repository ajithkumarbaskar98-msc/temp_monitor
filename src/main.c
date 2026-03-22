/*******************************************************************************
 * STM32L432KC — Temperature Monitor
 * - Internal temperature sensor with VDDA compensation via VREFINT
 * - TIM2 CH1 PWM buzzer on PA0 (~3kHz), synced with LED blink on PB3
 * - Button on PB4 (active LOW) shuts everything down
 * - Serial output at 9600 baud on USART2 (PA2=TX)
 * - LCD display of detected and threshold temperature with status
 * - Refer Schematic.kicad_sch file for pin configurations
 ******************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <sys/unistd.h>
#include "eeng1030_lib.h"
#include "display.h"

/*
 * Factory calibration values are written to flash at fixed addresses during
 * STM32 production. Reading them directly avoids any need for manual tuning.
 */
#define TS_CAL1      (*((volatile uint16_t *)0x1FFF75A8))   /* ADC raw count at  30°C, VDDA=3.0V */
#define TS_CAL2      (*((volatile uint16_t *)0x1FFF75CA))   /* ADC raw count at 130°C, VDDA=3.0V */
#define VREFINT_CAL  (*((volatile uint16_t *)0x1FFF75AA))   /* Internal VREFINT raw count @ VDDA=3.0V */

/* Function prototypes */
void setup(void);
void initADC(void);
void initSerial(uint32_t baudrate);
void initTIM2_PWM(void);
void buzzer_on(void);
void buzzer_off(void);
void stop_all(void);
int  readADC(int chan);
float readTemperature(void);
void updateDisplay(float temp, int breached);
void eputc(char c);

/* Set to 1 the first time temperature meets or exceeds the threshold. Never cleared. */
volatile int alert_active = 0;

int main(void)
{
    setup();
    init_display();

    /* Draw static UI elements once — only dynamic lines are refreshed each loop */
    fillRectangle(0, 0, 160, 80, RGBToWord(0, 0, 0));           /* clear full screen to black        */
    printText("Temp Monitor",    5,  0, RGBToWord(255, 255, 255), 0); /* white colour at top           */
    printText("Thresh: 19.00 C", 5, 40, RGBToWord(255, 255,   0), 0); /* yellow threshold label       */

    /* Print startup banner and calibration constants over serial */
    printf("\r\n=== STM32L432KC Temperature Monitor ===\r\n");
    printf("Threshold : 19.00 C\r\n");
    printf("Buzzer    : PA0 TIM2_CH1 ~3kHz\r\n");
    printf("Display   : SPI LCD 160x80\r\n");
    printf("TS_CAL1   (30C  @ 3.0V): %u\r\n", (unsigned)TS_CAL1);
    printf("TS_CAL2   (130C @ 3.0V): %u\r\n", (unsigned)TS_CAL2);
    printf("VREFINT_CAL    (@ 3.0V): %u\r\n", (unsigned)VREFINT_CAL);
    printf("-------------------------------------------\r\n");

    int sample = 0; /* Running count of temperature readings taken */

    while (1)
    {
        /*
         * Poll button on PB4 (active LOW).
         * A 5 ms delay between two reads debounces mechanical contact bounce before committing to a full shutdown.
         */
        if ((GPIOB->IDR & (1 << 4)) == 0)
        {
            delay_ms(5);
            if ((GPIOB->IDR & (1 << 4)) == 0)
                stop_all();     /* disables all peripherals and loops forever */
        }

        float temp = readTemperature(); /* compensated temperature in degrees C */
        sample++;

        /*
         * Once the threshold is crossed, alert_active latches HIGH and
         * stays set even if the temperature later drops below defined threshold temperature.
         * This ensures the LED keeps blinking during cool-down.
         */
        if (temp >= 19.00f)
            alert_active = 1;

        /*
         * printf does not support %f on this toolchain, so split the float
         * into integer whole and fractional parts for formatted output.
         */
        int whole = (int)temp;
        int frac  = (int)((temp - whole) * 100);
        if (frac < 0) frac = -frac; /* guard against negative fractional part on sub-zero values */

        printf("[%4d] Temp: %d.%02d C", sample, whole, frac);
        if (temp >= 19.00f)
            printf("  <<< THRESHOLD! [LED+BUZZER]\r\n");
        else if (alert_active)
            printf("      (cooling)  [LED blinking]\r\n"); /* threshold was crossed but temp has since dropped */
        else
            printf("      (normal)\r\n");

        /* Redraw the temperature and status lines on the LCD */
        updateDisplay(temp, alert_active);

        /*
         * Alert blink sequence: 250 ms ON / 250 ms OFF for LED.
         * Buzzer is added during ON phase only if temperature is still above threshold.
         * When no alert, LED is off and the loop waits a full 500 ms between readings.
         */
        if (alert_active)
        {
            GPIOB->ODR |= (1 << 3);     /* PB3 HIGH — LED on  */
            if (temp >= 19.00f)
                buzzer_on();            /* audible alert only while actively over threshold */
            delay_ms(250);

            GPIOB->ODR &= ~(1 << 3);   /* PB3 LOW  — LED off */
            buzzer_off();
            delay_ms(250);
        }
        else
        {
            GPIOB->ODR &= ~(1 << 3);   /* keep LED off in normal operation */
            buzzer_off();
            delay_ms(500);
        }
    }
}

/*------------------------------------------------------------------------------
 * updateDisplay()
 *
 * Refreshes the two dynamic LCD lines:
 *   Row 1 (y=20): current temperature in cyan
 *   Row 2 (y=60): "THRESH BREACHED" in red, or "Normal" in green
 *
 * Each line's band is filled black before redrawing to prevent text ghosting.
 *----------------------------------------------------------------------------*/
void updateDisplay(float temp, int breached)
{
    char buf[20];
    int whole = (int)temp;
    int frac  = (int)((temp - whole) * 100);
    if (frac < 0) frac = -frac;

    /* Erase and redraw temperature line */
    fillRectangle(0, 18, 160, 17, RGBToWord(0, 0, 0));
    sprintf(buf, "Temp:%3d.%02d C", whole, frac);
    printText(buf, 5, 20, RGBToWord(0, 255, 255), 0); /* cyan text */

    /* Erase and redraw status line */
    fillRectangle(0, 58, 160, 17, RGBToWord(0, 0, 0));
    if (breached)
        printText("THRESH BREACHED", 5, 60, RGBToWord(255, 0,   0), 0); /* red   */
    else
        printText("Normal",          5, 60, RGBToWord(0,   255, 0), 0); /* green */
}

/*------------------------------------------------------------------------------
 * stop_all()
 *
 * Called on button press. Silences the buzzer, turns off the LED, shows a
 * STOPPED screen, then disables TIM2 and the ADC before spinning forever.
 * A hardware RESET is required to restart the system.
 *----------------------------------------------------------------------------*/
void stop_all(void)
{
    buzzer_off();
    GPIOB->ODR &= ~(1 << 3);   /* turn off LED */

    /* Overwrite LCD with shutdown message */
    fillRectangle(0, 0, 160, 80, RGBToWord(0, 0, 0));
    printText("STOPPED",     30, 25, RGBToWord(255,   0,   0), 0); /* red   */
    printText("Press RESET", 15, 45, RGBToWord(255, 255, 255), 0); /* white */

    TIM2->CR1  &= ~(1 << 0);   /* CEN=0: stop TIM2 counter         */
    ADC1->CR   |=  (1 << 1);   /* ADDIS=1: request ADC disable     */
    while (ADC1->CR & (1 << 0)); /* ADEN bit clears when ADC is off */

    printf("\r\n[STOPPED] Button pressed.\r\n");
    printf("LED off. Buzzer off. TIM2 stopped. ADC disabled.\r\n");
    printf("Press RESET to restart.\r\n");
    while (1); /* halt — only a hardware reset can resume execution */
}

/*------------------------------------------------------------------------------
 * readTemperature()
 *
 * Reads the internal temperature sensor (ch17) and the internal voltage
 * reference VREFINT (ch0), then applies a VDDA compensation factor so that
 * supply voltage variation does not skew the result.
 *
 * Formula (from STM32L4 reference manual):
 *   factor     = VREFINT_CAL / vref_raw       (normalises to 3.0V reference)
 *   corrected  = ts_raw * factor               (supply-compensated ADC count)
 *   temp (°C)  = (corrected - TS_CAL1) * 100  / (TS_CAL2 - TS_CAL1) + 30
 *----------------------------------------------------------------------------*/
float readTemperature(void)
{
    int ts_raw   = readADC(17);  /* raw ADC count from internal temperature sensor */
    int vref_raw = readADC(0);   /* raw ADC count from internal VREFINT channel    */

    float factor    = (float)VREFINT_CAL / (float)vref_raw; /* VDDA compensation ratio */
    float corrected = (float)ts_raw * factor;                /* supply-corrected ts count */

    /* Linear interpolation between the two factory calibration points */
    return ((corrected - (float)TS_CAL1) * (130.0f - 30.0f)
            / (float)((int)TS_CAL2 - (int)TS_CAL1)) + 30.0f;
}

/*------------------------------------------------------------------------------
 * readADC()
 *
 * Performs a single software-triggered ADC conversion on the given channel.
 * The ADC is enabled, started, and disabled on every call (single-shot mode).
 *
 * Bit operations map to ADC_CR register fields:
 *   bit 0  ADEN  — ADC enable
 *   bit 1  ADDIS — ADC disable
 *   bit 2  ADSTART — start conversion
 * ADC_ISR fields:
 *   bit 0  ADRDY — ADC ready
 *   bit 2  EOC   — end of conversion
 *----------------------------------------------------------------------------*/
int readADC(int chan)
{
    ADC1->SQR1  = (chan << 6);    /* set the single conversion channel (SQ1 field) */
    ADC1->ISR   = 0xFFFFFFFF;     /* clear all ADC status/interrupt flags           */

    ADC1->CR   |= (1 << 0);       /* ADEN=1: enable ADC                             */
    while (!(ADC1->ISR & (1 << 0))); /* wait for ADRDY — ADC ready to convert       */

    ADC1->CR   |= (1 << 2);       /* ADSTART=1: begin conversion                    */
    while (!(ADC1->ISR & (1 << 2))); /* wait for EOC — conversion complete          */

    int result  = ADC1->DR;       /* reading DR also clears the EOC flag             */

    ADC1->CR   |= (1 << 1);       /* ADDIS=1: disable ADC after reading              */
    while (ADC1->CR & (1 << 0));  /* wait for ADEN to clear, confirming disable      */

    return result;
}

/*------------------------------------------------------------------------------
 * buzzer_on() / buzzer_off()
 *
 * Control TIM2 CH1 output compare mode without touching the running counter.
 * OC1M field is bits [6:4] of CCMR1.
 *   Mode 6 (110b) = PWM mode 1 — PA0 toggles at ~3 kHz (audible tone)
 *   Mode 4 (100b) = Force inactive — PA0 held LOW, counter keeps running
 *
 * Masking with ~(7<<4) clears the three OC1M bits before OR-ing in the new value.
 *----------------------------------------------------------------------------*/
void buzzer_on(void)
{
    TIM2->CCMR1 = (TIM2->CCMR1 & ~(7 << 4)) | (6 << 4); /* OC1M = PWM mode 1 */
}

void buzzer_off(void)
{
    TIM2->CCMR1 = (TIM2->CCMR1 & ~(7 << 4)) | (4 << 4); /* OC1M = force inactive (PA0 LOW) */
}

/*------------------------------------------------------------------------------
 * setup()
 *
 * Brings up the minimum set of clocks, GPIO pins, and peripherals needed before the main loop runs. 
 * Order matters: clocks must be enabled before any peripheral registers are accessed.
 *----------------------------------------------------------------------------*/
void setup(void)
{
    initClocks(); /* ramp MSI from 4 MHz up to 80 MHz via the PLL */

    /*
     * Configure SysTick for a 1 ms interrupt period.
     * LOAD = (80 MHz / 1 kHz) - 1 = 79999
     * CTRL bits: CLKSOURCE=1 (processor clock), TICKINT=1, ENABLE=1
     */
    SysTick->LOAD = 80000 - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = 7;
    __asm(" cpsie i "); /* CPSR I-bit clear — unmask all interrupts */

    /*
     * Enable peripheral clocks via RCC:
     *   AHB2ENR  bit 0  = GPIOAEN
     *   AHB2ENR  bit 1  = GPIOBEN
     *   AHB2ENR  bit 13 = ADCEN
     *   APB1ENR1 bit 17 = USART2EN
     *   APB1ENR1 bit 0  = TIM2EN
     */
    RCC->AHB2ENR  |= (1 << 0) | (1 << 1) | (1 << 13);
    RCC->APB1ENR1 |= (1 << 17) | (1 << 0);

    /*
     * PB3 = push-pull output (LED)
     * PB4 = input with internal pull-up (button, active LOW)
     */
    GPIOB->MODER &= ~((3 << 6) | (3 << 8)); /* clear MODER bits for PB3 and PB4  */
    GPIOB->MODER |=   (1 << 6);             /* PB3 = general-purpose output (01) */
    GPIOB->PUPDR &= ~(3 << 8);              /* clear PUPDR bits for PB4           */
    GPIOB->PUPDR |=  (1 << 8);             /* PB4 = pull-up (01)                 */

    /*
     * Alternate function assignments for PA0 and PA2:
     *   PA0 = AF1 → TIM2_CH1 (buzzer PWM output)
     *   PA2 = AF7 → USART2_TX (serial transmit)
     *
     * PA1, PA4–PA7 are intentionally left untouched here;
     * they are configured inside initSPI() and init_display().
     */
    GPIOA->MODER  &= ~((3 << 0) | (3 << 4));       /* clear mode bits for PA0, PA2     */
    GPIOA->MODER  |=   (2 << 0) | (2 << 4);        /* PA0=AF(10), PA2=AF(10)           */
    GPIOA->AFR[0] &= ~((0xF << 0) | (0xF << 8));   /* clear AF nibbles for PA0, PA2    */
    GPIOA->AFR[0] |=   (1   << 0) | (7   << 8);    /* PA0=AF1 (TIM2), PA2=AF7 (USART2) */

    initADC();
    initSerial(9600);
    initTIM2_PWM();
}

/*------------------------------------------------------------------------------
 * initADC()
 *
 * Configures ADC1 for 12-bit single-shot conversions using the system clock,
 * with the internal temperature sensor and VREFINT channels both enabled.
 *
 * Key steps:
 *   1. Route sysclk to ADC via CCIPR (bits 29:28 = 11)
 *   2. Enable VREFINT (CCR bit 22) and temperature sensor (CCR bit 23)
 *      via the ADC common register; set clock prescaler /1 (bit 16)
 *   3. Exit deep-power-down (DEEPPWD=0), enable voltage regulator (ADVREGEN=1)
 *   4. Wait for regulator start-up, then run self-calibration
 *   5. Set maximum sample time on ch0 (SMPR1) and ch17 (SMPR2) for accuracy
 *   6. JQDIS=1 (CFGR bit 31) — disable injected queue to prevent conflicts
 *----------------------------------------------------------------------------*/
void initADC(void)
{
    RCC->CCIPR        |= (1 << 29) | (1 << 28);     /* sysclk as ADC clock source          */
    ADC1_COMMON->CCR   = (1 << 16) | (1 << 22) | (1 << 23); /* /1 prescaler, VREFINT, TSEN */
    ADC1->CR           = 0;                          /* clear CR: exits deep power-down     */
    ADC1->CR           = (1 << 28);                  /* ADVREGEN=1: enable voltage regulator */
    delay(100);                                       /* wait for regulator to stabilise     */
    ADC1->CR          |= (1 << 31);                  /* ADCAL=1: start calibration          */
    while (ADC1->CR & (1 << 31));                    /* wait until ADCAL clears (done)      */
    ADC1->SMPR1        = (7 << 0);                   /* ch0  (VREFINT): 640.5 cycle sample  */
    ADC1->SMPR2        = (7 << 21);                  /* ch17 (temp sensor): 640.5 cycles    */
    ADC1->CFGR         = (1 << 31);                  /* JQDIS=1: disable injected queue     */
}

/*------------------------------------------------------------------------------
 * initTIM2_PWM()
 *
 * Configures TIM2 channel 1 to drive a ~3 kHz PWM signal on PA0 (AF1).
 *
 * Clock chain:
 *   Sysclk = 80 MHz
 *   PSC    = 79   → timer tick = 80 MHz / (79+1) = 1 MHz
 *   ARR    = 332  → PWM period = 1 MHz / (332+1) ≈ 3003 Hz (~3 kHz)
 *   CCR1   = 166  → 50% duty cycle (166/333 ≈ 49.8%)
 *
 * The output compare mode is initially set to "force inactive" (OC1M=100)
 * so the buzzer stays silent until buzzer_on() is explicitly called.
 * OC1PE=1 enables the CCR1 preload register for glitch-free updates.
 *----------------------------------------------------------------------------*/
void initTIM2_PWM(void)
{
    TIM2->PSC   = 79;                           /* prescaler: 80 MHz → 1 MHz tick       */
    TIM2->ARR   = 332;                          /* auto-reload: sets PWM frequency       */
    TIM2->CCR1  = 166;                          /* compare value: 50% duty cycle         */
    TIM2->CCMR1 = (4 << 4) | (1 << 3);         /* OC1M=force inactive, OC1PE=preload on */
    TIM2->CCER  = (1 << 0);                     /* CC1E=1: enable CH1 output             */
    TIM2->EGR   = (1 << 0);                     /* UG=1: update event, latches PSC/ARR   */
    TIM2->CR1   = (1 << 0);                     /* CEN=1: start counter                  */
}

/*------------------------------------------------------------------------------
 * initSerial()
 *
 * Configures USART2 for TX-only output at the requested baud rate.
 * PA2 must already be set to AF7 before this is called.
 *
 * BRR is set using integer division (OVER8=0, so BRR = fCLK / baud).
 * CR3 bit 12 (OVRDIS) disables the overrun error flag so it cannot
 * stall the transmitter if data is consumed faster than it arrives.
 *----------------------------------------------------------------------------*/
void initSerial(uint32_t baudrate)
{
    USART2->CR1 = 0;                          /* disable USART before configuration   */
    USART2->CR2 = 0;                          /* default framing: 1 start, 8 data, 1 stop */
    USART2->CR3 = (1 << 12);                  /* OVRDIS=1: disable overrun detection  */
    USART2->BRR = 80000000 / baudrate;        /* baud rate divisor (OVER8=0)          */
    USART2->CR1 = (1 << 3);                   /* TE=1: enable transmitter             */
    USART2->CR1|= (1 << 0);                   /* UE=1: enable USART                   */
}

/*------------------------------------------------------------------------------
 * eputc()
 *
 * Transmits a single character over USART2.
 * Busy-waits on TXE (bit 7 of ISR) to ensure the previous byte has been
 * moved from TDR to the shift register before writing the next character.
 *----------------------------------------------------------------------------*/
void eputc(char c)
{
    while ((USART2->ISR & (1 << 7)) == 0); /* wait for TXE — transmit data register empty */
    USART2->TDR = c;
}

/*------------------------------------------------------------------------------
 * _write()
 *
 * Newlib syscall hook that redirects printf/puts output to USART2.
 * Called automatically by the C runtime for writes to stdout or stderr.
 * Returns -1 with errno=EBADF for any other file descriptor.
 *----------------------------------------------------------------------------*/
int _write(int file, char *data, int len)
{
    if ((file != STDOUT_FILENO) && (file != STDERR_FILENO))
    {
        errno = EBADF;
        return -1;
    }
    while (len--)
        eputc(*data++);
    return 0;
}