// clang-format off
#include "stdafx.h"
// clang-format on
#include <string_view>
#include <utility>
#include <vector>
#include <fstream>

#include "accuracy_analyzer.hpp"
#include "audioengine.hpp"
#include "beatmapengine.hpp"
#include "config.hpp"
#include "lazer_judgement.hpp"
#include "osudb.hpp"
#include "relax.hpp"
#include "replayengine.hpp"
#include "texture.hpp"
#include "thirdparty/glm/glm.hpp"
#include "tools.hpp"
#include "ui.hpp"
#include "zoom_pan.hpp"

#ifndef BUILD_LABEL
#ifdef _DEBUG
#define BUILD_LABEL ("Debug " __DATE__ " " __TIME__)
#else
#error Non-debug build without a build label
#define BUILD_LABEL ("None")
#endif
#endif

namespace
{

class GraphicsHandle
{
   public:
    HWND hWnd;
    HDC hdc;
    HGLRC hrc;
};

GraphicsHandle graphics_handle;
std::wstring beatmap_file_override;

void normalize_tap_keys_for_lazer_export(replayengine::Replay &replay)
{
    for (auto &frame : replay.mut_frames()) {
        if ((frame.keys & 0x5) != 0) frame.keys |= 0x5;
        if ((frame.keys & 0xA) != 0) frame.keys |= 0xA;
    }
}

void setup_graphics(HWND hWnd)
{
    graphics_handle.hWnd = hWnd;
    graphics_handle.hdc = GetDC(hWnd);
    if (graphics_handle.hdc == nullptr) {
        fatal("Get device context (GetDC) failed");
        return;
    }
    PIXELFORMATDESCRIPTOR pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int iFormat = ChoosePixelFormat(graphics_handle.hdc, &pfd);
    if (iFormat == 0) fatal("ChoosePixelFormat failed");
    if (!SetPixelFormat(graphics_handle.hdc, iFormat, &pfd)) fatal("SetPixelFormat failed");
    graphics_handle.hrc = wglCreateContext(graphics_handle.hdc);
    if (graphics_handle.hrc == nullptr) fatal("wglCreateContext failed");
    if (!wglMakeCurrent(graphics_handle.hdc, graphics_handle.hrc)) fatal("wglMakeCurrent failed");
}

// If out_str is nullptr, then assigns out_len to the length of the string. Otherwise this call is expecting out_len to
// contain the length of out_str. It will fill out_str up to the length of s or the given length, whichever is smaller.
// Afterwards, the value out_len is set to the the number of bytes written.
void return_a_string(BYTE *out_str, INT *out_len, std::string_view s)
{
    if (out_str == nullptr) {
        *out_len = static_cast<int>(s.size());
        return;
    }
    *out_len = std::min(*out_len, static_cast<int>(s.size()));
    for (int i = 0; i < *out_len; ++i) {
        out_str[i] = static_cast<BYTE>(s[i]);
    }
}

void set_a_string(const wchar_t *in_str, INT in_len, std::string &s)
{
    s.resize(in_len);
    for (int i = 0; i < in_len; ++i) {
        if (in_str[i] < 0x20 || in_str[i] >= 127)
            s[i] = '?';
        else
            s[i] = static_cast<char>(in_str[i]);
    }
}

void pixel_to_virtual(glm::vec2 &v)
{
    const float vpw = static_cast<float>(zoom_pan.playfield().w);
    const float vph = static_cast<float>(zoom_pan.playfield().h);
    // change to GL coordinates
    v.x = (v.x - zoom_pan.playfield().x) / vpw * 2.f - 1.f;
    v.y = (v.y - zoom_pan.playfield().y) / vph * 2.f - 1.f;
    v.y = -v.y;
    // inverse of the projection matrix (to osu coords)
    zoom_pan.gl_to_osu_pixel(v);
}

}  // namespace

DLLFUN(void) GetDllBuildLabel(BYTE *out_str, INT *len)
{
    return_a_string(out_str, len, BUILD_LABEL);
}

