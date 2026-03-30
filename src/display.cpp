#include "display.h"

// ─── Layout constants (all in pixels, 128×32 screen) ─────────────────────────
//
// Tip: draw every page on paper first — 32 rows goes fast.
//
//  size-1 font: 6×8  px per glyph
//  size-2 font: 12×16 px per glyph

namespace Layout {
    // NOW_PLAYING
    constexpr int16_t NP_TITLE_Y    =  0;   // title text baseline row
    constexpr int16_t NP_INFO_Y     = 10;   // play-icon + time row
    constexpr int16_t NP_BAR_Y      = 22;   // progress bar top
    constexpr int16_t NP_BAR_H      =  8;   // progress bar height
    constexpr int16_t NP_ICON_X     =  0;
    constexpr int16_t NP_TIME_X     = 10;   // time string starts after icon

    // CLOCK
    constexpr int16_t CLK_TIME_Y    =  0;   // HH:MM, size-2
    constexpr int16_t CLK_SEC_Y     =  8;   // :SS, size-1 (overlaid right of time)
    constexpr int16_t CLK_DATE_Y    = 24;   // date string, size-1

    // STEPS
    constexpr int16_t STP_COUNT_Y   =  0;   // large step count, size-2
    constexpr int16_t STP_LABEL_Y   =  0;   // "STEPS" label, size-1 (right-aligned)
    constexpr int16_t STP_CAL_Y     = 24;   // calories, size-1

    // BATTERY
    constexpr int16_t BAT_PCT_Y     =  0;   // percentage, size-2
    constexpr int16_t BAT_BAR_X     = 10;
    constexpr int16_t BAT_BAR_Y     = 20;
    constexpr int16_t BAT_BAR_W     = 104;
    constexpr int16_t BAT_BAR_H     = 10;
    constexpr int16_t BAT_NUB_W     =  4;
    constexpr int16_t BAT_NUB_H     =  4;
}

// ─── Constructor / lifecycle ──────────────────────────────────────────────────

DisplayManager::DisplayManager()
    : _disp(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RESET) {}

bool DisplayManager::begin() {
    Wire.begin(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);

    if (!_disp.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR)) {
        return false;   // caller should halt or retry
    }

    _disp.setTextWrap(false);   // critical: prevent GFX from wrapping scrolling text
    _disp.setTextColor(SSD1306_WHITE);
    _disp.clearDisplay();
    _disp.display();
    return true;
}

void DisplayManager::update(const DisplayState& state) {
    _disp.clearDisplay();

    switch (state.page) {
        case DisplayPage::NOW_PLAYING: drawNowPlaying(state); break;
        case DisplayPage::CLOCK:       drawClock(state);      break;
        case DisplayPage::STEPS:       drawSteps(state);      break;
        case DisplayPage::BATTERY:     drawBattery(state);    break;
        default: break;
    }

    _disp.display();
}

void DisplayManager::setContrast(uint8_t contrast) {
    // dim() only gives two levels; issuing raw SSD1306 commands gives the
    // full 0–255 range needed for volume-linked brightness.
    _disp.dim(false);                         // take display out of auto-dim first
    _disp.ssd1306_command(SSD1306_SETCONTRAST);
    _disp.ssd1306_command(contrast);
}

void DisplayManager::sleep() { _disp.ssd1306_command(SSD1306_DISPLAYOFF); }
void DisplayManager::wake()  { _disp.ssd1306_command(SSD1306_DISPLAYON);  }

// ─── NOW PLAYING ──────────────────────────────────────────────────────────────
//
//  ┌────────────────────────────────┐
//  │ Mass Destruction -fatima ver- ►│  ← scrolling title  (y=0)
//  │ ▶  01:23 / 03:34               │  ← icon + time      (y=10)
//  │ ████████████░░░░░░░░░░░░░░░░░░ │  ← progress bar     (y=22)
//  └────────────────────────────────┘

