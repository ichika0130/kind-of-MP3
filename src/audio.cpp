#include "audio.h"

// ─── Global EOF callback ─────────────────────────────────────────────────────
//
// ESP32-audioI2S calls these free functions at end of file.
// We bounce them to the single AudioManager instance via a file-scope pointer.
// audio_eof_mp3 fires for MP3, FLAC, and AAC local-file playback alike.

static AudioManager* g_audiomgr = nullptr;

void audio_eof_mp3(const char* /*info*/) {
    if (g_audiomgr) g_audiomgr->_onTrackEnd();
}

// ─── Constructor ─────────────────────────────────────────────────────────────

AudioManager::AudioManager() {
    g_audiomgr = this;
    // Zero-init shuffle array
    for (int i = 0; i < PLAYLIST_MAX; i++) _shuffleOrder[i] = i;
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

    // ── I2S / Audio library ───────────────────────────────────────────────────

    _audio.setPinout(AUDIO_BCLK_PIN, AUDIO_LRC_PIN, AUDIO_DIN_PIN);
    _audio.setVolume(_volume);

    // ── Headphone detect ─────────────────────────────────────────────────────

    pinMode(HP_DETECT_PIN, INPUT_PULLUP);
    _hpInserted = (digitalRead(HP_DETECT_PIN) == HP_INSERTED_LEVEL);
    Serial.printf("[audio] headphones %s\n", _hpInserted ? "connected" : "disconnected");

    // ── Start first track (if any) ────────────────────────────────────────────

    if (_trackCount > 0) {
        _startTrack(0);
    }

    // ── Populate initial DisplayState ─────────────────────────────────────────

    state.songTitle       = getCurrentTrackTitle();
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

    state.songTitle       = getCurrentTrackTitle();
    state.songPositionSec = getCurrentPositionSec();
    state.songDurationSec = getTotalDurationSec();
    state.isPlaying       = _isPlaying && !_isPaused;
    state.playMode        = _playMode;
    state.volume          = _volume;

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

// ─── Getters ─────────────────────────────────────────────────────────────────

String AudioManager::getCurrentTrackTitle() {
    if (_trackCount == 0 || _currentIdx < 0) return "No tracks";
    return _titleFromPath(_playlist[_currentIdx]);
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
void AudioManager::_startTrack(int index) {
    if (index < 0 || index >= _trackCount) return;

    _currentIdx = index;
    _isPaused   = false;
    _isPlaying  = true;

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

// Returns the filename without its directory or extension.
// "/Tracks/Mass Destruction.mp3" → "Mass Destruction"
String AudioManager::_titleFromPath(const String& path) {
    int lastSlash = path.lastIndexOf('/');
    String name = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
    int lastDot  = name.lastIndexOf('.');
    if (lastDot > 0) name = name.substring(0, lastDot);
    return name;
}

// Case-insensitive check for .mp3 / .flac extensions.
bool AudioManager::_isAudioFile(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".flac");
}
