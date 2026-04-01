#include "audio.h"

// ─── Constructor ─────────────────────────────────────────────────────────────
//
// ESP32-audioI2S v3 removed the old free-function callbacks (audio_eof_mp3 etc.)
// in favour of Audio::audio_info_callback, an inline static std::function.
// We register it here; evt_eof fires for MP3, FLAC, and AAC local-file playback.

AudioManager::AudioManager() {
    _titleBuf[0] = '\0';
    for (int i = 0; i < PLAYLIST_MAX; i++) _shuffleOrder[i] = i;

    Audio::audio_info_callback = [this](Audio::msg_t m) {
        if (m.e == Audio::evt_eof) _onTrackEnd();
    };
}

// ─── begin ───────────────────────────────────────────────────────────────────

bool AudioManager::begin(DisplayState& state) {
    // ── SD card ───────────────────────────────────────────────────────────────

    // Reconfigure the default SPI bus to our custom pins before calling SD.begin.
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[audio] SD init failed — check wiring");
        return false;
    }
    Serial.println("[audio] SD OK");

    if (!_scanSD()) {
        Serial.println("[audio] no audio files found in SD root");
        // Not a fatal error — device boots, just nothing to play
    } else {
        Serial.printf("[audio] playlist: %d tracks\n", _trackCount);
    }

    // ── I2S → PCM5102A ───────────────────────────────────────────────────────
    // PCM5102A receives I²S from the ESP32-S3 and outputs analog stereo to the
    // 3.5 mm headphone jack.  Volume is controlled digitally via I²S data.

    _audio.setPinout(AUDIO_BCLK_PIN, AUDIO_LRC_PIN, AUDIO_DIN_PIN);
    _audio.setVolume(_volume);
    applyEQPreset(0);   // boot default: FLAT

    // ── Headphone detect ─────────────────────────────────────────────────────

    pinMode(HP_DETECT_PIN, INPUT_PULLUP);
    _hpInserted = (digitalRead(HP_DETECT_PIN) == HP_INSERTED_LEVEL);
    Serial.printf("[audio] headphones %s\n", _hpInserted ? "connected" : "disconnected");

    // ── Start first track (if any) ────────────────────────────────────────────

    if (_trackCount > 0) {
        _startTrack(0);
    }

    // ── Populate initial DisplayState ─────────────────────────────────────────

    strlcpy(state.songTitle, getCurrentTrackTitle(), sizeof(state.songTitle));
    state.songPositionSec = 0;
    state.songDurationSec = 0;   // filled after audio buffers a bit
    state.isPlaying       = _isPlaying && !_isPaused;
    state.playMode        = _playMode;
    state.volume          = _volume;

    return true;
}

// ─── update ──────────────────────────────────────────────────────────────────

void AudioManager::update(DisplayState& state) {
    // Must be called every loop iteration — drives the Audio decoder / callbacks
    _audio.loop();

    // ── Refresh DisplayState ──────────────────────────────────────────────────
    // songTitle: copy from cached _titleBuf — no heap allocation.

    strlcpy(state.songTitle, _titleBuf, sizeof(state.songTitle));
    state.songPositionSec = getCurrentPositionSec();
    state.songDurationSec = getTotalDurationSec();
    state.isPlaying       = _isPlaying && !_isPaused;
    state.playMode        = _playMode;
    state.volume          = _volume;
    state.eqPreset = _eqPreset;
    memcpy(state.eqBands, _eqBands, sizeof(_eqBands));

    // ── Headphone detection ───────────────────────────────────────────────────

    _pollHpDetect(state);
}

// ─── Playback controls ────────────────────────────────────────────────────────

void AudioManager::play(int index) {
    if (index < 0 || index >= _trackCount) return;
    _startTrack(index);
}

void AudioManager::pause() {
    if (!_isPlaying || _isPaused) return;
    _audio.pauseResume();
    _isPaused = true;
}

void AudioManager::resume() {
    if (!_isPlaying || !_isPaused) return;
    _audio.pauseResume();
    _isPaused = false;
}

void AudioManager::next() {
    if (_trackCount == 0) return;

    switch (_playMode) {
        case PlayMode::SEQUENTIAL:
            _startTrack((_currentIdx + 1) % _trackCount);
            break;

        case PlayMode::SHUFFLE:
            _shufflePos = (_shufflePos + 1) % _trackCount;
            _startTrack(_shuffleOrder[_shufflePos]);
            break;

        case PlayMode::REPEAT_ONE:
            _startTrack(_currentIdx);
            break;
    }
}

