#include "input.h"

// ─── ButtonTracker::poll ─────────────────────────────────────────────────────
//
// Called every Arduino loop iteration (~no fixed rate).
// Returns at most one Event per call; the state machine advances by at most
// one transition per call so rapid loops never skip state.

ButtonTracker::Event ButtonTracker::poll() {
    // ── 1. Debounce ───────────────────────────────────────────────────────────
    //
    // Track the "candidate" (in-flight value).  Only commit a new stable edge
    // once the candidate has been unchanged for DEBOUNCE_MS.

    bool raw = digitalRead(_pin);

    if (raw != _candidate) {
        // Signal is changing — restart the stability timer
        _candidate = raw;
        _candMs    = millis();
    }

    // Detect a newly committed edge (candidate stable, differs from last stable)
    bool falling = false;
    bool rising  = false;

    if ((millis() - _candMs >= DEBOUNCE_MS) && (_candidate != _stable)) {
        falling = (_stable == HIGH && _candidate == LOW);
        rising  = (_stable == LOW  && _candidate == HIGH);
        _stable = _candidate;
    }

    // ── 2. State machine ──────────────────────────────────────────────────────

    switch (_st) {

        // ── IDLE: waiting for a press ─────────────────────────────────────────
        case St::IDLE:
            if (falling) {
                _st      = St::PRESSED;
                _pressMs = millis();
            }
            return Event::NONE;

        // ── PRESSED: button is down, waiting for release or long-press mark ──
        case St::PRESSED:
            if (rising) {
                // Released before threshold → short press
                _st = St::IDLE;
                return Event::SHORT_PRESS;
            }
            if (millis() - _pressMs >= LONGPRESS_MS) {
                // Held long enough → long press start
                _st       = St::LONG_HELD;
                _repeatMs = millis();
                return Event::LONG_START;
            }
            return Event::NONE;

        // ── LONG_HELD: long-press already fired, button still down ────────────
        case St::LONG_HELD:
            if (rising) {
                // Release after long press — no additional event
                _st = St::IDLE;
                return Event::NONE;
            }
            if (millis() - _repeatMs >= REPEAT_MS) {
                _repeatMs = millis();
                return Event::LONG_REPEAT;
            }
            return Event::NONE;
    }

    return Event::NONE;  // unreachable, satisfies compiler
}

// ─── InputManager ─────────────────────────────────────────────────────────────

InputManager::InputManager()
    : _page   (BTN_PAGE_PIN),
      _play   (BTN_PLAY_PIN),
      _prev   (BTN_PREV_PIN),
      _next   (BTN_NEXT_PIN),
      _volUp  (BTN_VOL_UP_PIN),
      _volDown(BTN_VOL_DOWN_PIN)
{}

void InputManager::begin() {
    _page.begin();
    _play.begin();
    _prev.begin();
    _next.begin();
    _volUp.begin();
    _volDown.begin();
}

void InputManager::update(AudioManager& audio, DisplayState& state) {
    using Ev = ButtonTracker::Event;

    // ── PAGE CYCLE ────────────────────────────────────────────────────────────
    //   short → advance through NOW_PLAYING → STEPS → BATTERY → (wrap)
    //   CLOCK is excluded from the manual cycle; it is entered automatically.
    //   If the current page is outside the cycle (e.g. CLOCK, DARK_HOUR, WAKE),
    //   the press snaps to NOW_PLAYING.

    switch (_page.poll()) {
        case Ev::SHORT_PRESS: {
            static constexpr DisplayPage CYCLE[] = {
                DisplayPage::NOW_PLAYING,
                DisplayPage::STEPS,
                DisplayPage::BATTERY,
            };
            constexpr uint8_t CYCLE_LEN = sizeof(CYCLE) / sizeof(CYCLE[0]);

            uint8_t next = 0;   // default: go to NOW_PLAYING if not found in cycle
            for (uint8_t i = 0; i < CYCLE_LEN; i++) {
                if (state.page == CYCLE[i]) {
                    next = (i + 1) % CYCLE_LEN;
                    break;
                }
            }
            state.page = CYCLE[next];
            Serial.printf("[input] PAGE short: page → %d\n", (int)state.page);
            break;
        }
        default: break;
    }

    // ── PLAY / PAUSE ──────────────────────────────────────────────────────────
    //   short → toggle play/pause
    //   long  → reserved

    switch (_play.poll()) {
        case Ev::SHORT_PRESS:
            if (state.isPlaying) {
                audio.pause();
            } else {
                audio.resume();
            }
            break;
        default: break;
    }

    // ── PREVIOUS ──────────────────────────────────────────────────────────────
    //   short → previous()          (AudioManager handles the ">3 s = restart" rule)
    //   long  → force-restart       (always go to beginning of current track)

    switch (_prev.poll()) {
        case Ev::SHORT_PRESS:
            audio.previous();
            break;
        case Ev::LONG_START:
            // play(currentIndex) re-opens the file from byte 0
            audio.play(audio.getCurrentIndex());
            Serial.println("[input] PREV long: restart track");
            break;
        default: break;
    }

    // ── NEXT ──────────────────────────────────────────────────────────────────
    //   short → next()
    //   long  → cycle play mode (SEQUENTIAL → SHUFFLE → REPEAT_ONE → …)

    switch (_next.poll()) {
        case Ev::SHORT_PRESS:
            audio.next();
            break;
        case Ev::LONG_START: {
            PlayMode next = static_cast<PlayMode>(
                ((uint8_t)audio.getPlayMode() + 1) % 3);
            audio.setPlayMode(next);
            state.playMode = next;   // immediate feedback before next audio.update()
            Serial.printf("[input] NEXT long: play mode → %d\n", (int)next);
            break;
        }
        default: break;
    }

    // ── VOL+ ──────────────────────────────────────────────────────────────────
    //   short      → +1 step on release
    //   long start → +1 step immediately at 600 ms mark
    //   long repeat→ +1 step every 200 ms while held
    //
    // All three cases do the same thing — C++ fall-through between empty cases
    // is intentional and well-defined.

    switch (_volUp.poll()) {
        case Ev::SHORT_PRESS:
        case Ev::LONG_START:
        case Ev::LONG_REPEAT:
            audio.setVolume(audio.getVolume() + 1);
            break;
        default: break;
    }

    // ── VOL- ──────────────────────────────────────────────────────────────────

    switch (_volDown.poll()) {
        case Ev::SHORT_PRESS:
        case Ev::LONG_START:
        case Ev::LONG_REPEAT: {
            uint8_t v = audio.getVolume();
            if (v > 0) audio.setVolume(v - 1);
            break;
        }
        default: break;
    }
}
