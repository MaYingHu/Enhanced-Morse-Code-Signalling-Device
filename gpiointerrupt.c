/*
 * Copyright (c) 2015-2020, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== gpiointerrupt.c ========
 *  THIS CODE WILL SIGNAL ONE OF A NUMBER OF MESSAGES IN MORSE CODE IN THE
 *  TI CC3220S-LAUNCHXL USING THE LEDS. PRESSING A BUTTON WILL CYCLE TO THE
 *  PREVIOUS OR FOLLOWING MESSAGE ONCE THE MESSAGE IN PROGRESS HAS FINISHED.
 */

#include <stdint.h>
#include <stddef.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>

/* Driver configuration */
#include "ti_drivers_config.h"
#include <ti/drivers/Timer.h>

/* --- Housekeeping variables --- */
volatile unsigned char TimerFlag = 0;
volatile short int message_ended = 0;
volatile unsigned char button_pressed = 0;
volatile unsigned char next_message_index = 0;

/* indices for message components */
short unsigned int message_index = 0;
short unsigned int character_index = 0;
short unsigned int symbol_index = 0;
short unsigned int phase = 0;

/* array of messages */
char *messages[] = {"ss", "oo", "sos"};
short int num_messages = 3;

/* lengths of Morse code symbols */
const int dot_len = 2;
const int dash_len = 4;
const int character_pause_len = 2;
const int word_pause_len = 4;

/* function prototypes */
void timerCallback(Timer_Handle myHandle, int_fast16_t status);
void initTimer(void);
void gpioButtonFxn0(uint_least8_t index);
void gpioButtonFxn1(uint_least8_t index);
void set_leds(unsigned char led_settings);
void signal_dot(short int phase);
void signal_dash(int phase);
void character_pause(int phase);
void word_pause(int phase);
const char* get_morse(char character);
void signal_message();
short unsigned int normalize_message_index(short unsigned int next_message_index);
void configure_board();

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    /* initialize timer and counter variables */
    initTimer();
    unsigned long checkTime = 0;
    const unsigned long checkPeriod = 500;

    /* configure TI board */
    configure_board();

    /* main loop to toggle between 'SOS' and 'OK' messages */
    while(1) {

        /* call signal_message() every 500ms */
        if (checkTime >= checkPeriod) {
           signal_message();
        }

        /* change message and reset message_ended and button_pressed flags to 0
         * if button(s) have been pressed and current message has reached its end */
        if (next_message_index != message_index && message_ended == 1) {
          message_index = next_message_index = normalize_message_index(next_message_index);
          message_ended = 0;
          button_pressed = 0;
        }

        /* reset TimerFlag and increment checkTime with every period */
        while (!TimerFlag) {}
        TimerFlag = 0;
        checkTime += 100;
    }
}

/*
 *  ======== gpioTimerFxn ========
 *  Callback function for the Timer.
 */
void timerCallback(Timer_Handle myHandle, int_fast16_t status)
{
    TimerFlag = 1;
}

/*
 * Initialize the Timer
 */
void initTimer(void)
{
    Timer_Handle timer0;
    Timer_Params params;

    Timer_init();
    Timer_Params_init(&params);
    params.period = 500000;
    params.periodUnits = Timer_PERIOD_US;
    params.timerMode = Timer_CONTINUOUS_CALLBACK;
    params.timerCallback = timerCallback;

    timer0 = Timer_open(CONFIG_TIMER_0, &params);

    if (timer0 == NULL) {
        /* Failed to initialize timer */
        while (1) {}
    }

    if (Timer_start(timer0) == Timer_STATUS_ERROR) {
        /* Failed to start timer */
        while (1) {}
    }
}

/*
 *  ======== gpioButtonFxn0 ========
 *  Callback function for the GPIO interrupt on CONFIG_GPIO_BUTTON_0.
 *
 *  Note: GPIO interrupts are cleared prior to invoking callbacks.
 */
void gpioButtonFxn0(uint_least8_t index)
{
    /* set change_message = 1 if button pressed
     * at least during the message's cycle */
    if (!button_pressed) {
      next_message_index = next_message_index + 1;
      button_pressed = 1;
    }
}

/*
 *  ======== gpioButtonFxn1 ========
 *  Callback function for the GPIO interrupt on CONFIG_GPIO_BUTTON_1.
 *  This may not be used for all boards.
 *
 *  Note: GPIO interrupts are cleared prior to invoking callbacks.
 */
void gpioButtonFxn1(uint_least8_t index)
{
    /* set change_message = 1 if button pressed
     * at least during the message's cycle */
    if (!button_pressed) {
      next_message_index = next_message_index + num_messages - 1;
      button_pressed = 1;
    }
}

/* --- functions to switch on one or other, or both, or neither of the LEDs --- */
void set_leds(unsigned char led_settings) {

  /* assume both off */
  GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
  GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);

  /* switch red on */
  if (0b01 & led_settings) {
     GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
  }

  /* switch green on */
  if (0b10 & led_settings) {
     GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_ON);
  }
}

/* iterate over message, character by character,
 * convert each character to Morse code, then
 * iterate over the Morse string symbol by symbol
 * and display each in turn, phase by phase, with the
 * intermediate pauses added */
