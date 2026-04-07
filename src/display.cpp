#include "display.h"

// ─── Vertical-layout constants (rotation 1 → effective 32 × 128) ─────────────
//
//  size-1 font: 6×8 px per glyph   size-2 font: 12×16 px per glyph
//  Screen width (short axis)  = 32 px
//  Screen height (long axis)  = 128 px
//
//  Populated when page renderers are redesigned.

namespace VLayout {
    // Effective logical dimensions after setRotation(1): physical 128×32 → logical 32×128
    constexpr int16_t W = 32;
    constexpr int16_t H = 128;

    // ── NOW_PLAYING ──────────────────────────────────────────────────────────────
    // Battery icon — top-right corner, persistent (outline + nub + fill)
    constexpr int16_t NP_BATT_X     = 23;   // outline left edge
    constexpr int16_t NP_BATT_Y     =  0;   // outline top edge
    constexpr int16_t NP_BATT_W     =  7;   // outline width (excludes nub)
    constexpr int16_t NP_BATT_H     =  4;   // outline height
    constexpr int16_t NP_BATT_NUB_W =  2;   // positive-terminal nub width
    constexpr int16_t NP_BATT_NUB_H =  2;   // positive-terminal nub height

    // Row 1 — ▶/❚❚ icon + remaining time (starts below battery outline)
    constexpr int16_t NP_ROW1_Y     =  6;   // top of row 1 (clears NP_BATT_H gap)
    constexpr int16_t NP_ICON_X     =  0;   // play/pause icon left anchor

    // Title — centred vertically in the main canvas
    constexpr int16_t NP_TITLE_Y    = 58;   // top of scrolling title glyph

    // Playmode label — just below title
    constexpr int16_t NP_MODE_Y     = 70;

    // Volume blocks — bottom row
    constexpr int16_t NP_VOL_Y      = 118;  // top of blocks
    constexpr int16_t NP_VOL_BW     =  3;   // block pixel width
    constexpr int16_t NP_VOL_BH     =  6;   // block pixel height
    constexpr int16_t NP_VOL_GAP    =  1;   // gap between blocks
    constexpr int8_t  NP_VOL_TOTAL  =  7;   // total blocks (volume 0–21 → 0–7 filled)
    // Centre all blocks on W: startX = (32 - (7*3 + 6*1)) / 2 = (32-27)/2 = 2
    constexpr int16_t NP_VOL_STARTX =
        (W - (NP_VOL_TOTAL * NP_VOL_BW + (NP_VOL_TOTAL - 1) * NP_VOL_GAP)) / 2;

    // ── CLOCK (32×128) ──────────────────────────────────────────────────────────
    // HH and MM are drawn on separate rows — "HH:MM" at size-2 is 60 px, wider than W.
    constexpr int16_t CLK_BATT_Y  =  0;   // battery %, size-1, right-aligned
    constexpr int16_t CLK_HH_Y    = 42;   // hour digits, size-2, centred
    constexpr int16_t CLK_MM_Y    = 60;   // minute digits, size-2, centred (2 px gap)
    constexpr int16_t CLK_DAY_Y   = 88;   // day-of-week token (e.g. "MON"), size-1, centred
    constexpr int16_t CLK_DATE_Y  = 98;   // date token (e.g. "03/31"), size-1, centred

    // ── STEPS (32×128) ──────────────────────────────────────────────────────────
    constexpr int16_t STP_BATT_Y  =  0;   // battery %, size-1, right-aligned
    constexpr int16_t STP_COUNT_Y = 48;   // step count, size-2, centred
    constexpr int16_t STP_CAL_Y   = 72;   // calorie count, size-1, centred

    // ── BATTERY page (32×128) ───────────────────────────────────────────────────
    // Vertical bar: nub (positive terminal) sits above the outline.
    constexpr int16_t BAT_PCT_Y   = 30;   // percentage, size-2, centred
    constexpr int16_t BAT_BAR_W   = 20;   // outline width
    constexpr int16_t BAT_BAR_H   = 58;   // outline height
    constexpr int16_t BAT_BAR_X   = (W - BAT_BAR_W) / 2;  // centred → 6
    constexpr int16_t BAT_BAR_Y   = 58;   // outline top
    constexpr int16_t BAT_NUB_W   =  8;   // positive-terminal nub width
    constexpr int16_t BAT_NUB_H   =  3;   // positive-terminal nub height

    // ── WAKE animation (rotation 0 → physical 128×32) ──────────────────────────
    // "HELLO!" centred on the landscape screen.
    constexpr int16_t  WAKE_CHAR_W  = 12;                                          // size-2 glyph width
    constexpr int16_t  WAKE_TEXT_N  =  6;                                          // chars in "HELLO!"
    constexpr int16_t  WAKE_X       = (DISPLAY_WIDTH  - WAKE_TEXT_N * WAKE_CHAR_W) / 2;  // 28
    constexpr int16_t  WAKE_Y       = (DISPLAY_HEIGHT - 16) / 2;                   // 8

    // ── Animation timing ────────────────────────────────────────────────────────
    constexpr uint16_t WAKE_CHAR_MS = 150;   // ms between character reveals
    constexpr uint16_t WAKE_HOLD_MS = 500;   // ms to hold full "HELLO!" before transitioning
    constexpr uint16_t SLIDE_MS     = 200;   // ms for track-change slide-out
    constexpr uint16_t PAGE_SLIDE_MS = 200;   // ms for page slide-in (right → left)