DLLFUN(INT) Init(HWND hWnd, const wchar_t *osu_db_path, const wchar_t *song_path)
{
    constexpr int SUCCESS = 0;
    constexpr int NULL_HWND = 1;
    constexpr int AUDIO_FAILURE = 2;
    constexpr int OSUDB_FAILURE = 3;
    constexpr int REPLAYENGINE_FAILURE = 4;
    constexpr int BEATMAPENGINE_FAILURE = 5;
    constexpr int TEXTURE_FAILURE = 6;
    if (hWnd == nullptr) return NULL_HWND;
    error_message_owner = hWnd;
    setup_graphics(hWnd);
    config::osu_db_path = osu_db_path ? osu_db_path : L"";
    config::song_path = song_path ? song_path : L"";
    config::init();
    if (!audioengine::init()) return AUDIO_FAILURE;
    if (!config::osu_db_path.empty()) {
        std::ifstream db_file(config::osu_db_path);
        if (db_file.good() && !osudb::init()) {
            not_fatal("osu!.db could not be read. Online beatmap lookup will still be used when configured.");
        }
    }
    if (!replayengine::init()) return REPLAYENGINE_FAILURE;
    if (!beatmapengine::init(L"")) return BEATMAPENGINE_FAILURE;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
    glLineWidth(2.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glClearColor(.0f, .0f, .0f, 1.0f);
    zoom_pan.reset();
    zoom_pan.set_projection();
    if (!textures::init()) return TEXTURE_FAILURE;
    glColor3f(1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return SUCCESS;
}

DLLFUN(BOOL) Cleanup()
{
    config::cleanup();
    return TRUE;
}

DLLFUN(void) SetBeatmapFileOverride(const wchar_t *fname)
{
    beatmap_file_override = fname ? fname : L"";
}

DLLFUN(void) SetSkinDirectory(const wchar_t *dir)
{
    const std::wstring path = dir ? dir : L"";
    textures::set_skin_directory(path);
    audioengine::set_skin_directory(path);
}

DLLFUN(BOOL) LoadReplay(const wchar_t *fname)
{
    using ::replayengine::Replay;
    using ::replayengine::ReplayFrame;
    if (!replayengine::MutateCurrentView([&fname](const Replay &, Replay &next) { return next.read_from(fname); })) {
        return FALSE;
    }
    lazer_judgement::mark_dirty();
    std::wstring osu_path;
    std::wstring song_path;
    if (!beatmap_file_override.empty()) {
        osu_path = beatmap_file_override;
    } else if (!osudb::get_entry(replayengine::CurrentView()->metadata().beatmap_hash, osu_path, song_path)) {
        not_fatal("You do not have this beatmap. Information about the hit objects cannot be loaded.");
    }
    const std::vector<ReplayFrame> &frames = replayengine::CurrentView()->frames();
    SongTime_t replay_start = 0;
    SongTime_t replay_end = 1;
    if (!frames.empty()) {
        replay_start = frames.front().ms;
        replay_end = frames.back().ms;
    }
    beatmapengine::first_hitobject_ms = replay_start;
    beatmapengine::last_hitobject_ms = replay_end;
    if (!beatmapengine::init(osu_path)) {
        return FALSE;
    }
    const std::wstring audio_path = beatmapengine::audio_path.empty() ? song_path : beatmapengine::audio_path;
    audioengine::load_with_fallback(audio_path, replay_start - 500, replay_end + 500);
    lazer_judgement::mark_dirty();
    audioengine::handle->set_playback_speed(1.0f);
    return TRUE;
}

DLLFUN(void) OnResize(int width, int height)
{
    zoom_pan.on_resize(width, height);
}

DLLFUN(void) MouseDown(float x, float y)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_down(v);
}

DLLFUN(void) MouseUp(float x, float y)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_up(v);
}

DLLFUN(void) MouseMove(float x, float y)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_move(v);
}

DLLFUN(void) MouseDownRight(float x, float y)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_down_right(v);
}

DLLFUN(void) MouseUpRight(float x, float y)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_up_right(v);
}

