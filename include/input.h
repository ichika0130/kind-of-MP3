#pragma once

#include <Arduino.h>
#include "audio.h"    // AudioManager, PlayMode
#include "display.h"  // DisplayState, DisplayPage

// ─── Button pin constants ─────────────────────────────────────────────────────
//
//  Button 1  GPIO 7   page cycle (GPIO7 chosen: free, non-strapping, not forbidden)
//  Button 2  GPIO 3   play / pause
//  Button 3  GPIO 2   previous track / restart  (GPIO1↔2 swapped vs. original wiring)
//  Button 4  GPIO 1   next track / play-mode cycle
//  Button 5  GPIO14   volume up
//  Button 6  GPIO15   volume down

constexpr uint8_t BTN_PAGE_PIN     =  7;   // Button 1 — page cycle (new)
constexpr uint8_t BTN_PLAY_PIN     =  3;   // Button 2 — play / pause
constexpr uint8_t BTN_PREV_PIN     =  2;   // Button 3 — previous / restart
constexpr uint8_t BTN_NEXT_PIN     =  1;   // Button 4 — next / play-mode cycle
constexpr uint8_t BTN_VOL_UP_PIN   = 14;   // Button 5 — volume up
constexpr uint8_t BTN_VOL_DOWN_PIN = 15;   // Button 6 — volume down

// ─── ButtonTracker ────────────────────────────────────────────────────────────
//
// Per-button non-blocking state machine.
//
// State transitions (all buttons are active-LOW / INPUT_PULLUP):
//
//   IDLE  ──falling edge──►  PRESSED  ──rising edge before threshold──►  IDLE  (SHORT_PRESS)
//                                │
//                         held ≥ 600 ms
//                                │
//                                ▼
//                           LONG_HELD  ──► fires LONG_START once, then
//                                          LONG_REPEAT every 200 ms while held
//                                          rising edge → IDLE (no extra event)

class ButtonTracker {
public:
    enum class Event : uint8_t {
        NONE,
        SHORT_PRESS,   // fired on release if hold was < LONGPRESS_MS
        LONG_START,    // fired once at the LONGPRESS_MS mark
        LONG_REPEAT    // fired every REPEAT_MS thereafter while still held
    };

    explicit ButtonTracker(uint8_t pin) : _pin(pin) {}

    void  begin() { pinMode(_pin, INPUT_PULLUP); }
    Event poll();

private:
    uint8_t _pin;

    // ── Debounce ──────────────────────────────────────────────────────────────
    // _candidate tracks the in-flight (possibly bouncing) signal.
    // _stable    is the last edge that survived DEBOUNCE_MS without change.
    bool          _candidate = HIGH;
    bool          _stable    = HIGH;
    unsigned long _candMs    = 0;     // millis() when _candidate last changed

    // ── Press state machine ───────────────────────────────────────────────────
    enum class St : uint8_t { IDLE, PRESSED, LONG_HELD } _st = St::IDLE;
    unsigned long _pressMs  = 0;   // millis() when press was confirmed stable
    unsigned long _repeatMs = 0;   // millis() of last LONG_START or LONG_REPEAT

    // ── Timing constants ─────────────────────────────────────────────────────
    static constexpr uint16_t DEBOUNCE_MS  =  50;
    static constexpr uint16_t LONGPRESS_MS = 600;
    static constexpr uint16_t REPEAT_MS    = 200;
};

// ─── InputManager ─────────────────────────────────────────────────────────────
//
// Owns all six ButtonTrackers and maps their events to AudioManager/DisplayState
// calls.  Call begin() once in setup(), update() every loop iteration.
//
// Button actions summary:
//
//   PAGE      short → cycle page (NOW_PLAYING→STEPS→BATTERY)   long → (reserved)
//   PLAY/PAUSE short → toggle pause/resume                      long → (reserved)
//   PREV      short → audio.previous()          long → restart current track
//   NEXT      short → audio.next()              long → cycle play mode
//   VOL+      short → vol +1                    long → vol +1 every 200 ms
//   VOL-      short → vol -1                    long → vol -1 every 200 ms
//
//   CLOCK is not included in the manual page cycle; it is shown automatically.

class InputManager {
public:
    InputManager();

    void begin();
    void update(AudioManager& audio, DisplayState& state);

private:
    ButtonTracker _page;
    ButtonTracker _play;
    ButtonTracker _prev;
    ButtonTracker _next;
    ButtonTracker _volUp;
    ButtonTracker _volDown;
};
