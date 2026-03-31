#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "../.pio/libdeps/esp32s3_super_mini/ESP32-audioI2S/src/Audio.h"
#include "display.h"   // DisplayState, PlayMode

// ─── Pin constants ────────────────────────────────────────────────────────────
// I²S bus connected to PCM5102A stereo DAC → 3.5 mm headphone jack.

#define AUDIO_BCLK_PIN    4   // PCM5102A BCK
#define AUDIO_LRC_PIN     5   // PCM5102A LCK
#define AUDIO_DIN_PIN     6   // PCM5102A DIN

#define SD_SCK_PIN       12
#define SD_MISO_PIN      13
#define SD_MOSI_PIN      11
#define SD_CS_PIN        10

#define HP_DETECT_PIN    16

// Polarity of the headphone-detect switch.
// LOW  = headphones inserted (most common: jack shorts detect pin to GND).
// Change to HIGH if your specific jack uses an active-high switch.
#define HP_INSERTED_LEVEL LOW

// ─── Limits ───────────────────────────────────────────────────────────────────

#define AUDIO_VOL_DEFAULT  10    // 0–21
#define AUDIO_VOL_MAX      21
#define PLAYLIST_MAX      200    // max tracks scanned from SD root

// ─── AudioManager ─────────────────────────────────────────────────────────────

class AudioManager {
public:
    AudioManager();

    // Call once in setup(). Inits SPI/SD, scans playlist, starts I2S.
    // Populates state.songTitle etc. for the first track.
    // Returns false if SD init fails (audio will be unavailable).
    bool begin(DisplayState& state);

    // Call every Arduino loop iteration.
    // Drives the Audio library, polls headphone detect, refreshes DisplayState.
    void update(DisplayState& state);

    // ── Playback controls ─────────────────────────────────────────────────────

    // Play track by playlist index (0-based). No-op if index out of range.
    void play(int index);

    void pause();
    void resume();

    // Skip to next / previous track respecting the current play mode.
    // previous() restarts the track if position > 3 s, otherwise goes back one.
    void next();
    void previous();

    // Volume: 0 (mute) – 21 (max). Clamped silently.
    void setVolume(uint8_t vol);

    void setPlayMode(PlayMode mode);

    // ── Getters ───────────────────────────────────────────────────────────────

    PlayMode getPlayMode()    const { return _playMode; }
    int      getTrackCount()  const { return _trackCount; }
    int      getCurrentIndex()const { return _currentIdx; }
    uint8_t  getVolume()      const { return _volume; }

    // Returns filename without path or extension, e.g. "Mass Destruction".
    // Points to an internal char buffer — valid until the next play() call.
    const char* getCurrentTrackTitle() const;

    // Both return 0 if unavailable (e.g. no track loaded yet).
    uint32_t getCurrentPositionSec();
    uint32_t getTotalDurationSec();

    // Returns the playlist index of the first track whose title contains
    // 'name' (case-insensitive).  Returns -1 if not found.
    // Used by BLEManager to locate the Dark Hour BGM ("tartarus").
    int findTrackByName(const String& name);

    // ── Internal callback — do not call from outside ──────────────────────────
    void _onTrackEnd();

private:
    Audio    _audio;

    // ── Playlist ──────────────────────────────────────────────────────────────
    String   _playlist[PLAYLIST_MAX];   // full paths, e.g. "/song.mp3"
    int      _trackCount = 0;
    int      _currentIdx = 0;
    char     _titleBuf[64] = {};        // current track title — filled in _startTrack()

    // ── Shuffle state ─────────────────────────────────────────────────────────
    int      _shuffleOrder[PLAYLIST_MAX];  // permutation of [0, _trackCount)
    int      _shufflePos  = 0;             // position in _shuffleOrder[]
    void     _buildShuffle();              // Fisher-Yates from current position

    // ── Playback state ────────────────────────────────────────────────────────
    PlayMode _playMode  = PlayMode::SEQUENTIAL;
    bool     _isPlaying = false;   // true once a track has been started
    bool     _isPaused  = false;
    uint8_t  _volume    = AUDIO_VOL_DEFAULT;

    // ── Headphone detection ───────────────────────────────────────────────────
    bool          _hpInserted       = false;
    bool          _hpPausedByUnplug = false;   // true if we auto-paused on unplug
    unsigned long _hpLastEdgeMs     = 0;
    static constexpr uint16_t HP_DEBOUNCE_MS = 60;
    void _pollHpDetect(DisplayState& state);

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool  _scanSD();
    void  _startTrack(int index);
    // Extracts filename stem from a path into buf[len].  No heap allocation.
    static void _titleFromPath(const String& path, char* buf, size_t len);
    static bool _isAudioFile  (const String& name);
};
