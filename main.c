/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);

//--------------------------------------------------------------------+
// HELPER FUNCTIONS
//--------------------------------------------------------------------+

/**
 * @brief Sends a keyboard report if the device is ready.
 *
 * @param report_id The report ID (e.g., REPORT_ID_KEYBOARD).
 * @param modifier  Modifier keys (e.g., KEYBOARD_MODIFIER_LEFTSHIFT).
 * @param keycode   Array of 6 keycodes.
 * @return true if report sent, false otherwise (not mounted/ready).
 */
bool send_keyboard_report(uint8_t report_id, uint8_t modifier,
                          uint8_t keycode[6]) {
  // Skip if hid is not ready yet
  if (!tud_hid_ready())
    return false;

  return tud_hid_keyboard_report(report_id, modifier, keycode);
}

/**
 * @brief Sends a single key press.
 *        Note: This sends the state where the key IS pressed.
 *        You must send a key release report afterwards to "release" the key.
 */
bool send_key_press(uint8_t modifier, uint8_t key_code) {
  uint8_t keycode[6] = {0};
  keycode[0] = key_code;
  return send_keyboard_report(REPORT_ID_KEYBOARD, modifier, keycode);
}

/**
 * @brief Sends an empty keyboard report to release all keys.
 */
bool send_key_release(void) {
  return tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
}

/**
 * @brief Sends a mouse report if the device is ready.
 */
bool send_mouse_move(int8_t x, int8_t y) {
  // Skip if hid is not ready yet
  if (!tud_hid_ready())
    return false;

  return tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, x, y, 0, 0);
}

bool send_mouse_click(uint8_t buttons) {
  if (!tud_hid_ready())
    return false;
  return tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, 0, 0, 0, 0);
}

bool send_mouse_release(void) {
  if (!tud_hid_ready())
    return false;
  return tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, 0, 0, 0, 0);
}

/*------------- MAIN -------------*/
int main(void) {
  board_init();

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  while (1) {
    tud_task(); // tinyusb device task
    led_blinking_task();

    hid_task();
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) { blink_interval_ms = BLINK_MOUNTED; }

// Invoked when device is unmounted
void tud_umount_cb(void) { blink_interval_ms = BLINK_NOT_MOUNTED; }

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// HID Demo State Machine
//--------------------------------------------------------------------+

typedef enum {
  STATE_IDLE,
  STATE_WAIT_INIT,
  STATE_MOUSE_UP,
  STATE_MOUSE_DOWN,
  STATE_CLICK_PRESS,
  STATE_CLICK_RELEASE,
  STATE_WAIT_BEFORE_TYPE,
  STATE_TYPE_CHAR,
  STATE_RELEASE_CHAR,
  STATE_DONE
} app_state_t;

static app_state_t app_state = STATE_IDLE;
static uint32_t state_start_ms = 0;

// The text to type
static const char *text_to_type = "Hello World!";
static int text_index = 0;

void hid_task(void) {
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if (board_millis() - start_ms < interval_ms)
    return; // not enough time
  start_ms += interval_ms;

  if (!tud_mounted()) {
    app_state = STATE_IDLE;
    return;
  }

  // Remote wakeup
  if (tud_suspended()) {
    tud_remote_wakeup();
    return;
  }

  // Skip if hid is not ready yet
  if (!tud_hid_ready())
    return;

  switch (app_state) {
  case STATE_IDLE:
    // Start the sequence
    state_start_ms = board_millis();
    app_state = STATE_WAIT_INIT;
    break;

  case STATE_WAIT_INIT:
    if (board_millis() - state_start_ms > 2000) { // Wait 2 seconds
      app_state = STATE_WAIT_BEFORE_TYPE;
    }
    break;

  case STATE_MOUSE_UP:
    if (send_mouse_move(0, -20)) {
      app_state = STATE_MOUSE_DOWN;
    }
    break;

  case STATE_MOUSE_DOWN:
    if (send_mouse_move(0, 20)) {
      app_state = STATE_CLICK_PRESS;
    }
    break;

  case STATE_CLICK_PRESS:
    if (send_mouse_click(MOUSE_BUTTON_LEFT)) {
      app_state = STATE_CLICK_RELEASE;
    }
    break;

  case STATE_CLICK_RELEASE:
    if (send_mouse_release()) {
      state_start_ms = board_millis();
      app_state = STATE_WAIT_BEFORE_TYPE;
    }
    break;

  case STATE_WAIT_BEFORE_TYPE:
    if (board_millis() - state_start_ms > 500) {
      text_index = 0;
      app_state = STATE_TYPE_CHAR;
    }
    break;

  case STATE_TYPE_CHAR: {
    char c = text_to_type[text_index];
    if (c == 0) {
      app_state = STATE_DONE;
      break;
    }

    uint8_t key = 0;
    uint8_t modifier = 0;

    if (c >= 'a' && c <= 'z') {
      key = HID_KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      key = HID_KEY_A + (c - 'A');
      modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (c == ' ') {
      key = HID_KEY_SPACE;
    } else if (c == '!') {
      key = HID_KEY_1;
      modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    }

    if (send_key_press(modifier, key)) {
      app_state = STATE_RELEASE_CHAR;
    }
  } break;

  case STATE_RELEASE_CHAR:
    if (send_key_release()) {
      text_index++;
      app_state = STATE_TYPE_CHAR;
    }
    break;

  case STATE_DONE:
    // Do nothing
    break;
  }
}

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint16_t len) {
  (void)instance;
  (void)len;
  (void)report;
}

// Invoked when received GET_REPORT control request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT) {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD) {
      // bufsize should be (at least) 1
      if (bufsize < 1)
        return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      } else {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms)
    return;

  // Blink every interval ms
  if (board_millis() - start_ms < blink_interval_ms)
    return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
