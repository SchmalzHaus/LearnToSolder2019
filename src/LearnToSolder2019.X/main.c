/*
 * Learn To Solder 2018 board software
 * 
 * Written by Brian Schmalz of Schmalz Haus LLC
 * brian@schmalzhaus.com
 * 
 * Copyright 2018
 * All of this code is in the public domain
 * 
 * Versions:
 * 
 */

#include "mcc_generated_files/mcc.h"

// Starting time, in ms, between switching which LED is currently on in main pattern
#define SLOW_DELAY          250

// Maximum number of patterns allowed in the battery array
#define NUMBER_OF_PATTERNS    8

// Button debounce time in milliseconds
#define BUTTON_DEBOUNCE_MS   20

// Number of milliseconds to stay awake for before sleeping just to see if another
// button will be pressed
#define SHUTDOWN_DELAY_MS   100

// Time between two button presses below which is considered 'short'
#define QUICK_PRESS_MS      250

/* Switch input :  (pressed = low)
 * S1 = GP3
 * 
 * LEDs: (from left to right in claws)
 * D1 = GP0 
 * D2 = GP1
 * D3 = GP2
 * D4 = GP4
 * D5 = GP5
 */

// I/O pin that push button is on
#define BUTTON_IO             PORTAbits.RA3

#define TRISA_LEDS_ALL_OUTUPT 0xC8    // 0b11001000
#define PORTA_LEDS_ALL_LOW    0x00

#define D1_ON                 LATAbits.LATA0 = 1;
#define D1_OFF                LATAbits.LATA0 = 0;
#define D2_ON                 LATAbits.LATA1 = 1;
#define D2_OFF                LATAbits.LATA1 = 0;
#define D3_ON                 LATAbits.LATA2 = 1;
#define D3_OFF                LATAbits.LATA2 = 0;
#define D4_ON                 LATAbits.LATA4 = 1;
#define D4_OFF                LATAbits.LATA4 = 0;
#define D5_ON                 LATAbits.LATA5 = 1;
#define D5_OFF                LATAbits.LATA5 = 0;

#define LED_D1                0x01  // D3 State 1 A0 high
#define LED_D2                0x02  // D4 State 0 A1 high
#define LED_D3                0x04  // D1 State 3 A4 high
#define LED_D4                0x08  // D2 State 2 A5 high
#define LED_D5                0x10  // D8 State 0 A5 high

#define PATTERN_OFF_STATE     0 // State for all patterns where they are inactive

// Index for each pattern into the patterns arrays
#define PATTERN_RIGHT_FLASH   0
#define PATTERN_LEFT_FLASH    1
#define PATTERN_RIGHT_GAME    2

// Maximum number of milliseconds to allow system to run
#define MAX_AWAKE_TIME_MS     (5UL * 60UL * 1000UL)

// The five states a button can be in (for debouncing))
typedef enum {
    BUTTON_STATE_IDLE = 0,
    BUTTON_STATE_PRESSED_TIMING,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASED_TIMING,
    BUTTON_STATE_RELEASED
} ButtonState_t;

static uint8_t TRISTable[] =
{
  0xFC,     // Right Red
  0xFC,     // Right Green
  0xCF,     // Right Blue
  0xCF,     // Right Yellow
  0xDE,     // Left Yellow
  0xDE,     // Left Blue
  0xED,     // Left Green
  0xED      // Left Red
};

static uint8_t PORTTable[] =
{
  0x01,     // Right Red
  0x02,     // Right Green
  0x10,     // Right Blue
  0x20,     // Right Yellow
  0x20,     // Left Yellow
  0x01,     // Left Blue
  0x10,     // Left Green
  0x02      // Left Red
};

// Each bit represents an LED. Set high to turn that LED on. Interface from mainline to ISR
static volatile uint8_t LEDOns = 0;
// Counts up from 0 to 7, represents the LED number currently being serviced in the ISR
static uint8_t LEDState = 0;

// Each pattern has a delay counter that counts down at a 1ms rate
volatile uint16_t PatternDelay[NUMBER_OF_PATTERNS];
// Each pattern has a state variable defining what state it is in
volatile uint8_t PatternState[NUMBER_OF_PATTERNS];

// Counts number of milliseconds we are awake for, and puts us to sleep if 
// we stay awake for too long
volatile static uint32_t WakeTimer = 0;

// Counts down from SHUTDOWN_DELAY_MS after everything is over before we go to sleep
volatile static uint8_t ShutdownDelayTimer = 0;

// Countdown 1ms timers to  debounce the button inputs
volatile static uint8_t DebounceTimer = 0;

// Keep track of the state of each button during debounce
volatile static ButtonState_t ButtonState = BUTTON_STATE_IDLE;

// Record the last value of WakeTimer when the button was pushed
volatile static uint32_t LastButtonPressTime = 0;

//void SetLEDOn(uint8_t LED)
//{
//  LEDOns = (uint8_t)(LEDOns | LED);
//}

//void SetLEDOff(uint8_t LED)
//{
//  LEDOns = (uint8_t)(LEDOns & ~LED);
//}

void SetAllLEDsOff(void)
{
  LEDOns = 0;
}

/* This ISR runs every 125 uS. It takes the values in LEDState and lights
 * up the LEDs appropriately.
 * It also handles a number of software timer decrementing every 1ms.
 */