DLLFUN(void) MouseWheel(float x, float y, BOOL isUp)
{
    glm::vec2 v(x, y);
    pixel_to_virtual(v);
    ui::mouse_wheel(v, isUp);
}

DLLFUN(void) Render()
{
    zoom_pan.set_projection();
    glClear(GL_COLOR_BUFFER_BIT);
    const SongTime_t t = audioengine::handle->get_time();
    beatmapengine::update_hitsounds(t, audioengine::handle->is_playing());
    beatmapengine::draw(t);
    replayengine::MutableCurrentView()->draw(t);
    ui::draw(t);
    wglSwapLayerBuffers(graphics_handle.hdc, WGL_SWAP_MAIN_PLANE);
}

DLLFUN(void) Play()
{
    audioengine::handle->play();
}

DLLFUN(void) Pause()
{
    audioengine::handle->pause();
}

DLLFUN(void) Stop()
{
    audioengine::handle->stop();
}

DLLFUN(void) TogglePause()
{
    audioengine::handle->toggle_pause();
}

DLLFUN(BOOL) IsPlaying()
{
    return audioengine::handle->is_playing();
}

DLLFUN(BOOL) IsPaused()
{
    return audioengine::handle->is_paused();
}

DLLFUN(BOOL) IsStopped()
{
    return audioengine::handle->is_stopped();
}

DLLFUN(void) JumpTo(SongTime_t ms)
{
    audioengine::handle->jump_to(ms);
}

DLLFUN(void) RelJump(SongTime_t ms)
{
    audioengine::handle->rel_jump(ms);
}

DLLFUN(void) SetVolume(float percent)
{
    audioengine::handle->set_volume(percent);
}

DLLFUN(float) GetVolume()
{
    return audioengine::handle->get_volume();
}

DLLFUN(void) SetPlaybackSpeed(float multiplier)
{
    audioengine::handle->set_playback_speed(multiplier);
}

DLLFUN(float) GetPlaybackSpeed()
{
    return audioengine::handle->get_playback_speed();
}

DLLFUN(SongTime_t) GetTime()
{
    return audioengine::handle->get_time();
}

// TRAIL_OFF = 0;
// TRAIL_RAW = 1;
// TRAIL_HITS = 2;
DLLFUN(void) SetCursorTrail(int kind)
{
    if (kind >= 0 && kind < static_cast<int>(ui::TrailMode::num_trail_modes)) {
        ui::trail_mode = static_cast<ui::TrailMode>(kind);
    }
}

DLLFUN(void) SetCursorTrailLength(SongTime_t ms)
{
    ui::trail_length = ms;
}

DLLFUN(SongTime_t) GetReplayStartMs()
{
    return beatmapengine::first_hitobject_ms;
}

DLLFUN(SongTime_t) GetReplayEndMs()
{
    return beatmapengine::last_hitobject_ms;
}

DLLFUN(float) GetBeatmapStackLeniency()
{
    return beatmapengine::stack_leniency;
}

DLLFUN(float) GetBeatmapHP()
{
    return beatmapengine::hp;
}

DLLFUN(float) GetBeatmapCS()
{
    return beatmapengine::cs;
}

DLLFUN(float) GetBeatmapOD()
{
    return beatmapengine::od;
}

DLLFUN(float) GetBeatmapAR()
{
    return beatmapengine::ar;
}

DLLFUN(float) GetBeatmapSliderMult()
{
    return beatmapengine::slider_mult;
}

DLLFUN(void) SetBeatmapStackLeniency(float value)
{
    beatmapengine::stack_leniency = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) SetBeatmapHP(float value)
{
    beatmapengine::hp = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) SetBeatmapCS(float value)
{
    beatmapengine::cs = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) SetBeatmapOD(float value)
{
    beatmapengine::od = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) SetBeatmapAR(float value)
{
    beatmapengine::ar = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) SetBeatmapSliderMult(float value)
{
    beatmapengine::slider_mult = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) PlaceMarkIn()
{
    using ::replayengine::Replay;
    replayengine::MutateCurrentView([](Replay &replay) {
        replay.place_mark_in();
        return true;
    });
}