    // DARK_HOUR — populated in next step
}

// ─── Legacy layout constants (128×32 horizontal, rotation 0) ─────────────────
//
//  size-1 font: 6×8  px per glyph
//  size-2 font: 12×16 px per glyph

namespace Layout {
    // DARK_HOUR (still uses legacy 128×32 coordinates — rewritten in next step)
    constexpr int16_t DH_TIME_Y     =  2;   // HH:MM, size-2, centred
    constexpr int16_t DH_SEC_OFFSET =  8;   // :SS size-1, y offset from DH_TIME_Y
    constexpr int16_t DH_LABEL_Y    = 24;   // "-- DARK HOUR --", size-1, centred
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

void DisplayManager::update(DisplayState& state) {
    // ── Rotation: WAKE is horizontal (0), everything else is vertical (1) ────
    int targetRotation = (state.page == DisplayPage::WAKE) ? 0 : 1;
    if (targetRotation != _currentRotation) {
        _currentRotation = targetRotation;
        _disp.setRotation(_currentRotation);
        _disp.clearDisplay();   // clear after rotation change before drawing
    }

    _disp.clearDisplay();

    // ── Page slide-in animation (right → left, PAGE_SLIDE_MS) ─────────────────
    // Fires on any page transition except: to/from WAKE (uses rotation-0),
    // and to DARK_HOUR (has its own scan-line entrance animation).
    if (state.page != _prevPage
        && state.page  != DisplayPage::WAKE
        && state.page  != DisplayPage::DARK_HOUR
        && _prevPage   != DisplayPage::WAKE) {
        _pageSlidingIn    = true;
        _pageSlideStartMs = millis();
        _pageSlideX       = VLayout::W;
    }
    if (_pageSlidingIn) {
        unsigned long slideElapsed = millis() - _pageSlideStartMs;
        if (slideElapsed >= VLayout::PAGE_SLIDE_MS) {
            _pageSlidingIn = false;
            _pageSlideX    = 0;
        } else {
            _pageSlideX = (int16_t)(VLayout::W -
                (int16_t)((float)slideElapsed / VLayout::PAGE_SLIDE_MS * VLayout::W));
        }
    }

    // ── Reset WAKE animation state whenever the page is freshly entered ───────
    if (state.page == DisplayPage::WAKE && _prevPage != DisplayPage::WAKE) {
        _wakeAnimState = 0;
        _wakeCharIndex = 0;
        _wakeAnimTick  = 0;
    }
    _prevPage = state.page;

    // Reset Dark Hour animation timestamp when leaving that page so it replays
    // correctly if the device sees midnight again (or wakes from deep sleep).
    if (state.page != DisplayPage::DARK_HOUR && _darkHourEnteredMs != 0) {
        _darkHourEnteredMs = 0;
    }

    switch (state.page) {
        case DisplayPage::NOW_PLAYING: drawNowPlaying(state); break;
        case DisplayPage::CLOCK:       drawClock(state);      break;
        case DisplayPage::STEPS:       drawSteps(state);      break;
        case DisplayPage::BATTERY:     drawBattery(state);    break;
        case DisplayPage::DARK_HOUR:   drawDarkHour(state);   break;
        case DisplayPage::WAKE:        drawWake(state);       break;
        case DisplayPage::EQ:          drawEQ(state);        break;
        case DisplayPage::BLE_PAIRING: drawBtPairing(state); break;
        case DisplayPage::USB_MSC:     drawUsbMsc(state);    break;
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
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0                          x=31
//  ┌──────────────────────[BAT]┐  y=0   battery outline 7×4 + nub
//  │ ▶  1:43                   │  y=6   play/pause icon (left) + remaining time (right)
//  │                           │
//  │  Mass Destruction…        │  y=58  scrolling title (size-1, 32 px wide)
//  │  SEQ                      │  y=70  playmode label
//  │                           │
//  │  ███████░░░░░░░░           │  y=118 volume blocks (7 total)
//  └───────────────────────────┘  y=127

void DisplayManager::drawNowPlaying(const DisplayState& s) {
    using namespace VLayout;

    // ── Track-change slide-out animation ─────────────────────────────────────
    //
    // _lastTitle holds the currently-rendered title.  When s.songTitle differs,
    // we slide all content down by _slideOffset (0→H over SLIDE_MS ms) before
    // snapping to the new title.  Drawing always uses _lastTitle so the old
    // content slides off; _lastTitle is updated only when the slide completes.

    const char* newTitle = s.songTitle;

    if (strcmp(newTitle, _lastTitle) != 0) {
        if (_lastTitle[0] == '\0' || _sliding) {
            // First load or a title change mid-slide: snap immediately
            strncpy(_lastTitle, newTitle, sizeof(_lastTitle) - 1);
            _lastTitle[sizeof(_lastTitle) - 1] = '\0';
            _scrollX         = 0;
            _scrollHoldTicks = SCROLL_HOLD_TICKS;
            _lastScrollTick  = millis();
            _sliding         = false;
            _slideOffset     = 0;
        } else {
            // Normal track change: begin slide-out
            _sliding      = true;
            _slideStartMs = millis();
            _slideOffset  = 0;
        }
    }
    if (_sliding) {
        unsigned long elapsed = millis() - _slideStartMs;
        _slideOffset = (int16_t)((float)elapsed / (float)SLIDE_MS * (float)H);
        if (_slideOffset > H) _slideOffset = H;
        if (elapsed >= SLIDE_MS) {
            // Slide complete — snap to new content
            strncpy(_lastTitle, newTitle, sizeof(_lastTitle) - 1);
            _lastTitle[sizeof(_lastTitle) - 1] = '\0';
            _scrollX         = 0;
            _scrollHoldTicks = SCROLL_HOLD_TICKS;
            _lastScrollTick  = millis();
            _sliding         = false;
            _slideOffset     = 0;
        }
    }

    // Every element's Y is shifted by yOff so the whole frame moves together
    const int16_t yOff = _slideOffset;

    // ── 1. Battery icon (top-right, persistent) ──────────────────────────────
    _disp.drawRect(NP_BATT_X + _pageSlideX, NP_BATT_Y + yOff, NP_BATT_W, NP_BATT_H, SSD1306_WHITE);
    _disp.fillRect(NP_BATT_X + NP_BATT_W + _pageSlideX,
                   NP_BATT_Y + yOff + (NP_BATT_H - NP_BATT_NUB_H) / 2,
                   NP_BATT_NUB_W, NP_BATT_NUB_H, SSD1306_WHITE);
    {
        int16_t fillW = (int16_t)((float)s.batteryPct / 100.0f * (NP_BATT_W - 2));
        if (fillW > 0)
            _disp.fillRect(NP_BATT_X + 1 + _pageSlideX, NP_BATT_Y + yOff + 1,
                           fillW, NP_BATT_H - 2, SSD1306_WHITE);
    }

    // ── 1b. BLE connected indicator (left of battery, same yOff) ────────────
    if (s.bleConnected) {
        _drawBleIcon(16 + _pageSlideX, NP_BATT_Y + yOff);   // 5×9 px, 3 px gap before battery at x=23
    }

    // ── 2. Play / pause icon (left of Row 1) ─────────────────────────────────
    if (s.isPlaying) {
        // ▶ right-pointing filled triangle, 3 columns × 6 px tall
        for (int i = 0; i < 3; i++) {
            _disp.drawFastVLine(NP_ICON_X + i + _pageSlideX,
                                NP_ROW1_Y + yOff + i,
                                6 - (2 * i), SSD1306_WHITE);
        }
    } else {
        // ❚❚ two 2×6 bars, 1 px gap
        _disp.fillRect(NP_ICON_X + _pageSlideX,     NP_ROW1_Y + yOff, 2, 6, SSD1306_WHITE);
        _disp.fillRect(NP_ICON_X + 3 + _pageSlideX, NP_ROW1_Y + yOff, 2, 6, SSD1306_WHITE);
    }

    // ── 3. Remaining time (right-aligned in Row 1) ────────────────────────────
    {
        uint32_t rem = (s.songDurationSec > s.songPositionSec)
                       ? s.songDurationSec - s.songPositionSec : 0;
        char timeBuf[8];
        snprintf(timeBuf, sizeof(timeBuf), "%lu:%02lu",
                 (unsigned long)(rem / 60), (unsigned long)(rem % 60));
        int16_t tw = textWidth(timeBuf, 1);
        _disp.setTextSize(1);
        _disp.setCursor(W - tw + _pageSlideX, NP_ROW1_Y + yOff);
        _disp.print(timeBuf);
    }

    // ── 4. Scrolling title — always draws _lastTitle ──────────────────────────
    {
        int16_t titleW = textWidth(_lastTitle, 1);
        _disp.setTextSize(1);

        if (titleW <= W) {
            _disp.setCursor(0 + _pageSlideX, NP_TITLE_Y + yOff);
            _disp.print(_lastTitle);
        } else {
            // Scroll position only advances while not mid-slide
            if (!_sliding) {
                unsigned long now = millis();
                if (now - _lastScrollTick >= SCROLL_TICK_MS) {
                    _lastScrollTick = now;
                    if (_scrollHoldTicks > 0) {
                        --_scrollHoldTicks;
                    } else {
                        --_scrollX;
                        if (_scrollX < -(titleW + W / 2)) {
                            _scrollX         = 0;
                            _scrollHoldTicks = SCROLL_HOLD_TICKS;
                        }
                    }
                }
            }
            _disp.setCursor(_scrollX + _pageSlideX, NP_TITLE_Y + yOff);
            _disp.print(_lastTitle);
        }
    }

    // ── 5. Playmode label ─────────────────────────────────────────────────────
    {
        const char* modeStr;
        switch (s.playMode) {
            case PlayMode::SHUFFLE:    modeStr = "SHF"; break;
            case PlayMode::REPEAT_ONE: modeStr = "REP"; break;
            default:                   modeStr = "SEQ"; break;
        }
        _disp.setTextSize(1);
        _disp.setCursor(0 + _pageSlideX, NP_MODE_Y + yOff);
        _disp.print(modeStr);
    }

    // ── 6. Volume blocks ─────────────────────────────────────────────────────
    {
        int filledBlocks = s.volume / 3;
        if (filledBlocks > (int)NP_VOL_TOTAL) filledBlocks = (int)NP_VOL_TOTAL;

        for (int i = 0; i < (int)NP_VOL_TOTAL; i++) {
            int16_t bx = NP_VOL_STARTX + (int16_t)(i * (NP_VOL_BW + NP_VOL_GAP)) + _pageSlideX;
            if (i < filledBlocks) {
                _disp.fillRect(bx, NP_VOL_Y + yOff, NP_VOL_BW, NP_VOL_BH, SSD1306_WHITE);
            } else {
                _disp.drawRect(bx, NP_VOL_Y + yOff, NP_VOL_BW, NP_VOL_BH, SSD1306_WHITE);
            }
        }
    }
}

// ─── CLOCK ────────────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌──────────[PCT%]┐  y=0   battery %, size-1, right-aligned
//  │                │
//  │      12        │  y=42  hour,   size-2, centred (2 chars × 12 = 24 px)
//  │      34        │  y=60  minute, size-2, centred
//  │                │
//  │      MON       │  y=88  day-of-week token, size-1, centred
//  │     03/31      │  y=98  date token, size-1, centred
//  └────────────────┘

void DisplayManager::drawClock(const DisplayState& s) {
    using namespace VLayout;

    // ── 1. Battery percentage (top-right) ────────────────────────────────────
    {
        char pctBuf[6];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", s.batteryPct);
        int16_t pw = textWidth(pctBuf, 1);
        _disp.setTextSize(1);
        _disp.setCursor(W - pw + _pageSlideX, CLK_BATT_Y);
        _disp.print(pctBuf);
    }

    // ── 1b. BLE connected indicator (top-left, avoids battery% at top-right) ─
    if (s.bleConnected) {
        _drawBleIcon(0 + _pageSlideX, CLK_BATT_Y);
    }

    // ── 2. Hour and minute — separate rows, size-2, centred ──────────────────
    // "HH:MM" at size-2 is 60 px wide; split onto two rows so each fits in 32 px.
    {
        char hhBuf[3], mmBuf[3];
        snprintf(hhBuf, sizeof(hhBuf), "%02d", s.hour);
        snprintf(mmBuf, sizeof(mmBuf), "%02d", s.minute);

        const int16_t digitW  = textWidth("00", 2);   // 2 chars × 12 = 24 px
        const int16_t centreX = (W - digitW) / 2;     // (32-24)/2 = 4

        _disp.setTextSize(2);
        _disp.setCursor(centreX + _pageSlideX, CLK_HH_Y);
        _disp.print(hhBuf);
        _disp.setCursor(centreX + _pageSlideX, CLK_MM_Y);
        _disp.print(mmBuf);
    }

    // ── 3. Date string — split at first space ─────────────────────────────────
    // Expected format: "MON 03/31". Each token fits in 32 px at size-1.
    {
        _disp.setTextSize(1);
        const char* ds = s.dateStr.c_str();
        const char* sp = strchr(ds, ' ');

        if (sp && sp > ds) {
            char dayBuf[8] = {};
            size_t dayLen = (size_t)(sp - ds);
            if (dayLen >= sizeof(dayBuf)) dayLen = sizeof(dayBuf) - 1;
            strncpy(dayBuf, ds, dayLen);

            int16_t dw = textWidth(dayBuf, 1);
            _disp.setCursor((W - dw) / 2 + _pageSlideX, CLK_DAY_Y);
            _disp.print(dayBuf);

            const char* datePart = sp + 1;
            int16_t ddw = textWidth(datePart, 1);
            _disp.setCursor((W - ddw) / 2 + _pageSlideX, CLK_DATE_Y);
            _disp.print(datePart);
        } else {
            int16_t dw = textWidth(ds, 1);
            _disp.setCursor((W - dw) / 2 + _pageSlideX, CLK_DAY_Y);
            _disp.print(ds);
        }
    }
}

// ─── STEPS ────────────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌──────────[PCT%]┐  y=0   battery %, size-1, right-aligned
//  │                │
//  │     7,832      │  y=48  step count, size-2, centred
//  │                │
//  │  231.5 kcal    │  y=72  calorie count, size-1, centred
//  └────────────────┘

void DisplayManager::drawSteps(const DisplayState& s) {
    using namespace VLayout;

    // ── 1. Battery percentage (top-right) ────────────────────────────────────
    {
        char pctBuf[6];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", s.batteryPct);
        int16_t pw = textWidth(pctBuf, 1);
        _disp.setTextSize(1);
        _disp.setCursor(W - pw + _pageSlideX, STP_BATT_Y);
        _disp.print(pctBuf);
    }

    // ── 1b. BLE connected indicator (top-left) ───────────────────────────────
    if (s.bleConnected) {
        _drawBleIcon(0 + _pageSlideX, STP_BATT_Y);
    }

    // ── 2. Step count, size-2, centred ───────────────────────────────────────
    {
        char stepBuf[12];
        formatSteps(stepBuf, sizeof(stepBuf), s.stepCount);
        int16_t sw = textWidth(stepBuf, 2);
        _disp.setTextSize(2);
        _disp.setCursor((W - sw) / 2 + _pageSlideX, STP_COUNT_Y);
        _disp.print(stepBuf);
    }

    // ── 3. Calorie count, size-1, centred ────────────────────────────────────
    {
        char calBuf[14];
        snprintf(calBuf, sizeof(calBuf), "%.1f kcal", s.caloriesBurned);
        int16_t cw = textWidth(calBuf, 1);
        _disp.setTextSize(1);
        _disp.setCursor((W - cw) / 2 + _pageSlideX, STP_CAL_Y);
        _disp.print(calBuf);
    }
}

// ─── BATTERY ─────────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌────────────────┐
//  │                │
//  │      72%       │  y=30  percentage, size-2, centred
//  │                │
//  │     ┌──┐       │  y=55  nub (positive terminal, 8×3 px, centred on bar)
//  │     │██│       │  y=58  outline (20×58 px, centred → x=6)
//  │     │██│       │        fill rises from bottom proportional to batteryPct
//  │     │  │       │
//  │     └──┘       │  y=116 outline bottom
//  └────────────────┘

void DisplayManager::drawBattery(const DisplayState& s) {
    using namespace VLayout;

    // ── 1. Percentage number, size-2, centred ────────────────────────────────
    {
        char pctBuf[5];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", s.batteryPct);
        int16_t pw = textWidth(pctBuf, 2);
        _disp.setTextSize(2);
        _disp.setCursor((W - pw) / 2 + _pageSlideX, BAT_PCT_Y);
        _disp.print(pctBuf);
    }

    // ── 1b. BLE connected indicator (top-left) ───────────────────────────────
    if (s.bleConnected) {
        _drawBleIcon(0 + _pageSlideX, 0);
    }

    // ── 2. Positive-terminal nub (centred above outline) ──────────────────────
    _disp.fillRect(BAT_BAR_X + (BAT_BAR_W - BAT_NUB_W) / 2 + _pageSlideX,
                   BAT_BAR_Y - BAT_NUB_H,
                   BAT_NUB_W, BAT_NUB_H, SSD1306_WHITE);

    // ── 3. Outline ────────────────────────────────────────────────────────────
    _disp.drawRect(BAT_BAR_X + _pageSlideX, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, SSD1306_WHITE);

    // ── 4. Fill from bottom, proportional to batteryPct ─────────────────────
    {
        int16_t inner  = BAT_BAR_H - 2;
        int16_t fillH  = (int16_t)((float)s.batteryPct / 100.0f * inner);
        if (fillH > 0) {
            _disp.fillRect(BAT_BAR_X + 1 + _pageSlideX,
                           BAT_BAR_Y + 1 + inner - fillH,
                           BAT_BAR_W - 2, fillH, SSD1306_WHITE);
        }
    }
}

// ─── DARK HOUR ───────────────────────────────────────────────────────────────
//
//  ┌────────────────────────────────┐
//  │         00:00         :00      │  ← HH:MM size-2 centred + :SS size-1 (y=2)
//  │                                │
//  │                                │
//  │      -- DARK HOUR --           │  ← label size-1, centred          (y=24)
//  └────────────────────────────────┘
//
//  Entrance animation (first 800 ms): a bright scan line sweeps top→bottom,
//  revealing the content row by row.  After the animation the full frame stays.

void DisplayManager::drawDarkHour(const DisplayState& s) {
    // ── First entry: record timestamp and ensure the physical screen is on ────
    if (_darkHourEnteredMs == 0) {
        _darkHourEnteredMs = millis();
        wake();
    }

    unsigned long elapsed = millis() - _darkHourEnteredMs;
    constexpr unsigned long ANIM_MS = 800;
    bool animating = (elapsed < ANIM_MS);

    // ── HH:MM in large font, centred ──────────────────────────────────────────
    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", s.hour, s.minute);

    _disp.setTextSize(2);
    int16_t  tx, ty;
    uint16_t tw, th;
    _disp.getTextBounds(hmBuf, 0, 0, &tx, &ty, &tw, &th);
    int16_t hmX = (DISPLAY_WIDTH - (int16_t)tw) / 2;
    _disp.setCursor(hmX, Layout::DH_TIME_Y);
    _disp.print(hmBuf);

    // ── :SS in small font, right of main time ─────────────────────────────────
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), ":%02d", s.second);
    _disp.setTextSize(1);
    _disp.setCursor(hmX + (int16_t)tw + 1, Layout::DH_TIME_Y + Layout::DH_SEC_OFFSET);
    _disp.print(secBuf);

    // ── "-- DARK HOUR --" label, centred ──────────────────────────────────────
    static const char* LABEL = "-- DARK HOUR --";
    uint16_t lw, lh;
    _disp.getTextBounds(LABEL, 0, 0, &tx, &ty, &lw, &lh);
    _disp.setCursor((DISPLAY_WIDTH - (int16_t)lw) / 2, Layout::DH_LABEL_Y);
    _disp.print(LABEL);

    // ── Scan-line reveal animation ────────────────────────────────────────────
    if (animating) {
        // scanY advances from 0 to DISPLAY_HEIGHT over ANIM_MS
        int16_t scanY = (int16_t)((float)elapsed / (float)ANIM_MS * DISPLAY_HEIGHT);

        // Bright leading edge
        _disp.drawFastHLine(0, scanY, DISPLAY_WIDTH, SSD1306_WHITE);

        // Black out everything below the scan line (not yet revealed)
        if (scanY + 1 < DISPLAY_HEIGHT) {
            _disp.fillRect(0, scanY + 1, DISPLAY_WIDTH,
                           DISPLAY_HEIGHT - scanY - 1, SSD1306_BLACK);
        }
    }
}

// ─── WAKE ANIMATION ───────────────────────────────────────────────────────────
//
//  128 × 32 horizontal (rotation 0)
//
//  x=0                                              x=127
//  ┌────────────────────────────────────────────────┐
//  │                                                │
//  │              H E L L O !                       │  ← size-2, centred (y=8)
//  │                                                │
//  └────────────────────────────────────────────────┘
//
//  State 0: characters appear one by one, WAKE_CHAR_MS apart.
//  State 1: full "HELLO!" held for WAKE_HOLD_MS, then s.page → NOW_PLAYING.
//  _wakeAnimTick == 0 means the session has just been entered; initialise here.

void DisplayManager::drawWake(DisplayState& s) {
    using namespace VLayout;

    unsigned long now = millis();

    // Initialise on first call for this WAKE session
    if (_wakeAnimTick == 0) {
        _wakeAnimTick = now;
    }

    if (_wakeAnimState == 0) {
        // Reveal next character after each WAKE_CHAR_MS interval
        if (now - _wakeAnimTick >= WAKE_CHAR_MS) {
            _wakeAnimTick = now;
            ++_wakeCharIndex;
            if (_wakeCharIndex >= WAKE_TEXT_N) {
                _wakeCharIndex = WAKE_TEXT_N;   // clamp
                _wakeAnimState = 1;             // all chars shown → enter hold
                _wakeAnimTick  = now;
            }
        }
    } else if (_wakeAnimState == 1) {
        // Hold full "HELLO!" then hand off
        if (now - _wakeAnimTick >= WAKE_HOLD_MS) {
            s.page = DisplayPage::NOW_PLAYING;
            return;   // rotation change + fresh draw happen on next update() call
        }
    }

    // Draw the revealed portion of "HELLO!"
    static constexpr char WAKE_TEXT[] = "HELLO!";
    _disp.setTextSize(2);
    for (int i = 0; i < _wakeCharIndex; i++) {
        _disp.setCursor(WAKE_X + (int16_t)(i * WAKE_CHAR_W), WAKE_Y);
        _disp.write(WAKE_TEXT[i]);
    }
}

// ─── BLE PAIRING ─────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌────────────────┐
//  │      BLE       │  y=10  label, size-1, centred
//  │ ─────────────  │  y=22  separator line
//  │       ╱        │
//  │      ╱╲        │  y=30–66  Bluetooth symbol (36 px tall)
//  │     ╱  ╲       │
//  │      ╲  ╱      │
//  │       ╲╱       │
//  │      PAIR      │  y=76  status ("PAIR" or "CONN"), size-1, centred
//  └────────────────┘

void DisplayManager::drawBtPairing(const DisplayState& s) {
    using namespace VLayout;

    // ── "BLE" heading ────────────────────────────────────────────────────────
    {
        _disp.setTextSize(1);
        const char* heading = "BLE";
        int16_t hw = textWidth(heading, 1);
        _disp.setCursor((W - hw) / 2 + _pageSlideX, 10);
        _disp.print(heading);
    }

    // ── Separator ─────────────────────────────────────────────────────────────
    _disp.drawFastHLine(4 + _pageSlideX, 22, W - 8, SSD1306_WHITE);

    // ── Bluetooth symbol (36 px tall, centred) ────────────────────────────────
    //
    //  Top  (16,30) ─┬─ upper-right (24,40) ─┬─ centre (16,48)
    //                │                        │
    //              left                      left
    //              serifs                   ignored
    //                │                        │
    //  Bot  (16,66) ─┴─ lower-right (24,56) ─┴─ centre (16,48)

    constexpr int16_t BT_CX  = W / 2;   // 16
    constexpr int16_t BT_TOP = 30;
    constexpr int16_t BT_UR  = 40;      // upper-right junction y
    constexpr int16_t BT_MID = 48;
    constexpr int16_t BT_LR  = 56;      // lower-right junction y
    constexpr int16_t BT_BOT = 66;
    constexpr int16_t BT_RX  = BT_CX + 8;   // 24 — right arm tip x
    constexpr int16_t BT_LX  = BT_CX - 8;   //  8 — left  serif tip x

    _disp.drawFastVLine(BT_CX + _pageSlideX, BT_TOP, BT_BOT - BT_TOP + 1, SSD1306_WHITE);
    _disp.drawLine(BT_CX + _pageSlideX, BT_TOP, BT_RX + _pageSlideX, BT_UR,  SSD1306_WHITE);
    _disp.drawLine(BT_RX + _pageSlideX, BT_UR,  BT_CX + _pageSlideX, BT_MID, SSD1306_WHITE);
    _disp.drawLine(BT_CX + _pageSlideX, BT_MID, BT_RX + _pageSlideX, BT_LR,  SSD1306_WHITE);
    _disp.drawLine(BT_RX + _pageSlideX, BT_LR,  BT_CX + _pageSlideX, BT_BOT, SSD1306_WHITE);
    _disp.drawLine(BT_CX + _pageSlideX, BT_TOP, BT_LX + _pageSlideX, BT_UR,  SSD1306_WHITE);
    _disp.drawLine(BT_CX + _pageSlideX, BT_BOT, BT_LX + _pageSlideX, BT_LR,  SSD1306_WHITE);

    // ── Status label ──────────────────────────────────────────────────────────
    {
        _disp.setTextSize(1);
        const char* status = s.bleConnected ? "CONN" : "PAIR";
        int16_t sw = textWidth(status, 1);
        _disp.setCursor((W - sw) / 2 + _pageSlideX, 76);
        _disp.print(status);
    }
}

// ─── EQ ───────────────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌──────────[BAT]┐  y=0   battery icon (persistent, top-right)
//  │               │
//  │     FLAT      │  y=12  preset name, size-1, centred
//  │               │
//  │  ┌─┐┌─┐┌─┐┌─┐┌─┐  y=24–96  5 vertical bars (3×72 px each)
//  │  │ ││ ││ ││ ││ ││  │
//  │  └─┘└─┘└─┘└─┘└─┘
//  │  32 250 1k 4k 16k  y=100  frequency labels, size-1
//  └───────────────┘

void DisplayManager::drawEQ(const DisplayState& s) {
    using namespace VLayout;

    // ── Battery icon (top-right, same layout as NOW_PLAYING) ─────────────────
    _disp.drawRect(NP_BATT_X + _pageSlideX, NP_BATT_Y, NP_BATT_W, NP_BATT_H, SSD1306_WHITE);
    _disp.fillRect(NP_BATT_X + NP_BATT_W + _pageSlideX,
                   NP_BATT_Y + (NP_BATT_H - NP_BATT_NUB_H) / 2,
                   NP_BATT_NUB_W, NP_BATT_NUB_H, SSD1306_WHITE);
    {
        int16_t fillW = (int16_t)((float)s.batteryPct / 100.0f * (NP_BATT_W - 2));
        if (fillW > 0)
            _disp.fillRect(NP_BATT_X + 1 + _pageSlideX, NP_BATT_Y + 1,
                           fillW, NP_BATT_H - 2, SSD1306_WHITE);
    }

    // ── BLE connected indicator ───────────────────────────────────────────────
    if (s.bleConnected) {
        _drawBleIcon(16 + _pageSlideX, NP_BATT_Y);
    }

    // ── Preset name, centred ─────────────────────────────────────────────────
    {
        static const char* const PRESET_NAMES[] = {"FLAT", "HEAVY", "POP", "JAZZ"};
        const char* name = (s.eqPreset < 4) ? PRESET_NAMES[s.eqPreset] : "CUST";
        _disp.setTextSize(1);
        int16_t nw = textWidth(name, 1);
        _disp.setCursor((W - nw) / 2 + _pageSlideX, 12);
        _disp.print(name);
    }

    // ── 5 vertical bars ───────────────────────────────────────────────────────
    //
    // Bar gain range −40..+6 dB = 46 dB total.  Fill rises from bottom:
    //   fillH = (gain + 40) / 46 * (BAR_H − 2)  (inner height)
    // A thin 0 dB reference line is drawn at 6/46 of the way from the top.

    constexpr int16_t BAR_W      =  3;
    constexpr int16_t BAR_GAP    =  2;
    constexpr int16_t BAR_TOP    = 24;
    constexpr int16_t BAR_H      = 72;
    constexpr int16_t LABEL_Y    = BAR_TOP + BAR_H + 4;   // 100
    // total strip width = 5×3 + 4×2 = 23 px; start x = (32−23)/2 = 4
    constexpr int16_t BAR_START  = (W - (5 * BAR_W + 4 * BAR_GAP)) / 2;  // 4

    // 0 dB reference line (6 dB from top of bar inner area)
    constexpr int16_t ZERO_DB_Y  = BAR_TOP + 1 +
        (int16_t)(6.0f / 46.0f * (BAR_H - 2));   // ≈ 34

    for (uint8_t i = 0; i < 5; i++) {
        int16_t bx = BAR_START + (int16_t)(i * (BAR_W + BAR_GAP)) + _pageSlideX;

        // Outline
        _disp.drawRect(bx, BAR_TOP, BAR_W, BAR_H, SSD1306_WHITE);

        // Fill from bottom
        int8_t gain = s.eqBands[i];
        if (gain < -40) gain = -40;
        if (gain >   6) gain =   6;
        int16_t fillH = (int16_t)(((float)(gain + 40) / 46.0f) * (BAR_H - 2));
        if (fillH > 0) {
            _disp.fillRect(bx + 1,
                           BAR_TOP + 1 + (BAR_H - 2) - fillH,
                           BAR_W - 2, fillH, SSD1306_WHITE);
        }

        // 0 dB tick mark (1 px wide, drawn over the outline)
        _disp.drawPixel(bx, ZERO_DB_Y, SSD1306_WHITE);
    }

    // ── Frequency labels ──────────────────────────────────────────────────────
    static const char* const FREQ_LABELS[] = {"32", "250", "1k", "4k", "16k"};
    _disp.setTextSize(1);
    for (uint8_t i = 0; i < 5; i++) {
        int16_t bx  = BAR_START + (int16_t)(i * (BAR_W + BAR_GAP)) + _pageSlideX;
        int16_t lw  = textWidth(FREQ_LABELS[i], 1);
        int16_t lx  = bx + BAR_W / 2 - lw / 2;
        _disp.setCursor(lx, LABEL_Y);
        _disp.print(FREQ_LABELS[i]);
    }
}

// ─── USB_MSC ──────────────────────────────────────────────────────────────────
//
//  32 px wide × 128 px tall (rotation 1)
//
//  x=0              x=31
//  ┌────────────────┐
//  │      USB       │  y=10  "USB" heading, size-1, centred
//  │  ──────────    │  y=22  separator
//  │                │
//  │    ┌──────┐    │  y=32  USB-A connector body (16×10 px, centred)
//  │    │██████│    │  y=34  filled contact area
//  │    └──┬───┘    │  y=42
//  │       │        │  y=42–58  cable
//  │    ┌──┴──┐     │  y=58  device-side connector (10×8 px, centred)
//  │    └─────┘     │  y=66
//  │                │
//  │      CONN      │  y=80  status label, size-1, centred
//  │      MSC       │  y=96  sublabel, size-1, centred
//  └────────────────┘

void DisplayManager::drawUsbMsc(const DisplayState& /*s*/) {
    using namespace VLayout;

    // ── "USB" heading ────────────────────────────────────────────────────────
    {
        _disp.setTextSize(1);
        const char* heading = "USB";
        int16_t hw = textWidth(heading, 1);
        _disp.setCursor((W - hw) / 2 + _pageSlideX, 10);
        _disp.print(heading);
    }

    // ── Separator ─────────────────────────────────────────────────────────────
    _disp.drawFastHLine(4 + _pageSlideX, 22, W - 8, SSD1306_WHITE);

    // ── USB-A connector body ──────────────────────────────────────────────────
    constexpr int16_t USB_CX  = W / 2;        // 16 — horizontal centre
    constexpr int16_t USB_BW  = 16;           // connector body width
    constexpr int16_t USB_BH  = 10;           // connector body height
    constexpr int16_t USB_BX  = USB_CX - USB_BW / 2;  // 8
    constexpr int16_t USB_BY  = 32;

    _disp.drawRect (USB_BX + _pageSlideX, USB_BY, USB_BW, USB_BH, SSD1306_WHITE);
    // Filled contact area (2 px margin inside)
    _disp.fillRect (USB_BX + 2 + _pageSlideX, USB_BY + 2, USB_BW - 4, USB_BH - 4, SSD1306_WHITE);

    // ── Cable (vertical line from connector bottom to device port) ────────────
    constexpr int16_t CABLE_Y0 = USB_BY + USB_BH;   // 42
    constexpr int16_t CABLE_Y1 = 58;
    _disp.drawFastVLine(USB_CX + _pageSlideX, CABLE_Y0, CABLE_Y1 - CABLE_Y0, SSD1306_WHITE);

    // ── Device-side connector (smaller rect, centred on cable) ────────────────
    constexpr int16_t DEV_W = 10;
    constexpr int16_t DEV_H =  8;
    constexpr int16_t DEV_X = USB_CX - DEV_W / 2;   // 11
    constexpr int16_t DEV_Y = CABLE_Y1;             // 58
    _disp.drawRect(DEV_X + _pageSlideX, DEV_Y, DEV_W, DEV_H, SSD1306_WHITE);

    // ── Status label ──────────────────────────────────────────────────────────
    {
        _disp.setTextSize(1);
        const char* status = "CONN";
        int16_t sw = textWidth(status, 1);
        _disp.setCursor((W - sw) / 2 + _pageSlideX, 80);
        _disp.print(status);
    }

    // ── "MSC" sublabel ────────────────────────────────────────────────────────
    {
        _disp.setTextSize(1);
        const char* sub = "MSC";
        int16_t ssw = textWidth(sub, 1);
        _disp.setCursor((W - ssw) / 2 + _pageSlideX, 96);
        _disp.print(sub);
    }
}

// ─── _drawBleIcon ─────────────────────────────────────────────────────────────
//
// Compact 5 × 9 px Bluetooth symbol drawn at (x, y).
// Used as a "connected" indicator on all pages except DARK_HOUR and WAKE.

void DisplayManager::_drawBleIcon(int16_t x, int16_t y) {
    // Vertical spine
    _disp.drawFastVLine(x + 2, y, 9, SSD1306_WHITE);
    // Upper-right arm
    _disp.drawLine(x + 2, y,     x + 4, y + 2, SSD1306_WHITE);
    _disp.drawLine(x + 4, y + 2, x + 2, y + 4, SSD1306_WHITE);
    // Lower-right arm
    _disp.drawLine(x + 2, y + 4, x + 4, y + 6, SSD1306_WHITE);
    _disp.drawLine(x + 4, y + 6, x + 2, y + 8, SSD1306_WHITE);
    // Upper-left serif
    _disp.drawLine(x + 2, y,     x,     y + 2, SSD1306_WHITE);
    // Lower-left serif
    _disp.drawLine(x + 2, y + 8, x,     y + 6, SSD1306_WHITE);
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
