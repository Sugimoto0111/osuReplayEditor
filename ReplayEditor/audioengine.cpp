// clang-format off
#include "stdafx.h"
// clang-format on
#include "audioengine.hpp"

#include <string.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

#include "config.hpp"
#include "thirdparty/bass24/bass.h"
#include "thirdparty/bass24/bass_fx.h"

namespace audioengine
{
namespace
{

enum class StreamStatus { Stopped, Paused, Playing };

class BassAudioEngine : public AudioEngine
{
   public:
    bool load(const std::wstring &);
    void play() override;
    void pause() override;
    void stop() override;
    void toggle_pause() override;
    bool is_playing() override;
    bool is_paused() override;
    bool is_stopped() override;
    void jump_to(SongTime_t ms) override;
    void rel_jump(SongTime_t ms) override;
    void set_volume(float) override;
    float get_volume() override;
    void set_playback_speed(float) override;
    float get_playback_speed() override;
    SongTime_t get_time() override;

   private:
    HSTREAM stream = 0;
    HSTREAM stream_without_fx = 0;
    StreamStatus status = StreamStatus::Stopped;
    float vol = 0.0f;
    float speed = 1.0f;
};

// Fake audio engine is used when the beatmap cannot be loaded. It doesn't support playing or pausing, but stores a
// current time, volume and speed.
class FakeAudioEngine : public AudioEngine
{
   public:
    bool load(SongTime_t start_time, SongTime_t end_time);
    void play() override;
    void pause() override;
    void stop() override;
    void toggle_pause() override;
    bool is_playing() override;
    bool is_paused() override;
    bool is_stopped() override;
    void jump_to(SongTime_t ms) override;
    void rel_jump(SongTime_t ms) override;
    void set_volume(float) override;
    float get_volume() override;
    void set_playback_speed(float) override;
    float get_playback_speed() override;
    SongTime_t get_time() override;

   private:
    void sync_time_to_now();