DLLFUN(void) PlaceMarkOut()
{
    using ::replayengine::Replay;
    replayengine::MutateCurrentView([](Replay &replay) {
        replay.place_mark_out();
        return true;
    });
}

DLLFUN(void) PlaceMarkMid()
{
    using ::replayengine::Replay;
    replayengine::MutateCurrentView([](Replay &replay) {
        replay.place_mark_mid();
        return true;
    });
}

DLLFUN(void) PlaceMarkAll()
{
    using ::replayengine::Replay;
    replayengine::MutateCurrentView([](Replay &replay) {
        replay.place_mark_all();
        return true;
    });
}

DLLFUN(void) ClearMarks()
{
    using ::replayengine::Replay;
    replayengine::MutateCurrentView([](Replay &replay) {
        replay.clear_marks();
        return true;
    });
}

DLLFUN(BOOL) AreMarksMidConsistent()
{
    return replayengine::CurrentView()->are_all_marks_consistent();
}

DLLFUN(BOOL) AreMarksInOutConsistent()
{
    return replayengine::CurrentView()->are_in_out_marks_consistent();
}

DLLFUN(BOOL) SetFrameKeyPress(int mask)
{
    using ::replayengine::Replay;
    if (replayengine::CurrentView()->are_in_out_marks_consistent()) {
        const bool changed = replayengine::MutateCurrentView([mask](Replay &replay) {
            for (auto &frame : replay.mut_selection()) {
                frame.keys = mask;
            }
            return true;
        });
        if (changed) lazer_judgement::mark_dirty();
        return changed;
    } else {
        return FALSE;
    }
}

DLLFUN(INT) GetReplayFrameCount()
{
    return static_cast<INT>(replayengine::CurrentView()->frames().size());
}

DLLFUN(INT) GetReplayFrames(INT *times, INT *keys, INT capacity)
{
    if (times == nullptr || keys == nullptr || capacity <= 0) return 0;

    const std::vector<replayengine::ReplayFrame> &frames = replayengine::CurrentView()->frames();
    INT count = static_cast<INT>(frames.size());
    if (count > capacity) count = capacity;

    for (INT i = 0; i < count; ++i) {
        times[i] = static_cast<INT>(frames[i].ms);
        keys[i] = frames[i].keys;
    }
    return count;
}

DLLFUN(INT) GetTimelineHitObjectCount()
{
    INT count = 0;
    for (const auto &obj : beatmapengine::hitobjects) {
        switch (obj.hitobject_type) {
            case HitObjectType::Circle:
            case HitObjectType::Slider:
            case HitObjectType::Spinner:
                ++count;
                break;
            default:
                break;
        }
    }
    return count;
}

DLLFUN(INT) GetTimelineHitObjects(INT *start_times, INT *end_times, INT *kinds, INT capacity)
{
    if (start_times == nullptr || end_times == nullptr || kinds == nullptr || capacity <= 0) return 0;

    INT count = 0;
    for (const auto &obj : beatmapengine::hitobjects) {
        INT kind = 0;
        switch (obj.hitobject_type) {
            case HitObjectType::Circle:
                kind = 1;
                break;
            case HitObjectType::Slider:
                kind = 2;
                break;
            case HitObjectType::Spinner:
                kind = 3;
                break;
            default:
                continue;
        }

        if (count >= capacity) break;
        start_times[count] = static_cast<INT>(obj.start);
        end_times[count] = static_cast<INT>(obj.end);
        kinds[count] = kind;
        ++count;
    }
    return count;
}