void DisplayManager::drawNowPlaying(const DisplayState& s) {
    // ── 1. Scrolling title ───────────────────────────────────────────────────

    const char* title  = s.songTitle;
    int16_t     titleW = textWidth(title, 1);   // total pixel width of title string

    // Reset scroll state when the track changes
    if (strcmp(s.songTitle, _lastTitle) != 0) {
        strncpy(_lastTitle, s.songTitle, sizeof(_lastTitle) - 1);
        _lastTitle[sizeof(_lastTitle) - 1] = '\0';
        _scrollX         = 0;
        _scrollHoldTicks = SCROLL_HOLD_TICKS;
        _lastScrollTick  = millis();
    }

    if (titleW <= DISPLAY_WIDTH) {
        // Fits on screen — static, left-aligned
        _disp.setTextSize(1);
        _disp.setCursor(0, Layout::NP_TITLE_Y);
        _disp.print(title);
    } else {
        // Animate: slide left one pixel per SCROLL_TICK_MS ms
        unsigned long now = millis();
        if (now - _lastScrollTick >= SCROLL_TICK_MS) {
            _lastScrollTick = now;
            if (_scrollHoldTicks > 0) {
                --_scrollHoldTicks;
            } else {
                --_scrollX;
                // Gap between end of text and restart = half screen width
                if (_scrollX < -(titleW + DISPLAY_WIDTH / 2)) {
                    _scrollX         = 0;
                    _scrollHoldTicks = SCROLL_HOLD_TICKS;
                }
            }
        }
        _disp.setTextSize(1);
        _disp.setCursor(_scrollX, Layout::NP_TITLE_Y);
        _disp.print(title);
    }

    // ── 2. Play / pause icon ─────────────────────────────────────────────────

    if (s.isPlaying) {
        // Filled right-pointing triangle ▶ (7×8 px)
        for (int i = 0; i < 4; i++) {
            _disp.drawFastVLine(Layout::NP_ICON_X + i,
                                Layout::NP_INFO_Y + i,
                                8 - (2 * i), SSD1306_WHITE);
        }
    } else {
        // Two vertical bars ‖ (2+2 px wide, gap of 2 px)
        _disp.fillRect(Layout::NP_ICON_X,     Layout::NP_INFO_Y, 2, 8, SSD1306_WHITE);
        _disp.fillRect(Layout::NP_ICON_X + 4, Layout::NP_INFO_Y, 2, 8, SSD1306_WHITE);
    }

    // ── 3. Time string ───────────────────────────────────────────────────────

    char timeBuf[14];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu/%02lu:%02lu",
             (unsigned long)(s.songPositionSec / 60),
             (unsigned long)(s.songPositionSec % 60),
             (unsigned long)(s.songDurationSec / 60),
             (unsigned long)(s.songDurationSec % 60));

    _disp.setTextSize(1);
    _disp.setCursor(Layout::NP_TIME_X, Layout::NP_INFO_Y);
    _disp.print(timeBuf);

    // ── 4. Progress bar ──────────────────────────────────────────────────────

    // Outer border
    _disp.drawRect(0, Layout::NP_BAR_Y, DISPLAY_WIDTH, Layout::NP_BAR_H, SSD1306_WHITE);

    // Filled portion
    if (s.songDurationSec > 0) {
        int16_t fillW = (int16_t)(
            (float)s.songPositionSec / (float)s.songDurationSec
            * (DISPLAY_WIDTH - 2));
        if (fillW > 0) {
            _disp.fillRect(1, Layout::NP_BAR_Y + 1,
                           fillW, Layout::NP_BAR_H - 2, SSD1306_WHITE);
        }
    }
}

// ─── CLOCK ────────────────────────────────────────────────────────────────────
//
//  ┌────────────────────────────────┐
//  │      12:34                     │  ← HH:MM size-2, centered  (y=0)
//  │           :56                  │  ← :SS size-1, right of time (y=8)
//  │      MON 2009/09/14            │  ← date, size-1, centered   (y=24)
//  └────────────────────────────────┘

void DisplayManager::drawClock(const DisplayState& s) {
    // ── HH:MM in large font ──────────────────────────────────────────────────

    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", s.hour, s.minute);

    _disp.setTextSize(2);
    int16_t  tx, ty;
    uint16_t tw, th;
    _disp.getTextBounds(hmBuf, 0, 0, &tx, &ty, &tw, &th);

    int16_t hmX = (DISPLAY_WIDTH - (int16_t)tw) / 2;
    _disp.setCursor(hmX, Layout::CLK_TIME_Y);
    _disp.print(hmBuf);

    // ── :SS in small font, vertically centred next to the large digits ───────

    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), ":%02d", s.second);

    _disp.setTextSize(1);
    _disp.setCursor(hmX + (int16_t)tw + 1, Layout::CLK_SEC_Y);
    _disp.print(secBuf);

    // ── Date string, centred ─────────────────────────────────────────────────

    uint16_t dw, dh;
    _disp.getTextBounds(s.dateStr.c_str(), 0, 0, &tx, &ty, &dw, &dh);
    _disp.setCursor((DISPLAY_WIDTH - (int16_t)dw) / 2, Layout::CLK_DATE_Y);
    _disp.print(s.dateStr);
}