    SongTime_t m_current_time = 0;
    SongTime_t m_start_time = 0;
    SongTime_t m_end_time = 1;
    U64 m_clock_anchor_ms = 0;
    StreamStatus m_status = StreamStatus::Stopped;
    float m_volume = 0.f;
    float m_speed = 1.f;
};

BassAudioEngine local_bass_audio_engine;
FakeAudioEngine local_fake_audio_engine;
std::wstring skin_directory;
bool bass_ready = false;

enum class HitSample { Normal = 0, Whistle = 1, Finish = 2, Clap = 3 };
std::array<std::array<HSAMPLE, 4>, 3> hit_samples{};

bool file_exists(const std::wstring &fname)
{
    std::ifstream f(fname);
    return f.good();
}

bool is_absolute_path(const std::wstring &path)
{
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    return false;
}

std::wstring join_path(const std::wstring &dir, const std::wstring &name)
{
    if (dir.empty()) return L"";
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

void free_hitsounds()
{
    for (auto &set : hit_samples) {
        for (HSAMPLE &sample : set) {
            if (sample) {
                BASS_SampleFree(sample);
                sample = 0;
            }
        }
    }
}

std::wstring find_hitsound_file(const wchar_t *set_name, const wchar_t *sample_name)
{
    const std::wstring stem = std::wstring(set_name) + L"-" + sample_name;
    const wchar_t *extensions[] = {L".wav", L".mp3", L".ogg"};
    for (const wchar_t *extension : extensions) {
        std::wstring path = join_path(skin_directory, stem + extension);
        if (file_exists(path)) return path;
    }
    return L"";
}

void load_hitsounds()
{
    free_hitsounds();
    if (skin_directory.empty()) return;

    const wchar_t *sets[] = {L"normal", L"soft", L"drum"};
    const wchar_t *samples[] = {L"hitnormal", L"hitwhistle", L"hitfinish", L"hitclap"};
    for (int set_index = 0; set_index < 3; ++set_index) {
        for (int sample_index = 0; sample_index < 4; ++sample_index) {
            const std::wstring path = find_hitsound_file(sets[set_index], samples[sample_index]);
            if (path.empty()) continue;
            hit_samples[set_index][sample_index] = BASS_SampleLoad(false, path.c_str(), 0, 0, 24, BASS_UNICODE);
        }
    }
}

void play_sample(int sample_set, HitSample sample_kind, float volume)
{
    int set_index = sample_set - 1;
    if (set_index < 0 || set_index >= 3) set_index = 0;
    const int sample_index = static_cast<int>(sample_kind);
    HSAMPLE sample = hit_samples[set_index][sample_index];
    if (!sample && set_index != 0) sample = hit_samples[0][sample_index];
    if (!sample) return;

    HCHANNEL channel = BASS_SampleGetChannel(sample, false);
    if (!channel) return;
    BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, std::clamp(volume, 0.0f, 1.0f));
    BASS_ChannelPlay(channel, true);
}

}  // namespace

AudioEngine *handle = &local_bass_audio_engine;

bool init()
{
    if (!BASS_Init(-1, 44100, BASS_DEVICE_STEREO, nullptr, nullptr)) {
        fatal("audio engine could not start");
        return false;
    }
    bass_ready = true;
    load_hitsounds();
    return true;
}

void set_skin_directory(const std::wstring &path)
{
    skin_directory = path;
    if (bass_ready) load_hitsounds();
}

void play_hitsound(int hit_sound, int sample_set, float volume)
{
    play_sample(sample_set, HitSample::Normal, volume);
    if (hit_sound & 2) play_sample(sample_set, HitSample::Whistle, volume);
    if (hit_sound & 4) play_sample(sample_set, HitSample::Finish, volume);
    if (hit_sound & 8) play_sample(sample_set, HitSample::Clap, volume);
}

void load_with_fallback(const std::wstring &fname, SongTime_t start_time, SongTime_t end_time)
{
    local_bass_audio_engine.stop();
    if (!fname.empty() && local_bass_audio_engine.load(fname)) {
        handle = &local_bass_audio_engine;
    } else {
        local_fake_audio_engine.load(start_time, end_time);
        handle = &local_fake_audio_engine;
    }
}

bool BassAudioEngine::load(const std::wstring &fname)
{
    if (stream) stop();
    std::wstring full_path = is_absolute_path(fname) ? fname : config::song_path + fname;
    if (!file_exists(full_path)) {
        not_fatal("this audio file could not be found, perhaps you configured your paths incorrectly?");
        return false;
    }
    stream =
        BASS_StreamCreateFile(false, full_path.c_str(), 0, 0, BASS_STREAM_DECODE | BASS_UNICODE | BASS_STREAM_PRESCAN);
    if (stream == 0) {
        not_fatal("audio stream could not be made (BASS_StreamCreateFile)");
        return false;
    }
    // need to keep the original stream handle so we can free it later
    stream_without_fx = stream;
    stream = BASS_FX_TempoCreate(stream, BASS_FX_TEMPO_ALGO_LINEAR);
    if (stream == 0) {
        not_fatal("audio stream could not be made (BASS_FX_TempoCreate)");
        return false;
    }
    constexpr float BASS_TRUE = 1.f;
    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, BASS_TRUE);
    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_TEMPO_OPTION_OVERLAP_MS, 4.f);
    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_TEMPO_OPTION_SEQUENCE_MS, 30.f);
    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, vol);
    speed = 1.0f;
    return true;
}

void BassAudioEngine::play()
{
    if (stream) {
        status = StreamStatus::Playing;
        BASS_ChannelPlay(stream, false);
    }
}

void BassAudioEngine::pause()
{
    if (stream) {
        status = StreamStatus::Paused;
        BASS_ChannelPause(stream);
    }
}

void BassAudioEngine::toggle_pause()
{
    if (status == StreamStatus::Paused)
        play();
    else if (status == StreamStatus::Playing)
        pause();
}

void BassAudioEngine::stop()
{
    if (stream) {
        status = StreamStatus::Stopped;
        BASS_StreamFree(stream);
        BASS_StreamFree(stream_without_fx);
        stream = 0;
        stream_without_fx = 0;
    }
}

bool BassAudioEngine::is_playing()
{
    return status == StreamStatus::Playing;
}

bool BassAudioEngine::is_paused()
{
    return status == StreamStatus::Paused;
}