void AudioManager::previous() {
    if (_trackCount == 0) return;

    // Standard behaviour: if we're more than 3 s in, restart the current track;
    // otherwise actually go back one.
    if (getCurrentPositionSec() > 3) {
        _startTrack(_currentIdx);
        return;
    }

    switch (_playMode) {
        case PlayMode::SEQUENTIAL:
            _startTrack((_currentIdx - 1 + _trackCount) % _trackCount);
            break;

        case PlayMode::SHUFFLE:
            _shufflePos = (_shufflePos - 1 + _trackCount) % _trackCount;
            _startTrack(_shuffleOrder[_shufflePos]);
            break;

        case PlayMode::REPEAT_ONE:
            _startTrack(_currentIdx);
            break;
    }
}

void AudioManager::setVolume(uint8_t vol) {
    _volume = min(vol, (uint8_t)AUDIO_VOL_MAX);
    _audio.setVolume(_volume);
}

void AudioManager::setPlayMode(PlayMode mode) {
    if (mode == _playMode) return;
    _playMode = mode;
    if (mode == PlayMode::SHUFFLE) {
        // Rebuild shuffle from the current position so the current track plays
        // to completion and the next one is random.
        _buildShuffle();
    }
    Serial.printf("[audio] play mode: %d\n", (int)mode);
}

// ─── applyEQPreset ────────────────────────────────────────────────────────────

void AudioManager::applyEQPreset(uint8_t preset) {
    static constexpr int8_t PRESETS[4][5] = {
        { 0,  0,  0,  0,  0},   // 0 FLAT
        { 8,  4,  0,  0,  0},   // 1 HEAVY — boost low and low-mid
        { 0,  2,  4,  2,  0},   // 2 POP   — mid presence boost
        { 4,  2,  0,  2,  4},   // 3 JAZZ  — warm lows + airy highs
    };
    if (preset >= 4) return;
    _eqPreset = preset;
    for (uint8_t i = 0; i < 5; i++) _eqBands[i] = PRESETS[preset][i];
    _applyBandsToHW();
    Serial.printf("[audio] EQ preset %d applied\n", preset);
}

// ─── setEQBands ───────────────────────────────────────────────────────────────

void AudioManager::setEQBands(const int8_t bands[5]) {
    for (uint8_t i = 0; i < 5; i++) {
        int8_t g = bands[i];
        if (g < -40) g = -40;
        if (g >   6) g =   6;
        _eqBands[i] = g;
    }
    _eqPreset = 0xFF;   // custom — no named preset active
    _applyBandsToHW();
    Serial.println("[audio] EQ custom bands applied");
}

// ─── _applyBandsToHW ─────────────────────────────────────────────────────────
//
// Maps 5 logical bands (32, 250, 1k, 4k, 16kHz) to ESP32-audioI2S setTone():
//   gainLowPass  — lowshelf  ~500 Hz : weighted blend of bands[0] and bands[1]
//   gainBandPass — peakingEQ ~1800 Hz: bands[2]
//   gainHighPass — highshelf ~6000 Hz: weighted blend of bands[3] and bands[4]

void AudioManager::_applyBandsToHW() {
    float low  = _eqBands[0] * 0.6f + _eqBands[1] * 0.4f;
    float mid  = (float)_eqBands[2];
    float high = _eqBands[3] * 0.4f + _eqBands[4] * 0.6f;
    _audio.setTone(low, mid, high);
}

// ─── Getters ─────────────────────────────────────────────────────────────────

const char* AudioManager::getCurrentTrackTitle() const {
    return (_trackCount > 0) ? _titleBuf : "No tracks";
}

uint32_t AudioManager::getCurrentPositionSec() {
    if (!_isPlaying) return 0;
    return _audio.getAudioCurrentTime();
}

uint32_t AudioManager::getTotalDurationSec() {
    if (!_isPlaying) return 0;
    return _audio.getAudioFileDuration();
}

// ─── EOF callback (called by global audio_eof_mp3) ───────────────────────────

void AudioManager::_onTrackEnd() {
    Serial.printf("[audio] EOF: track %d\n", _currentIdx);

    switch (_playMode) {
        case PlayMode::SEQUENTIAL:
            // Wrap at end of playlist
            _startTrack((_currentIdx + 1) % _trackCount);
            break;

        case PlayMode::SHUFFLE:
            _shufflePos = (_shufflePos + 1) % _trackCount;
            _startTrack(_shuffleOrder[_shufflePos]);
            break;

        case PlayMode::REPEAT_ONE:
            _startTrack(_currentIdx);
            break;
    }
}

// ─── Headphone detection ─────────────────────────────────────────────────────