DLLFUN(BOOL) SetFrameKeyPressRange(INT start_ms, INT end_ms, INT key_mask, BOOL pressed)
{
    if (key_mask == 0) return FALSE;
    if (start_ms > end_ms) std::swap(start_ms, end_ms);

    std::vector<replayengine::ReplayFrame> &frames = replayengine::MutableCurrentView()->mut_frames();
    if (frames.empty()) return FALSE;

    const auto apply_to_frame = [key_mask, pressed](replayengine::ReplayFrame &frame) {
        const int before = frame.keys;
        if (pressed) {
            frame.keys |= key_mask;
        } else {
            frame.keys &= ~key_mask;
        }
        return frame.keys != before;
    };

    bool changed = false;
    bool touched_frame = false;
    size_t closest_index = 0;
    I64 closest_distance = -1;
    const I64 midpoint = static_cast<I64>(start_ms) + (static_cast<I64>(end_ms) - start_ms) / 2;

    for (size_t i = 0; i < frames.size(); ++i) {
        const I64 frame_ms = static_cast<I64>(frames[i].ms);
        const I64 distance = frame_ms > midpoint ? frame_ms - midpoint : midpoint - frame_ms;
        if (closest_distance < 0 || distance < closest_distance) {
            closest_distance = distance;
            closest_index = i;
        }

        if (frame_ms < start_ms || frame_ms > end_ms) continue;
        touched_frame = true;
        changed = apply_to_frame(frames[i]) || changed;
    }

    if (!touched_frame) {
        changed = apply_to_frame(frames[closest_index]) || changed;
    }

    if (changed) lazer_judgement::mark_dirty();
    return changed;
}

DLLFUN(BOOL) LoadSave(const wchar_t *saveFileName)
{
    return FALSE;
}

DLLFUN(BOOL) WriteSave(const wchar_t *saveFileName)
{
    return FALSE;
}

DLLFUN(BOOL) ExportAsOsr(const wchar_t *osrFileName)
{
    replayengine::Replay replay = *replayengine::CurrentView();
    normalize_tap_keys_for_lazer_export(replay);
    return replay.write_to(osrFileName);
}

DLLFUN(BOOL) ExportAsOsrWithLazerKeys(const wchar_t *osrFileName)
{
    replayengine::Replay replay = *replayengine::CurrentView();
    normalize_tap_keys_for_lazer_export(replay);
    return replay.write_to(osrFileName);
}

DLLFUN(void) VisualMapInvert(BOOL value)
{
    beatmapengine::hitobjects_inverted = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) InvertCursorData()
{
    using ::replayengine::Replay;
    if (replayengine::MutateCurrentView([](Replay &replay) {
        replay.invert_replay_frames();
        return true;
    })) {
        lazer_judgement::mark_dirty();
    }
}

DLLFUN(BYTE) Replay_GetGamemode()
{
    return replayengine::CurrentView()->metadata().game_mode;
}

DLLFUN(INT32) Replay_GetVersion()
{
    return replayengine::CurrentView()->metadata().version;
}

DLLFUN(INT64) Replay_GetPlayTimestamp()
{
    return replayengine::CurrentView()->metadata().play_timestamp;
}

DLLFUN(void) Replay_GetPlayerName(BYTE *out_str, INT *len)
{
    return_a_string(out_str, len, replayengine::CurrentView()->metadata().player_name);
}

DLLFUN(INT16) Replay_GetNum300()
{
    return replayengine::CurrentView()->metadata().num_300;
}

DLLFUN(INT16) Replay_GetNum100()
{
    return replayengine::CurrentView()->metadata().num_100;
}

DLLFUN(INT16) Replay_GetNum50()
{
    return replayengine::CurrentView()->metadata().num_50;
}

DLLFUN(INT16) Replay_GetNumGeki()
{
    return replayengine::CurrentView()->metadata().num_geki;
}

DLLFUN(INT16) Replay_GetNumKatu()
{
    return replayengine::CurrentView()->metadata().num_katu;
}

DLLFUN(INT16) Replay_GetNumMiss()
{
    return replayengine::CurrentView()->metadata().num_miss;
}

DLLFUN(INT32) Replay_GetTotalScore()
{
    return replayengine::CurrentView()->metadata().total_score;
}

DLLFUN(INT16) Replay_GetMaxCombo()
{
    return replayengine::CurrentView()->metadata().max_combo;
}

DLLFUN(BOOL) Replay_GetFullCombo()
{
    return replayengine::CurrentView()->metadata().full_combo;
}

DLLFUN(UINT32) Replay_GetMods()
{
    return replayengine::CurrentView()->metadata().mods;
}