bool BassAudioEngine::is_stopped()
{
    return status == StreamStatus::Stopped;
}

void BassAudioEngine::jump_to(SongTime_t ms)
{
    if (stream) {
        if (ms < 0) ms = 0;
        const QWORD byte_pos = BASS_ChannelSeconds2Bytes(stream, static_cast<double>(ms / 1000.0));
        BASS_ChannelSetPosition(stream, byte_pos, BASS_POS_BYTE);
    }
}

void BassAudioEngine::rel_jump(SongTime_t ms)
{
    jump_to(get_time() + ms);
}

void BassAudioEngine::set_volume(float value)
{
    vol = value;
    if (stream) {
        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, value);
    }
}

float BassAudioEngine::get_volume()
{
    return vol;
}

void BassAudioEngine::set_playback_speed(float value)
{
    if (stream) {
        speed = value;
        value = 100 * (value - 1);
        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_TEMPO, value);
    }
}

float BassAudioEngine::get_playback_speed()
{
    return speed;
}

SongTime_t BassAudioEngine::get_time()
{
    if (stream) {
        const QWORD byte_pos = BASS_ChannelGetPosition(stream, BASS_POS_BYTE);
        const double ms = BASS_ChannelBytes2Seconds(stream, byte_pos);
        return static_cast<SongTime_t>(ms * 1000. + 0.5);
    } else {
        return 0;
    }
}

bool FakeAudioEngine::load(SongTime_t start_time, SongTime_t end_time)
{
    m_current_time = start_time;
    m_start_time = start_time;
    m_end_time = end_time;
    m_clock_anchor_ms = GetTickCount64();
    m_status = StreamStatus::Stopped;
    return true;
}

void FakeAudioEngine::play()
{
    if (m_current_time >= m_end_time) m_current_time = m_start_time;
    m_clock_anchor_ms = GetTickCount64();
    m_status = StreamStatus::Playing;
}

void FakeAudioEngine::pause()
{
    sync_time_to_now();
    m_status = StreamStatus::Paused;
}

void FakeAudioEngine::stop()
{
    m_current_time = m_start_time;
    m_clock_anchor_ms = GetTickCount64();
    m_status = StreamStatus::Stopped;
}

void FakeAudioEngine::toggle_pause()
{
    if (m_status == StreamStatus::Playing)
        pause();
    else
        play();
}

bool FakeAudioEngine::is_playing()
{
    return m_status == StreamStatus::Playing;
}

bool FakeAudioEngine::is_paused()
{
    return m_status == StreamStatus::Paused;
}

bool FakeAudioEngine::is_stopped()
{
    return m_status == StreamStatus::Stopped;
}

void FakeAudioEngine::jump_to(SongTime_t ms)
{
    sync_time_to_now();
    if (ms < m_start_time) {
        m_current_time = m_start_time;
    } else if (ms > m_end_time) {
        m_current_time = m_end_time;
    } else {
        m_current_time = ms;
    }
    m_clock_anchor_ms = GetTickCount64();
}

void FakeAudioEngine::rel_jump(SongTime_t ms)
{
    jump_to(get_time() + ms);
}

void FakeAudioEngine::set_volume(float value)
{
    m_volume = value;
}

float FakeAudioEngine::get_volume()
{
    return m_volume;
}

void FakeAudioEngine::set_playback_speed(float value)
{
    sync_time_to_now();
    m_speed = value;
}

float FakeAudioEngine::get_playback_speed()
{
    return m_speed;
}

SongTime_t FakeAudioEngine::get_time()
{
    sync_time_to_now();
    return m_current_time;
}

void FakeAudioEngine::sync_time_to_now()
{
    if (m_status != StreamStatus::Playing) return;

    const U64 now = GetTickCount64();
    const double elapsed = static_cast<double>(now - m_clock_anchor_ms) * static_cast<double>(m_speed);
    m_current_time += static_cast<SongTime_t>(elapsed + 0.5);
    m_clock_anchor_ms = now;

    if (m_current_time >= m_end_time) {
        m_current_time = m_end_time;
        m_status = StreamStatus::Paused;
    }
}

}  // namespace audioengine