void AudioManager::_pollHpDetect(DisplayState& /*state*/) {
    bool pinLevel = (digitalRead(HP_DETECT_PIN) == HP_INSERTED_LEVEL);

    // Only react after debounce window
    if (pinLevel == _hpInserted) {
        _hpLastEdgeMs = millis();   // reset timer while stable
        return;
    }
    if (millis() - _hpLastEdgeMs < HP_DEBOUNCE_MS) return;

    // Stable new state — commit it
    _hpInserted   = pinLevel;
    _hpLastEdgeMs = millis();

    if (!_hpInserted) {
        // Headphones unplugged
        Serial.println("[audio] headphones removed — pausing");
        if (_isPlaying && !_isPaused) {
            pause();
            _hpPausedByUnplug = true;
        }
    } else {
        // Headphones plugged back in
        Serial.println("[audio] headphones inserted — resuming");
        if (_hpPausedByUnplug && _isPaused) {
            resume();
        }
        _hpPausedByUnplug = false;
    }
}

// ─── Private helpers ─────────────────────────────────────────────────────────

// Scan SD root for .mp3 / .flac files and populate _playlist[].
bool AudioManager::_scanSD() {
    File root = SD.open("/");
    if (!root) {
        Serial.println("[audio] SD.open('/') failed");
        return false;
    }

    _trackCount = 0;

    File entry = root.openNextFile();
    while (entry && _trackCount < PLAYLIST_MAX) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            // entry.name() may or may not include the leading slash
            if (!name.startsWith("/")) name = "/" + name;

            if (_isAudioFile(name)) {
                _playlist[_trackCount++] = name;
                Serial.printf("[audio]   [%d] %s\n", _trackCount - 1, name.c_str());
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    return (_trackCount > 0);
}

// Open and start playing the track at the given playlist index.
// Fills _titleBuf here so update() never needs to call _titleFromPath().
void AudioManager::_startTrack(int index) {
    if (index < 0 || index >= _trackCount) return;

    _currentIdx = index;
    _isPaused   = false;
    _isPlaying  = true;

    _titleFromPath(_playlist[index], _titleBuf, sizeof(_titleBuf));

    const char* path = _playlist[index].c_str();
    Serial.printf("[audio] playing: %s\n", path);
    _audio.connecttoFS(SD, path);
}

// Fisher-Yates shuffle of [0, _trackCount).
// The current track is placed at _shufflePos so it finishes before shuffling.
void AudioManager::_buildShuffle() {
    for (int i = 0; i < _trackCount; i++) _shuffleOrder[i] = i;

    // Swap current track to position 0 so it becomes the start of the shuffle
    // (it's already playing, so _shufflePos = 0 means "currently at 0").
    int cur = _currentIdx;
    for (int i = 0; i < _trackCount; i++) {
        if (_shuffleOrder[i] == cur) {
            _shuffleOrder[i] = _shuffleOrder[0];
            _shuffleOrder[0] = cur;
            break;
        }
    }
    // Shuffle everything after position 0
    for (int i = _trackCount - 1; i > 1; i--) {
        int j = random(1, i + 1);
        int tmp = _shuffleOrder[i];
        _shuffleOrder[i] = _shuffleOrder[j];
        _shuffleOrder[j] = tmp;
    }
    _shufflePos = 0;
}

// Extracts the filename stem into buf[len]: strips directory and extension.
// "/Tracks/Mass Destruction.mp3" → "Mass Destruction"
// Pure pointer arithmetic — no String temporaries, no heap allocation.
void AudioManager::_titleFromPath(const String& path, char* buf, size_t len) {
    const char* src  = path.c_str();
    const char* name = strrchr(src, '/');
    name = name ? name + 1 : src;            // skip past last '/'

    const char* dot  = strrchr(name, '.');
    size_t      nlen = dot ? (size_t)(dot - name) : strlen(name);

    if (nlen >= len) nlen = len - 1;         // clamp to buffer
    memcpy(buf, name, nlen);
    buf[nlen] = '\0';
}

// Case-insensitive check for .mp3 / .flac extensions.
bool AudioManager::_isAudioFile(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".flac");
}

// ─── findTrackByName ─────────────────────────────────────────────────────────
//
// Linear scan of the playlist.  Title comparison is case-insensitive and uses
// indexOf so a query of "tartarus" matches "Tartarus.mp3", "tartarus_bgm.flac", etc.

int AudioManager::findTrackByName(const String& name) {
    String query = name;
    query.toLowerCase();
    char   buf[64];
    for (int i = 0; i < _trackCount; i++) {
        _titleFromPath(_playlist[i], buf, sizeof(buf));
        String title = buf;        // only one String alloc per iteration here
        title.toLowerCase();
        if (title.indexOf(query) >= 0) return i;
    }
    return -1;
}