DLLFUN(void) Replay_SetVersion(INT value)
{
    replayengine::MutableCurrentView()->mut_metadata().version = value;
}

DLLFUN(void) Replay_SetPlayTimestamp(INT64 value)
{
    replayengine::MutableCurrentView()->mut_metadata().play_timestamp = value;
}

DLLFUN(void) Replay_SetGamemode(BYTE value)
{
    replayengine::MutableCurrentView()->mut_metadata().game_mode = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) Replay_SetPlayerName(const wchar_t *str, INT len)
{
    set_a_string(str, len, replayengine::MutableCurrentView()->mut_metadata().player_name);
}

DLLFUN(void) Replay_SetNum300(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_300 = value;
}

DLLFUN(void) Replay_SetNum100(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_100 = value;
}

DLLFUN(void) Replay_SetNum50(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_50 = value;
}

DLLFUN(void) Replay_SetNumGeki(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_geki = value;
}

DLLFUN(void) Replay_SetNumKatu(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_katu = value;
}

DLLFUN(void) Replay_SetNumMiss(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().num_miss = value;
}

DLLFUN(void) Replay_SetTotalScore(INT32 value)
{
    replayengine::MutableCurrentView()->mut_metadata().total_score = value;
}

DLLFUN(void) Replay_SetMaxCombo(INT16 value)
{
    replayengine::MutableCurrentView()->mut_metadata().max_combo = value;
}

DLLFUN(void) Replay_SetFullCombo(BOOL value)
{
    replayengine::MutableCurrentView()->mut_metadata().full_combo = value;
}

DLLFUN(void) Replay_SetMods(UINT32 value)
{
    replayengine::MutableCurrentView()->mut_metadata().mods = value;
    lazer_judgement::mark_dirty();
}

DLLFUN(void) ResetPanZoom()
{
    zoom_pan.reset();
}

DLLFUN(void) ZoomIn()
{
    zoom_pan.mut_zoom() += 5.0;
    zoom_pan.set_dirty();
}

DLLFUN(void) ZoomOut()
{
    zoom_pan.mut_zoom() -= 5.0;
    zoom_pan.set_dirty();
}

DLLFUN(BOOL) Undo()
{
    const bool changed = replayengine::Undo();
    if (changed) lazer_judgement::mark_dirty();
    return changed;
}

DLLFUN(void) MakeUndoSnapshot()
{
    replayengine::DuplicateCurrentView();
}

DLLFUN(BOOL) Redo()
{
    const bool changed = replayengine::Redo();
    if (changed) lazer_judgement::mark_dirty();
    return changed;
}

DLLFUN(void)
AnalyzeAccuracy(BOOL do_trace, int *num_300, int *num_100, int *num_50, int *num_miss, float *accuracy,
                float *avg_early, float *avg_late, float *unstable_rate)
{
    accuracy_analyzer::Stats stats;
    accuracy_analyzer::analyze(&stats, do_trace);
    *num_300 = stats.num_300;
    *num_100 = stats.num_100;
    *num_50 = stats.num_50;
    *num_miss = stats.num_miss;
    *accuracy = stats.accuracy;
    *avg_early = stats.avg_early;
    *avg_late = stats.avg_late;
    *unstable_rate = stats.unstable_rate;
}

static INT timeline_marker_kind(const lazer_judgement::JudgedObject &obj)
{
    if (lazer_judgement::is_miss_like(obj.result)) return 0;
    const int points = lazer_judgement::aggregate_points(obj);
    if (points == 50) return 50;
    if (points == 100) return 100;
    return -1;
}

DLLFUN(INT) GetTimelineJudgementMarkerCount()
{
    INT count = 0;
    for (const auto &obj : lazer_judgement::judged_objects()) {
        if (timeline_marker_kind(obj) != -1) ++count;
    }
    return count;
}