void signal_message()
{
  /* initialize character and string variables */
  char character;
  char symbol;

  /* iterate over characters in message */
  character = messages[message_index][character_index];
  if (character != '\0') {

    /* assume that message is still in progress */
    message_ended = 0;

    /* iterate over symbols in character */
    symbol = get_morse(character)[symbol_index];
    if (symbol != '\0') {

      /* signal current symbol, phase by phase */
      switch (symbol) {
        case '.':
            if (phase < dot_len) {
                signal_dot(phase);
                ++phase;
            }
            else {
                ++symbol_index;
                symbol = get_morse(character)[symbol_index];
                phase = 0;
            }
          break;

        case '-':
          if (phase < dash_len) {
            signal_dash(phase);
            ++phase;
          }
          else {
              ++symbol_index;
              symbol = get_morse(character)[symbol_index];
              phase = 0;
          }
          break;

        /* space will fall through to default case, which will effectively replace unknown characters with a space */
        case ' ':

        default:
          if (phase < word_pause_len) {
            word_pause(phase);
            ++phase;
          }

          /* get next symbol and reset phase to 0 */
          else {
              ++symbol_index;
              symbol = get_morse(character)[symbol_index];
              phase = 0;
          }
          break;
      }
    }

    else {
      /* pause between characters */
      if (phase <= character_pause_len) {
        character_pause(phase);
        ++phase;
      }

      /* get next character and reset symbol_index and phase to 0 */
      else {
        ++character_index;
        character = messages[message_index][character_index];
        symbol_index = 0;
        phase = 0;
      }
    }
  }

  else {
    /* pause between messages */
    if (phase <= word_pause_len) {
      word_pause(phase);
      ++phase;
    }

    /* set message_ended flag to 1 and reset phase, smbol_index and character_index to 0 */
    else {
      message_ended = 1;
      phase = 0;
      symbol_index = 0;
      character_index = 0;
    }
  }
}

/* normalize index to ensure that it is a valid index for the messages array
 * @params next_message_index -> the variable as incremented/decremented by button interrupts
 * @ return -> the message mod num_messages
 */
short unsigned int normalize_message_index(short unsigned int index) {

  return index % num_messages;
}

/* --- functions to signal Morse code symbols (and pauses) ---
 * signal a 'dot' (500ms red LED, 500ms pause)
 * @param phase -> the current step in the dot symbol */
void signal_dot(short int phase) {

    if (phase <= 0) {
      set_leds(1);
    }

    else {
      set_leds(0);
      phase = 0;
    }
}

/* signal a 'dash' (1500ms green LED, 500ms pause)
 * @param phase -> the current step in the dash symbol */
void signal_dash(int phase) {

    if (phase <= 2) {
      set_leds(2);
    }

    else {
      set_leds(0);
      phase = 0;    // reset the phase counter
    }
}

/* pause between dots, dashes, characters and words:
 *    500ms - 500ms = 0ms between dots/dashes,
 *    1500ms - 500ms = 1000ms between characters,
 *    3500ms - 1500ms between words
 * @param phase -> the current step in the pause
 * Inter-character pause */
void character_pause(int phase) {

    if (phase <= 1) {
         set_leds(0);
    }
}

/* Inter-word/message pause */
void word_pause(int phase) {

    if (phase <= 3) {
         set_leds(0);
    }
}

/* Configure the TI board */
void configure_board() {
    /* Call driver init functions */
    GPIO_init();

    /* Configure the LED and button pins */
    GPIO_setConfig(CONFIG_GPIO_LED_0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_LED_1, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    /* Turn all LEDs off to begin with */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);


    /* Install Button callback */
    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, gpioButtonFxn0);

    /* Enable interrupts */
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);

    /*
     *  If more than one input pin is available for your device, interrupts
     *  will be enabled on CONFIG_GPIO_BUTTON1.
     */
    if (CONFIG_GPIO_BUTTON_0 != CONFIG_GPIO_BUTTON_1) {
        /* Configure BUTTON1 pin */
        GPIO_setConfig(CONFIG_GPIO_BUTTON_1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

        /* Install Button callback */
        GPIO_setCallback(CONFIG_GPIO_BUTTON_1, gpioButtonFxn1);

        /* Enable interrupts */
        GPIO_enableInt(CONFIG_GPIO_BUTTON_1);
    }
}

/* This function converts a character to its Morse code equivalent
 *   n.b. each 'symbol' (dot/dash) postpends a dot-length pause, and
 *   each character postpends a dash-length pause; each is then
 *   subtracted from the character- or word-pause when it occurs
 * @param character -> the character to be converted to Morse code
 * @return -> a string containting the Morse code for the character
 * */
const char* get_morse(char character)
{

  switch (character) {
    case 'a':
      return ".-";
    case 'b':
      return "-...";
    case 'c':
      return "-.-.";
    case 'd':
      return "-..";
    case 'e':
      return ".";
    case 'f':
      return "..-.";
    case 'g':
      return "--.";
    case 'h':
      return "....";
    case 'i':
      return "..";
    case 'j':
      return ".---";
    case 'k':
      return "-.-";
    case 'l':
      return ".-..";
    case 'm':
      return "--";
    case 'n':
      return "-.";
    case 'o':
      return "---";
    case 'p':
      return ".--.";
    case 'q':
      return "--.-";
    case 'r':
      return ".-.";
    case 's':
      return "...";
    case 't':
      return "-";
    case 'u':
      return "..-";
    case 'v':
      return "...-";
    case 'w':
      return ".--";
    case 'x':
      return "-..-";
    case 'y':
      return "-.--";
    case 'z':
      return "--..";
    default:
      return " ";
  }
}