void TMR0_Callback(void)
{
  uint8_t i;

#if 0
  // Default all LEDs to be off
  TRISA = TRISA_LEDS_ALL_OUTUPT;
  PORTA = PORTA_LEDS_ALL_LOW;

  // Create local bit pattern to test for what LED we should be thinking about
  i = (uint8_t)(1 << LEDState);
  
  // If the bit in LEDOns we're looking at is high (i.e. LED on)
  if (i & LEDOns)
  {
    // Then set the tris and port registers from the tables
    TRISA = TRISTable[LEDState];
    PORTA = PORTTable[LEDState];
  }
#endif
  
  // Always increment state and bit
  LEDState++;
  if (LEDState == 8)
  {
    // Approximately 1ms has passed since last time LEDState was 0, so
    // perform the 1ms tasks

    // Always increment wake timer to count this millisecond
    WakeTimer++;

    // Handle time delays for patterns
//    for (i=0; i < 8; i++)
//    {
//      if (PatternDelay[i])
//      {
//        PatternDelay[i]--;
//      }
//    }

    LEDState = 0;

    // Decrement button debounce timers
    if (DebounceTimer)
    {
        DebounceTimer--;
    }

    if (ShutdownDelayTimer)
    {
      ShutdownDelayTimer--;
    }
  }
}

// Return the raw state of the button input
bool ButtonPressedRaw(void)
{
  return (uint8_t)(BUTTON_IO == 0);
}

// Return the logical (debounced) state of the button
bool ButtonPressed(void)
{
    return (ButtonState == BUTTON_STATE_PRESSED);
}

// Return true if button is currently down (raw)
bool CheckForButtonPushes(void)
{
  static bool LastButtonState = false;
  
  // Debounce button press
  if (ButtonPressedRaw())
  {
    if (ButtonState == BUTTON_STATE_PRESSED_TIMING)
    {
      if (DebounceTimer == 0)
      {
        ButtonState = BUTTON_STATE_PRESSED;
        
        __delay_ms(5);
        D1_ON
        __delay_ms(200);
        D1_OFF
        D2_ON
        __delay_ms(200);
        D2_OFF
        D3_ON
        __delay_ms(200);
        D3_OFF
        D4_ON
        __delay_ms(200);
        D4_OFF
        D5_ON
        __delay_ms(200);
        D5_OFF
      }
    }
    else if (ButtonState != BUTTON_STATE_PRESSED)
    {
      ButtonState = BUTTON_STATE_PRESSED_TIMING;
      DebounceTimer = BUTTON_DEBOUNCE_MS;
    }
  }
  else
  {
    if (ButtonState == BUTTON_STATE_RELEASED_TIMING)
    {
      if (DebounceTimer == 0)
      {
        ButtonState = BUTTON_STATE_RELEASED;
      }
    }
    else if (ButtonState != BUTTON_STATE_RELEASED)
    {
      ButtonState = BUTTON_STATE_RELEASED_TIMING;
      DebounceTimer = BUTTON_DEBOUNCE_MS;
    }
  }
    
  if (ButtonPressed())
  {
    if (LastButtonState == false)
    {
      PatternState[PATTERN_LEFT_FLASH] = 1;
    }
    LastButtonState = true;
  }
  else
  {
    LastButtonState = false;
  }

  return ((bool)(ButtonPressedRaw()));
}

/*
                         Main application
 */
void main(void)
{
  // initialize the device
  SYSTEM_Initialize();

  TMR0_SetInterruptHandler(TMR0_Callback);

  // When using interrupts, you need to set the Global and Peripheral Interrupt Enable bits
  // Use the following macros to:

  // Enable the Global Interrupts
  INTERRUPT_GlobalInterruptEnable();

  // Enable the Peripheral Interrupts
  INTERRUPT_PeripheralInterruptEnable();

  // Disable the Global Interrupts
  //INTERRUPT_GlobalInterruptDisable();

  // Disable the Peripheral Interrupts
  //INTERRUPT_PeripheralInterruptDisable();
    
  uint8_t i;
  bool APatternIsRunning = false;

  TRISA = TRISA_LEDS_ALL_OUTUPT;
  PORTA = PORTA_LEDS_ALL_LOW;

  __delay_ms(5);
        D1_ON
        __delay_ms(200);
        D1_OFF
        D2_ON
        __delay_ms(200);
        D2_OFF
        D3_ON
        __delay_ms(200);
        D3_OFF
        D4_ON
        __delay_ms(200);
        D4_OFF
        D5_ON
        __delay_ms(200);
        D5_OFF

  
  while (1)
  {
//    RunFlash();
    
    APatternIsRunning = false;
    for (i=0; i < 8; i++)
    {
      if (PatternState[i] != 0)
      {
        APatternIsRunning = true;
      }
    }
    if ((!APatternIsRunning && DebounceTimer == 0) || (WakeTimer > MAX_AWAKE_TIME_MS))
    {
      SetAllLEDsOff();
      // Allow LEDsOff command to percolate to LEDs
      __delay_ms(5);

      ShutdownDelayTimer = SHUTDOWN_DELAY_MS;

      while (ShutdownDelayTimer && !CheckForButtonPushes())
      {
      }

      if (ShutdownDelayTimer == 0)
      {
          // Hit the VREGPM bit to put us in low power sleep mode
        VREGCONbits.VREGPM = 1;

        SLEEP();

        // Start off with time = 0;
        WakeTimer = 0;
      }
    }

    CheckForButtonPushes();
  }
}
/**
 End of File
*/