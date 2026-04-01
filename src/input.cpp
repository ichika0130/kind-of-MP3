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

    // ── Clock-wake gate ───────────────────────────────────────────────────────
    // When the device has just woken to the CLOCK page, only the PLAY button
    // (Button 2, GPIO3) takes action: short press sets page = WAKE (triggering
    // the HELLO! animation) and calls audio.resume().  All other trackers are
    // drained so their normal actions do not fire; the 5-second sleep timer is
    // reset by PowerManager via direct GPIO polling.
    if (state.page == DisplayPage::CLOCK) {
        Ev playEv = _play.poll();
        _page.poll(); _prev.poll(); _next.poll();
        _volUp.poll(); _volDown.poll();

        if (playEv == Ev::SHORT_PRESS) {
            state.page = DisplayPage::WAKE;   // triggers HELLO! animation in display
            audio.resume();
            Serial.println("[input] PLAY on CLOCK: WAKE animation + resume");
        }
        return;   // no other actions while in clock-wake mode
    }

    // ── PAGE CYCLE / BLE PAIRING ──────────────────────────────────────────────
    //
    //   On BLE_PAIRING page:
    //     short press        → exit pairing mode, revert to previous page
    //   Otherwise:
    //     short press        → advance NOW_PLAYING → STEPS → BATTERY → (wrap)
    //     long press (3 s)   → enter BLE pairing mode (BLE_PAIRING page)
    //
    //   CLOCK is excluded from the manual cycle; it is entered automatically.
    //   If the current page is outside the cycle, short press snaps to NOW_PLAYING.

    {
        // Static locals track the 3-second long-press accumulator.
        // pageLongArmed is cleared when the button is released before 3 s.
        static unsigned long pageLongStartMs = 0;
        static bool          pageLongArmed   = false;

        Ev pageEv = _page.poll();

        // Cancel arm immediately if the button has been released
        if (pageLongArmed && digitalRead(BTN_PAGE_PIN) == HIGH) {
            pageLongArmed = false;
        }

        if (state.page == DisplayPage::BLE_PAIRING) {
            // ── On BLE_PAIRING page: only short press exits ───────────────────
            if (pageEv == Ev::SHORT_PRESS) {
                state.pairingMode = false;
                state.page        = state.prePairingPage;
                pageLongArmed     = false;
                Serial.println("[input] PAGE short on BLE_PAIRING: exit pairing");
            }
        } else {
            // ── Normal mode ───────────────────────────────────────────────────

            // Arm / accumulate for 3-second long press
            if (pageEv == Ev::LONG_START) {
                pageLongStartMs = millis();
                pageLongArmed   = true;
            }
            // Trigger at 600 ms (LONG_START) + 2400 ms (≥12 LONG_REPEATs @ 200 ms)
            if (pageEv == Ev::LONG_REPEAT && pageLongArmed &&
                millis() - pageLongStartMs >= 2400UL) {
                pageLongArmed         = false;
                state.prePairingPage  = state.page;
                state.pairingMode     = true;
                state.page            = DisplayPage::BLE_PAIRING;
                Serial.println("[input] PAGE long (3 s): enter BLE pairing mode");
            }

            // Short press: cycle through normal pages
            if (pageEv == Ev::SHORT_PRESS) {
                static constexpr DisplayPage CYCLE[] = {
                    DisplayPage::NOW_PLAYING,
                    DisplayPage::STEPS,
                    DisplayPage::BATTERY,
                    DisplayPage::EQ,
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
            }
        }
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