DLLFUN(INT) GetTimelineJudgementMarkers(INT *times, INT *kinds, INT capacity)
{
    if (times == nullptr || kinds == nullptr || capacity <= 0) return 0;

    INT count = 0;
    for (const auto &obj : lazer_judgement::judged_objects()) {
        const INT kind = timeline_marker_kind(obj);
        if (kind == -1) continue;
        if (count >= capacity) break;
        times[count] = obj.time;
        kinds[count] = kind;
        ++count;
    }
    return count;
}

static bool hit_error_bar_candidate(const lazer_judgement::JudgedObject &obj)
{
    if (lazer_judgement::is_miss_like(obj.result)) return false;
    switch (obj.object_type) {
        case lazer_judgement::JudgedObjectType::Circle:
        case lazer_judgement::JudgedObjectType::SliderHead:
            break;
        default:
            return false;
    }

    const int points = lazer_judgement::aggregate_points(obj);
    return points == 300 || points == 100 || points == 50;
}

DLLFUN(INT) GetHitErrorMarkerCount()
{
    INT count = 0;
    for (const auto &obj : lazer_judgement::judged_objects()) {
        if (hit_error_bar_candidate(obj)) ++count;
    }
    return count;
}

DLLFUN(INT) GetHitErrorMarkers(INT *times, INT *errors, INT *points, INT capacity)
{
    if (times == nullptr || errors == nullptr || points == nullptr || capacity <= 0) return 0;

    INT count = 0;
    for (const auto &obj : lazer_judgement::judged_objects()) {
        if (!hit_error_bar_candidate(obj)) continue;
        if (count >= capacity) break;

        times[count] = obj.time;
        errors[count] = obj.hit_error;
        points[count] = lazer_judgement::aggregate_points(obj);
        ++count;
    }
    return count;
}

DLLFUN(INT) NextMiss()
{
    return accuracy_analyzer::next_hitobject(
        [](const lazer_judgement::JudgedObject &obj) { return lazer_judgement::is_miss_like(obj.result); });
}

DLLFUN(INT) Next50()
{
    return accuracy_analyzer::next_hitobject(
        [](const lazer_judgement::JudgedObject &obj) { return lazer_judgement::aggregate_points(obj) == 50; });
}

DLLFUN(INT) Next100()
{
    return accuracy_analyzer::next_hitobject(
        [](const lazer_judgement::JudgedObject &obj) { return lazer_judgement::aggregate_points(obj) == 100; });
}

DLLFUN(INT) Next300()
{
    return accuracy_analyzer::next_hitobject(
        [](const lazer_judgement::JudgedObject &obj) { return lazer_judgement::aggregate_points(obj) == 300; });
}

DLLFUN(INT) NextHitObject()
{
    return accuracy_analyzer::next_hitobject([](const lazer_judgement::JudgedObject &) { return true; });
}

DLLFUN(BOOL) GetHitInfo(INT index, INT *kind, BOOL *is_miss, INT *hit_error, INT *points)
{
    bool miss = false;
    const bool ok = lazer_judgement::get_judged_object_info(index, kind, &miss, hit_error, points);
    *is_miss = miss;
    return ok;
}

DLLFUN(BOOL) JudgementHasUnsupportedMods()
{
    return lazer_judgement::has_unsupported_mods();
}

DLLFUN(BOOL) AnalyzeAndApplyReplayMetadata()
{
    return lazer_judgement::apply_to_replay_metadata();
}

DLLFUN(void) SetTool(INT tool)
{
    if (tool >= 0 && tool < static_cast<int>(tool::ToolType::num_tools)) {
        tool::CurrentToolType(static_cast<tool::ToolType>(tool));
    }
}

DLLFUN(void) SetBrushRadius(INT brush_radius)
{
    tool::brush_radius = static_cast<float>(brush_radius);
}

DLLFUN(void) SetEditorWindow(INT start_ms, INT end_ms)
{
    tool::SetEditorWindow(start_ms, end_ms);
}

DLLFUN(void) RelaxRecalculateAllHits()
{
    relax::recalculate_all_hits();
    lazer_judgement::mark_dirty();
}

DLLFUN(void) RelaxRecalculateHitsInSelection()
{
    relax::recalculate_hits_in_selection();
    lazer_judgement::mark_dirty();
}