// ─── STEPS ────────────────────────────────────────────────────────────────────
//
//  ┌────────────────────────────────┐
//  │ 7,832             STEPS        │  ← count size-2 left, label right (y=0)
//  │                                │
//  │                                │
//  │                  231.5 kcal    │  ← calories, size-1, right-aligned (y=24)
//  └────────────────────────────────┘

void DisplayManager::drawSteps(const DisplayState& s) {
    // ── Step count in large font (left side) ─────────────────────────────────

    char stepBuf[10];
    formatSteps(stepBuf, sizeof(stepBuf), s.stepCount);

    _disp.setTextSize(2);
    _disp.setCursor(0, Layout::STP_COUNT_Y);
    _disp.print(stepBuf);

    // ── "STEPS" label in small font (right-aligned, same row) ────────────────

    int16_t  tx, ty;
    uint16_t lw, lh;
    _disp.setTextSize(1);
    _disp.getTextBounds("STEPS", 0, 0, &tx, &ty, &lw, &lh);
    _disp.setCursor(DISPLAY_WIDTH - (int16_t)lw, Layout::STP_LABEL_Y);
    _disp.print("STEPS");

    // ── Calories, right-aligned at bottom ────────────────────────────────────

    char calBuf[16];
    snprintf(calBuf, sizeof(calBuf), "%.1f kcal", s.caloriesBurned);

    uint16_t cw, ch;
    _disp.getTextBounds(calBuf, 0, 0, &tx, &ty, &cw, &ch);
    _disp.setCursor(DISPLAY_WIDTH - (int16_t)cw, Layout::STP_CAL_Y);
    _disp.print(calBuf);
}

// ─── BATTERY ─────────────────────────────────────────────────────────────────
//
//  ┌────────────────────────────────┐
//  │          72%                   │  ← percentage size-2, centred  (y=0)
//  │                                │
//  │  ▕████████████████▏▌           │  ← battery bar (y=20)
//  └────────────────────────────────┘
//   BAT_BAR_X=10              +nub

void DisplayManager::drawBattery(const DisplayState& s) {
    // ── Percentage number, centred ────────────────────────────────────────────

    char pctBuf[6];
    // Show a "+" prefix when charging
    if (s.isCharging) {
        snprintf(pctBuf, sizeof(pctBuf), "+%d%%", s.batteryPct);
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", s.batteryPct);
    }

    _disp.setTextSize(2);
    int16_t  tx, ty;
    uint16_t pw, ph;
    _disp.getTextBounds(pctBuf, 0, 0, &tx, &ty, &pw, &ph);
    _disp.setCursor((DISPLAY_WIDTH - (int16_t)pw) / 2, Layout::BAT_PCT_Y);
    _disp.print(pctBuf);

    // ── Battery bar outline ───────────────────────────────────────────────────

    using namespace Layout;
    _disp.drawRect(BAT_BAR_X, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, SSD1306_WHITE);

    // Positive terminal nub on the right
    _disp.fillRect(BAT_BAR_X + BAT_BAR_W,
                   BAT_BAR_Y + (BAT_BAR_H - BAT_NUB_H) / 2,
                   BAT_NUB_W, BAT_NUB_H, SSD1306_WHITE);

    // ── Fill proportional to percentage ──────────────────────────────────────

    int16_t fillW = (int16_t)((float)s.batteryPct / 100.0f * (BAT_BAR_W - 2));
    if (fillW > 0) {
        _disp.fillRect(BAT_BAR_X + 1, BAT_BAR_Y + 1,
                       fillW, BAT_BAR_H - 2, SSD1306_WHITE);
    }

    // Invert the fill when low (≤20%) so the bar reads as "almost empty"
    // (optional visual affordance — remove if not desired)
    if (s.batteryPct <= 20 && !s.isCharging) {
        _disp.drawRect(BAT_BAR_X, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, SSD1306_WHITE);
        // Re-draw outline after fillRect may have eaten the border pixel
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Format a step count with comma thousands separator, e.g. 7832 → "7,832"
void DisplayManager::formatSteps(char* buf, size_t len, uint32_t steps) {
    if (steps < 1000) {
        snprintf(buf, len, "%lu", (unsigned long)steps);
    } else if (steps < 1000000) {
        snprintf(buf, len, "%lu,%03lu",
                 (unsigned long)(steps / 1000),
                 (unsigned long)(steps % 1000));
    } else {
        snprintf(buf, len, "%lu,%03lu,%03lu",
                 (unsigned long)(steps / 1000000),
                 (unsigned long)((steps / 1000) % 1000),
                 (unsigned long)(steps % 1000));
    }
}

// Return pixel width of str rendered at the given text size
int16_t DisplayManager::textWidth(const char* str, uint8_t size) {
    return (int16_t)(strlen(str) * 6 * size);
}
