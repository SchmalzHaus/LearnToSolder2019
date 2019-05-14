/*
 * Learn To Solder 2019 board software
 * 
 * Written by Brian Schmalz of Schmalz Haus LLC
 * brian@schmalzhaus.com
 * 
 * Copyright 2019
 * All of this code is in the public domain
 * 
 * Versions:
 * 
 */

#include "mcc_generated_files/mcc.h"

// Button debounce time in milliseconds
#define BUTTON_DEBOUNCE_MS   20

// Number of milliseconds to stay awake for before sleeping just to see if another
// button will be pressed
#define SHUTDOWN_DELAY_MS   100

/* Switch input :  (pressed = low)
 * S1 = GP3
 * 
 * LEDs: (from left to right in claws, high = lit)
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

// Bit positions of each LED within Port A
#define LED_D1                0x01  // A0
#define LED_D2                0x02  // A1
#define LED_D3                0x04  // A2
#define LED_D4                0x10  // A4
#define LED_D5                0x20  // A5

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


// Working copy of LED bits to copy directly to LATA in the ISR
static uint8_t LATALEDs;

// LED interface from mainline to ISR: a 0 to 255 brightness value for each LED
static volatile uint8_t LEDBrightness[5];

// Used only in ISR to track where in PWM cycle each LED is
static uint8_t LEDPWMCount[5];

// Counts number of milliseconds we are awake for, and puts us to sleep if 
// we stay awake for too long
volatile static uint32_t WakeTimer;

// Counts down from SHUTDOWN_DELAY_MS after everything is over before we go to sleep
volatile static uint8_t ShutdownDelayTimer;

// Countdown 1ms timers to  debounce the button inputs
volatile static uint8_t DebounceTimer;

// Keep track of the state of each button during debounce
volatile static ButtonState_t ButtonState = BUTTON_STATE_IDLE;

// Record the last value of WakeTimer when the button was pushed
volatile static uint32_t LastButtonPressTime;

volatile static uint16_t NextPatternStepTimer;

static uint8_t PatternState;

void SetAllLEDsOff(void)
{
  uint8_t i;
  
  for (i=0; i < 5; i++)
  {
      LEDBrightness[i] = 0;
  }
}

/* This ISR runs every 125 uS. It takes the values in LEDBrigthness and uses them to 
 * generate software PWM for each LEDs.
 * The PWM frequency will be 32ms (125uS * 256). The value in LEDBrightness for each LED
 * will determine for how much of that time each LED is on for.
 * It also handles a number of software timer decrementing every 1ms.
 */
void RunTMR0(void)
{
  uint8_t i;
  static uint8_t OneMSCounter = 0;

  LATALEDs = 0;
  
  if (LEDBrightness[0] > 128)
  {
    LATALEDs |= LED_D1;
  }
  if (LEDBrightness[1] > 128)
  {
    LATALEDs |= LED_D2;
  }
  if (LEDBrightness[2] > 128)
  {
    LATALEDs |= LED_D3;
  }
  if (LEDBrightness[3] > 128)
  {
    LATALEDs |= LED_D4;
  }
  if (LEDBrightness[4] > 128)
  {
    LATALEDs |= LED_D5;
  }

  // As a final step, copy over the bits we've set up for the 5 LEDs
  LATA = LATALEDs;
  
  // Check to see if it's time to run the 1ms code
  OneMSCounter++;
  if (OneMSCounter >= 8)
  {
    // Approximately 1ms has passed since last time OneMSCounter was 0, so
    // perform the 1ms tasks
    OneMSCounter = 0;

    // Always increment wake timer to count this millisecond
    WakeTimer++;

    // Decrement button debounce timers
    if (DebounceTimer)
    {
        DebounceTimer--;
    }

    if (ShutdownDelayTimer)
    {
      ShutdownDelayTimer--;
    }
    
    if (NextPatternStepTimer)
    {
      NextPatternStepTimer--;
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

// Return true if button is currently down
bool CheckForButtonPushes(void)
{  
  // Debounce button press
  if (ButtonPressedRaw())
  {
    if (ButtonState == BUTTON_STATE_PRESSED_TIMING)
    {
      if (DebounceTimer == 0)
      {
        ButtonState = BUTTON_STATE_PRESSED;
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
    
  return ((bool)(ButtonPressedRaw()));
}

uint32_t PatternStartTime;

// Trigger the start of an LED pattern
void StartPattern(void)
{
  NextPatternStepTimer = 1000;
  PatternState = 1;
  LEDBrightness[0] = 255;
  LEDBrightness[1] = 0;
  LEDBrightness[2] = 0;
  LEDBrightness[3] = 0;
  LEDBrightness[4] = 0;
}

// If an LED pattern is running, do whatever needs to be done to run it
// Return true if pattern is still playing back, false if it's done
bool RunPattern(void)
{
  switch (PatternState)
  {
    case 0:
      
      break;
 
    case 1:
      if (NextPatternStepTimer == 0)
      {
        PatternState = 2;
        LEDBrightness[0] = 0;
        LEDBrightness[1] = 255;
        LEDBrightness[2] = 0;
        LEDBrightness[3] = 0;
        LEDBrightness[4] = 0;
      }
      break;
       
    default:
      break;
  }
    return true;
  }
  return false;
}

/*
                         Main application
 */
void main(void)
{
  // initialize the device
  SYSTEM_Initialize();

  TMR0_SetInterruptHandler(RunTMR0);

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
    
  /// Are these really needed? Probably not
  TRISA = TRISA_LEDS_ALL_OUTUPT;
  PORTA = PORTA_LEDS_ALL_LOW;
  
  while (1)
  {  
    static bool PlayingPattern = false;

    CheckForButtonPushes();
    PlayingPattern = RunPattern();
    
    // If we're not already playing a pattern, has the user pressed the button?
    if (!PlayingPattern && ButtonPressed())
    {
      PlayingPattern = true;
      StartPattern();
    }

    if (!PlayingPattern && (WakeTimer > MAX_AWAKE_TIME_MS))
    {
      SetAllLEDsOff();
      // Allow off command to percolate to LEDs (maximum 32ms)
      __delay_ms(50);

      // For SHUTDOWN_DELAY_MS, check to see if user has pressed the button just as we're trying to go to sleep
      ShutdownDelayTimer = SHUTDOWN_DELAY_MS;

      while (ShutdownDelayTimer && !CheckForButtonPushes())
      {
      }

      // If the button was not pushed, this timer will be at zero, and it's time to sleep
      if (ShutdownDelayTimer == 0)
      {
          // Hit the VREGPM bit to put us in low power sleep mode
        VREGCONbits.VREGPM = 1;

        SLEEP();

        // Start off with time = 0;
        WakeTimer = 0;
      }
    }
  }
}
/**
 End of File
*/