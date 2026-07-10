// clang-format off
#include "stdafx.h"
// clang-format on

#include "lazer_judgement.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "audioengine.hpp"
#include "beatmapengine.hpp"
#include "replayengine.hpp"

namespace lazer_judgement
{
namespace
{

constexpr uint32_t MOD_HARD_ROCK = 16;
constexpr uint32_t MOD_DOUBLE_TIME = 64;
constexpr uint32_t SUPPORTED_MODS = MOD_HARD_ROCK | MOD_DOUBLE_TIME;
constexpr float PLAYFIELD_HEIGHT = 384.f;
constexpr float OBJECT_RADIUS = 64.f;
constexpr float BROKEN_GAMEFIELD_ROUNDING_ALLOWANCE = 1.00041f;
constexpr float SLIDER_FOLLOW_AREA = 2.4f;
constexpr float SLIDER_TRACKING_TOLERANCE = 0.0f;
constexpr float LEGACY_SLIDER_PATH_FALLBACK_MIN_MARGIN = 3.0f;
constexpr float LEGACY_SLIDER_PATH_FALLBACK_TOLERANCE = 3.0f;
constexpr double TAIL_LENIENCY = -36.0;
constexpr double SPINNER_DURATION_ERROR = 0.0001;
constexpr int SPINNER_BONUS_SPINS_GAP = 2;
constexpr SongTime_t TRACKING_SAMPLE_STEP = 1;

enum class PressAction { None, Left, Right };

struct HitWindows {
    double great = 0.0;
    double ok = 0.0;
    double meh = 0.0;
    double miss = 400.0;
};

struct SliderEvent {
    JudgedObjectType type = JudgedObjectType::SliderTick;
    double time = 0.0;
    double judge_time = 0.0;
    double path_progress = 0.0;
};

struct MutableStats {
    JudgementStats stats;
    double current_base_score = 0.0;
    double maximum_base_score = 0.0;
    int current_accuracy_judgement_count = 0;
    int maximum_accuracy_judgement_count = 0;
    double current_combo_portion = 0.0;
    double maximum_combo_portion = 0.0;
    double current_bonus_portion = 0.0;
    int combo = 0;
};

struct ReplaySample {
    glm::vec2 p;
    SongTime_t ms = 0;
    int keys = 0;
    bool pressed_mouse1() const
    {
        return keys & 1;
    }
    bool pressed_mouse2() const
    {
        return keys & 2;
    }
    bool pressed_key1() const
    {
        return keys & 4;
    }
    bool pressed_key2() const
    {
        return keys & 8;
    }
};

bool dirty = true;
JudgementStats cached_stats;
std::vector<JudgedObject> cached_objects;

double difficulty_range(double value, double low, double mid, double high)
{
    if (value > 5.0) return mid + (high - mid) * (value - 5.0) / 5.0;
    if (value < 5.0) return mid - (mid - low) * (5.0 - value) / 5.0;
    return mid;
}

bool has_hr()
{
    return (replayengine::CurrentView()->metadata().mods & MOD_HARD_ROCK) != 0;
}

bool has_dt()
{
    return (replayengine::CurrentView()->metadata().mods & MOD_DOUBLE_TIME) != 0;
}

double adjusted_od()
{
    double value = beatmapengine::base_od;
    if (has_hr()) value = std::min(value * 1.4, 10.0);
    return std::round(value * 1000.0) / 1000.0;
}

double adjusted_cs()
{
    double value = beatmapengine::base_cs;
    if (has_hr()) value = std::min(value * 1.3, 10.0);
    return std::round(value * 1000.0) / 1000.0;
}

HitWindows make_hit_windows()
{
    const double od = adjusted_od();
    return {
        std::floor(difficulty_range(od, 80.0, 50.0, 20.0)) - 0.5,
        std::floor(difficulty_range(od, 140.0, 100.0, 60.0)) - 0.5,
        std::floor(difficulty_range(od, 200.0, 150.0, 100.0)) - 0.5,
        400.0,
    };
}

float circle_radius()
{
    const float scale =
        (1.0f - 0.7f * static_cast<float>((adjusted_cs() - 5.0) / 5.0)) / 2.0f *
        BROKEN_GAMEFIELD_ROUNDING_ALLOWANCE;
    return OBJECT_RADIUS * scale;
}

glm::vec2 apply_mod_position(glm::vec2 pos)
{
    if (has_hr() || beatmapengine::hitobjects_inverted) pos.y = PLAYFIELD_HEIGHT - pos.y;
    return pos;
}

template <typename T>
bool pressed_left(const T& frame)
{
    return frame.pressed_mouse1() || frame.pressed_key1();
}

template <typename T>
bool pressed_right(const T& frame)
{
    return frame.pressed_mouse2() || frame.pressed_key2();
}

template <typename T>
bool pressed_any(const T& frame)
{
    return pressed_left(frame) || pressed_right(frame);
}

PressAction press_action(const replayengine::ReplayFrame& prev, const replayengine::ReplayFrame& curr)
{
    if (!pressed_left(prev) && pressed_left(curr)) return PressAction::Left;
    if (!pressed_right(prev) && pressed_right(curr)) return PressAction::Right;
    return PressAction::None;
}

template <typename T>
bool is_action_pressed(const T& frame, PressAction action)
{
    switch (action) {
        case PressAction::Left:
            return pressed_left(frame);
        case PressAction::Right:
            return pressed_right(frame);
        default:
            return false;
    }
}

PressAction other_action(PressAction action)
{
    switch (action) {
        case PressAction::Left:
            return PressAction::Right;
        case PressAction::Right:
            return PressAction::Left;
        default:
            return PressAction::None;
    }
}

ReplaySample sample_replay_at(const std::vector<replayengine::ReplayFrame>& frames, SongTime_t time)
{
    if (frames.empty()) return ReplaySample{};

    auto next = std::upper_bound(frames.begin(), frames.end(), time,
                                 [](SongTime_t lhs, const replayengine::ReplayFrame& rhs) { return lhs < rhs.ms; });

    if (next == frames.begin()) {
        return ReplaySample{next->p, time, 0};
    }

    const auto current = next - 1;
    if (next == frames.end() || next->ms <= current->ms) {
        return ReplaySample{current->p, time, current->keys};
    }

    const float progress = static_cast<float>(time - current->ms) / static_cast<float>(next->ms - current->ms);
    return ReplaySample{glm::mix(current->p, next->p, glm::clamp(progress, 0.0f, 1.0f)), time, current->keys};
}

HitResult result_for(double offset, const HitWindows& windows)
{
    offset = std::abs(offset);
    if (offset <= windows.great) return HitResult::Great;
    if (offset <= windows.ok) return HitResult::Ok;
    if (offset <= windows.meh) return HitResult::Meh;
    if (offset <= windows.miss) return HitResult::Miss;
    return HitResult::None;
}

bool is_hit(HitResult result)
{
    switch (result) {
        case HitResult::Great:
        case HitResult::Ok:
        case HitResult::Meh:
        case HitResult::SmallTickHit:
        case HitResult::LargeTickHit:
        case HitResult::SliderTailHit:
        case HitResult::SmallBonus:
        case HitResult::LargeBonus:
            return true;
        default:
            return false;
    }
}

bool affects_combo(HitResult result)
{
    switch (result) {
        case HitResult::Miss:
        case HitResult::Meh:
        case HitResult::Ok:
        case HitResult::Great:
        case HitResult::LargeTickMiss:
        case HitResult::LargeTickHit:
        case HitResult::SliderTailHit:
            return true;
        default:
            return false;
    }
}

bool affects_accuracy(HitResult result)
{
    switch (result) {
        case HitResult::Miss:
        case HitResult::Meh:
        case HitResult::Ok:
        case HitResult::Great:
        case HitResult::SmallTickMiss:
        case HitResult::SmallTickHit:
        case HitResult::LargeTickMiss:
        case HitResult::LargeTickHit:
        case HitResult::SliderTailHit:
            return true;
        default:
            return false;
    }
}

bool is_scorable(HitResult result)
{
    return affects_accuracy(result);
}

bool is_bonus(HitResult result)
{
    return result == HitResult::SmallBonus || result == HitResult::LargeBonus;
}

bool affects_ui_bucket(JudgedObjectType type)
{
    return type == JudgedObjectType::Circle || type == JudgedObjectType::SliderHead ||
           type == JudgedObjectType::Spinner;
}

template <typename T>
T clamp_to(T value, T low, T high)
{
    return std::min(std::max(value, low), high);
}

int base_score_for(HitResult result)
{
    switch (result) {
        case HitResult::SmallTickHit:
            return 10;
        case HitResult::LargeTickHit:
            return 30;
        case HitResult::SliderTailHit:
            return 150;
        case HitResult::Meh:
            return 50;
        case HitResult::Ok:
            return 100;
        case HitResult::Great:
            return 300;
        case HitResult::SmallBonus:
            return 10;
        case HitResult::LargeBonus:
            return 50;
        default:
            return 0;
    }
}

HitResult max_result_for(JudgedObjectType type)
{
    switch (type) {
        case JudgedObjectType::SpinnerTick:
            return HitResult::SmallBonus;
        case JudgedObjectType::SpinnerBonus:
            return HitResult::LargeBonus;
        case JudgedObjectType::SliderTick:
        case JudgedObjectType::SliderRepeat:
            return HitResult::LargeTickHit;
        case JudgedObjectType::SliderTail:
            return HitResult::SliderTailHit;
        default:
            return HitResult::Great;
    }
}

HitResult miss_result_for(JudgedObjectType type)
{
    switch (type) {
        case JudgedObjectType::SliderTick:
        case JudgedObjectType::SliderRepeat:
            return HitResult::LargeTickMiss;
        case JudgedObjectType::SliderTail:
        case JudgedObjectType::SpinnerTick:
        case JudgedObjectType::SpinnerBonus:
            return HitResult::IgnoreMiss;
        default:
            return HitResult::Miss;
    }
}

const char* result_name(HitResult result)
{
    switch (result) {
        case HitResult::Miss:
            return "Miss";
        case HitResult::Meh:
            return "Meh";
        case HitResult::Ok:
            return "Ok";
        case HitResult::Great:
            return "Great";
        case HitResult::SmallTickMiss:
            return "SmallTickMiss";
        case HitResult::SmallTickHit:
            return "SmallTickHit";
        case HitResult::LargeTickMiss:
            return "LargeTickMiss";
        case HitResult::LargeTickHit:
            return "LargeTickHit";
        case HitResult::SliderTailHit:
            return "SliderTailHit";
        case HitResult::SmallBonus:
            return "SmallBonus";
        case HitResult::LargeBonus:
            return "LargeBonus";
        case HitResult::IgnoreMiss:
            return "IgnoreMiss";
        default:
            return "None";
    }
}

const char* object_type_name(JudgedObjectType type)
{
    switch (type) {
        case JudgedObjectType::Circle:
            return "Circle";
        case JudgedObjectType::SliderHead:
            return "SliderHead";
        case JudgedObjectType::SliderTick:
            return "SliderTick";
        case JudgedObjectType::SliderRepeat:
            return "SliderRepeat";
        case JudgedObjectType::SliderTail:
            return "SliderTail";
        case JudgedObjectType::Spinner:
            return "Spinner";
        case JudgedObjectType::SpinnerTick:
            return "SpinnerTick";
        case JudgedObjectType::SpinnerBonus:
            return "SpinnerBonus";
        default:
            return "Unknown";
    }
}

template <typename T>
size_t lower_frame_index(const std::vector<T>& frames, SongTime_t ms)
{
    return static_cast<size_t>(std::lower_bound(frames.begin(), frames.end(), ms, CmpMs<T>()) - frames.begin());
}

void push_result(std::vector<JudgedObject>& out, int source_index, JudgedObjectType type, HitResult result,
                 SongTime_t time, int hit_error)
{
    out.push_back({source_index, type, result, time, hit_error});
}

bool start_time_ordered_policy_blocks(const std::vector<JudgedObject>& previous_results, int source_index,
                                      SongTime_t candidate_time)
{
    if (source_index < 0 || source_index >= static_cast<int>(beatmapengine::hitobjects.size())) return false;

    const auto& target = beatmapengine::hitobjects[source_index];
    const JudgedObject* blocking_object = nullptr;
    int blocking_source_index = -1;

    for (const auto& previous : previous_results) {
        if (previous.object_type != JudgedObjectType::Circle && previous.object_type != JudgedObjectType::SliderHead)
            continue;
        if (previous.source_hitobject_index < 0 ||
            previous.source_hitobject_index >= static_cast<int>(beatmapengine::hitobjects.size()))
            continue;

        const auto& previous_source = beatmapengine::hitobjects[previous.source_hitobject_index];
        if (previous_source.start >= target.start) continue;

        blocking_object = &previous;
        blocking_source_index = previous.source_hitobject_index;
    }

    if (blocking_object == nullptr || blocking_source_index < 0) return false;

    const auto& blocking_source = beatmapengine::hitobjects[blocking_source_index];
    return blocking_object->time > candidate_time && candidate_time < blocking_source.start;
}

std::optional<JudgedObject> judge_press_target(const std::vector<replayengine::ReplayFrame>& frames,
                                               std::vector<int>& consumed_press_frames, int source_index,
                                               JudgedObjectType type, glm::vec2 object_pos, SongTime_t object_time,
                                               const HitWindows& windows, float radius, PressAction* out_action,
                                               size_t* out_frame_index,
                                               const std::vector<JudgedObject>& previous_results)
{
    if (frames.size() < 2) {
        return JudgedObject{source_index, type, miss_result_for(type), object_time + static_cast<SongTime_t>(windows.meh),
                            0};
    }

    size_t index = lower_frame_index(frames, object_time - static_cast<SongTime_t>(windows.miss));
    if (index == 0) index = 1;
    for (; index < frames.size(); ++index) {
        const auto& curr = frames[index];
        if (curr.ms > object_time + windows.meh) break;
        if (consumed_press_frames[index] >= 0) continue;
        const auto& prev = frames[index - 1];
        const PressAction action = press_action(prev, curr);
        if (action == PressAction::None) continue;
        if (glm::distance(object_pos, curr.p) > radius) continue;
        const HitResult result = result_for(curr.ms - object_time, windows);
        if (result == HitResult::None) continue;
        if (start_time_ordered_policy_blocks(previous_results, source_index, curr.ms)) continue;
        consumed_press_frames[index] = source_index;
        if (out_action) *out_action = action;
        if (out_frame_index) *out_frame_index = index;
        return JudgedObject{source_index, type, result, curr.ms, curr.ms - object_time};
    }

    return JudgedObject{source_index, type, miss_result_for(type), object_time + static_cast<SongTime_t>(windows.meh), 0};
}

glm::vec2 slider_position_at_progress(const hitobject_t& obj, double progress)
{
    if (!obj.slider) return apply_mod_position(obj.pos);
    glm::vec2 pos = obj.pos;
    obj.slider->position_at_progress(static_cast<float>(clamp_to(progress, 0.0, 1.0)), pos);
    return apply_mod_position(pos);
}

glm::vec2 time_slider_position_at_progress(const hitobject_t& obj, double progress)
{
    if (!obj.slider) return apply_mod_position(obj.pos);

    glm::vec2 pos = obj.pos;
    const SongTime_t span_duration = std::max(1, static_cast<SongTime_t>(obj.slider->duration()));
    SongTime_t offset = static_cast<SongTime_t>(clamp_to(progress, 0.0, 1.0) * span_duration);
    if (progress >= 1.0 && offset > 0) --offset;
    const SongTime_t t = obj.start + offset;
    if (!obj.slider->position_at_time(t, obj.start, obj.start + span_duration, pos)) {
        obj.slider->ball_position_at_time(t, obj.start, obj.start + span_duration, pos);
    }
    return apply_mod_position(pos);
}

glm::vec2 slider_ball_position_at(const hitobject_t& obj, SongTime_t time)
{
    if (!obj.slider) return apply_mod_position(obj.pos);

    const double span_duration = std::max(1.0, static_cast<double>(obj.slider->duration()));
    const double total_duration = span_duration * std::max(1, obj.slider->repeat);
    const double elapsed = clamp_to(static_cast<double>(time - obj.start), 0.0, total_duration);
    int span_index = static_cast<int>(std::floor(elapsed / span_duration));
    double span_elapsed = elapsed - span_index * span_duration;

    if (span_index >= obj.slider->repeat) {
        span_index = obj.slider->repeat - 1;
        span_elapsed = span_duration;
    }

    double progress = span_elapsed / span_duration;
    if ((span_index % 2) == 1) progress = 1.0 - progress;
    return slider_position_at_progress(obj, progress);
}

glm::vec2 legacy_slider_ball_position_at(const hitobject_t& obj, SongTime_t time)
{
    if (!obj.slider) return apply_mod_position(obj.pos);

    const double span_duration = std::max(1.0, static_cast<double>(obj.slider->duration()));
    const double total_duration = span_duration * std::max(1, obj.slider->repeat);
    const double elapsed = clamp_to(static_cast<double>(time - obj.start), 0.0, total_duration);
    int span_index = static_cast<int>(std::floor(elapsed / span_duration));
    double span_elapsed = elapsed - span_index * span_duration;

    if (span_index >= obj.slider->repeat) {
        span_index = obj.slider->repeat - 1;
        span_elapsed = span_duration;
    }

    double progress = span_elapsed / span_duration;
    if ((span_index % 2) == 1) progress = 1.0 - progress;
    return time_slider_position_at_progress(obj, progress);
}

bool legacy_expanded_tracking_at(const std::vector<replayengine::ReplayFrame>& frames, const hitobject_t& obj,
                                 SongTime_t time, float radius)
{
    const ReplaySample sample = sample_replay_at(frames, time);
    if (!pressed_any(sample)) return false;

    const float follow_radius = radius * SLIDER_FOLLOW_AREA;
    const float current_distance = glm::distance(slider_ball_position_at(obj, time), sample.p);
    if (current_distance - follow_radius <= LEGACY_SLIDER_PATH_FALLBACK_MIN_MARGIN) return false;

    const float legacy_distance = glm::distance(legacy_slider_ball_position_at(obj, time), sample.p);
    return legacy_distance <= follow_radius + LEGACY_SLIDER_PATH_FALLBACK_TOLERANCE;
}

std::vector<SliderEvent> generate_slider_events(const hitobject_t& obj)
{
    std::vector<SliderEvent> events;
    if (!obj.slider) return events;

    events.push_back({JudgedObjectType::SliderHead, static_cast<double>(obj.start), static_cast<double>(obj.start), 0.0});

    const double span_duration = std::max(1.0, static_cast<double>(obj.slider->duration()));
    const double length = clamp_to(static_cast<double>(obj.slider->pixel_length), 0.0, 100000.0);
    double last_tick_or_repeat_time = static_cast<double>(obj.start);
    if (length > 0.0 && beatmapengine::base_slider_tick_rate > 0.0f) {
        const double beat_length = beatmapengine::beat_length_at(obj.start);
        const double tick_distance =
            clamp_to(static_cast<double>(obj.slider->velocity) * beat_length / beatmapengine::base_slider_tick_rate, 0.0,
                     length);
        const double min_distance_from_end = static_cast<double>(obj.slider->velocity) * 10.0;

        if (tick_distance > 0.0 && std::isfinite(tick_distance)) {
            for (int span = 0; span < obj.slider->repeat; ++span) {
                const double span_start = obj.start + span * span_duration;
                const bool reversed = (span % 2) == 1;
                for (double d = tick_distance; d <= length; d += tick_distance) {
                    if (d >= length - min_distance_from_end) break;
                    const double path_progress = d / length;
                    const double time_progress = reversed ? 1.0 - path_progress : path_progress;
                    const double time = span_start + time_progress * span_duration;
                    events.push_back({JudgedObjectType::SliderTick, time, time, path_progress});
                    last_tick_or_repeat_time = std::max(last_tick_or_repeat_time, time);
                    if (d + tick_distance == d) break;
                }

                if (span < obj.slider->repeat - 1) {
                    const double time = span_start + span_duration;
                    events.push_back({JudgedObjectType::SliderRepeat, time, time,
                                      ((span + 1) % 2) != 0 ? 1.0 : 0.0});
                    last_tick_or_repeat_time = std::max(last_tick_or_repeat_time, time);
                }
            }
        }
    }

    const double tail_time = obj.end;
    const double tail_activation_time =
        std::max(std::max(static_cast<double>(obj.start), tail_time + TAIL_LENIENCY), last_tick_or_repeat_time);
    events.push_back({JudgedObjectType::SliderTail, tail_time, tail_activation_time,
                      (obj.slider->repeat % 2) != 0 ? 1.0 : 0.0});

    std::stable_sort(events.begin(), events.end(), [](const SliderEvent& lhs, const SliderEvent& rhs) {
        if (lhs.judge_time == rhs.judge_time) return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
        return lhs.judge_time < rhs.judge_time;
    });
    return events;
}

class SliderTracker
{
   public:
    SliderTracker(const std::vector<replayengine::ReplayFrame>& frames, const hitobject_t& obj, PressAction head_action,
                  SongTime_t start_time, bool tracking_at_start, bool accept_any_key_at_start)
        : frames(frames),
          obj(obj),
          head_action(head_action),
          frame_index(lower_frame_index(frames, start_time)),
          last_sample_time(start_time),
          tracking(tracking_at_start),
          accept_any_key(accept_any_key_at_start || head_action == PressAction::None)
    {
        while (frame_index < frames.size() && frames[frame_index].ms <= last_sample_time) ++frame_index;

        if (!accept_any_key) {
            const ReplaySample prev = sample_replay_at(frames, std::max<SongTime_t>(obj.start, start_time - 1));
            accept_any_key = !is_action_pressed(prev, other_action(head_action));
        }
    }

    bool update_to(SongTime_t time, float radius)
    {
        scan_until(time, radius, nullptr);
        return tracking;
    }

    bool update_to_or_first_tracking(SongTime_t time, float radius, SongTime_t* tracking_time)
    {
        if (tracking) {
            if (tracking_time) *tracking_time = last_sample_time;
            return true;
        }

        return scan_until(time, radius, tracking_time);
    }

   private:
    bool scan_until(SongTime_t time, float radius, SongTime_t* first_tracking_time)
    {
        while (last_sample_time < time) {
            SongTime_t next_sample_time = std::min(time, last_sample_time + TRACKING_SAMPLE_STEP);
            if (frame_index < frames.size() && frames[frame_index].ms > last_sample_time)
                next_sample_time = std::min(next_sample_time, frames[frame_index].ms);

            if (next_sample_time <= last_sample_time) next_sample_time = last_sample_time + 1;
            if (next_sample_time > time) next_sample_time = time;

            update_with_sample(sample_replay_at(frames, next_sample_time), radius);
            last_sample_time = next_sample_time;

            while (frame_index < frames.size() && frames[frame_index].ms <= last_sample_time) ++frame_index;

            if (tracking && first_tracking_time != nullptr) {
                *first_tracking_time = last_sample_time;
                return true;
            }
        }
        return tracking;
    }

    void update_with_sample(const ReplaySample& curr, float radius)
    {
        const ReplaySample prev = sample_replay_at(frames, std::max<SongTime_t>(obj.start, curr.ms - 1));
        if (!accept_any_key && !is_action_pressed(prev, other_action(head_action))) {
            accept_any_key = true;
        }
        const bool valid_action = accept_any_key ? pressed_any(curr) : is_action_pressed(curr, head_action);
        const float follow_radius = tracking ? radius * SLIDER_FOLLOW_AREA : radius;
        const glm::vec2 ball_pos = slider_ball_position_at(obj, curr.ms);
        const float ball_distance = glm::distance(ball_pos, curr.p);
        bool valid_position = ball_distance <= follow_radius + SLIDER_TRACKING_TOLERANCE;

        if (!valid_position && ball_distance - follow_radius > LEGACY_SLIDER_PATH_FALLBACK_MIN_MARGIN) {
            const glm::vec2 legacy_ball_pos = legacy_slider_ball_position_at(obj, curr.ms);
            valid_position =
                glm::distance(legacy_ball_pos, curr.p) <= follow_radius + LEGACY_SLIDER_PATH_FALLBACK_TOLERANCE;
        }
        tracking = valid_action && valid_position;
    }

    const std::vector<replayengine::ReplayFrame>& frames;
    const hitobject_t& obj;
    PressAction head_action;
    size_t frame_index;
    SongTime_t last_sample_time;
    bool tracking;
    bool accept_any_key;
};

void judge_slider(const std::vector<replayengine::ReplayFrame>& frames, std::vector<int>& consumed_press_frames,
                  std::vector<JudgedObject>& out, int source_index, const hitobject_t& obj, const HitWindows& windows,
                  float radius)
{
    auto events = generate_slider_events(obj);
    if (events.empty()) return;

    PressAction head_action = PressAction::None;
    size_t head_frame_index = 0;
    auto head = judge_press_target(frames, consumed_press_frames, source_index, JudgedObjectType::SliderHead,
                                   apply_mod_position(obj.pos), obj.start, windows, radius, &head_action,
                                   &head_frame_index, out);
    if (!head) return;
    out.emplace_back(*head);

    const bool head_hit = is_hit(head->result);

    const glm::vec2 head_cursor = sample_replay_at(frames, head->time).p;
    bool all_passed_in_range = true;
    if (head_hit) {
        for (const auto& event : events) {
            if (event.type == JudgedObjectType::SliderHead) continue;
            if (event.time > head->time) break;
            const glm::vec2 event_pos = slider_position_at_progress(obj, event.path_progress);
            if (glm::distance(event_pos, head_cursor) > radius * SLIDER_FOLLOW_AREA + SLIDER_TRACKING_TOLERANCE) {
                all_passed_in_range = false;
                break;
            }
        }
    } else {
        all_passed_in_range = false;
    }

    const SongTime_t tracker_start_time = head_hit ? head->time : obj.start;
    const glm::vec2 tracker_start_cursor = sample_replay_at(frames, tracker_start_time).p;
    const bool tracking_at_start =
        pressed_any(sample_replay_at(frames, tracker_start_time)) &&
        glm::distance(slider_ball_position_at(obj, tracker_start_time), tracker_start_cursor) <=
        radius + SLIDER_TRACKING_TOLERANCE;
    bool tracking_at_head = glm::distance(slider_ball_position_at(obj, head->time), head_cursor) <=
                            radius * SLIDER_FOLLOW_AREA + SLIDER_TRACKING_TOLERANCE;
    if (!all_passed_in_range)
        tracking_at_head = glm::distance(slider_ball_position_at(obj, head->time), head_cursor) <=
                           radius + SLIDER_TRACKING_TOLERANCE;
    SliderTracker tracker(frames, obj, head_action, tracker_start_time, head_hit ? tracking_at_head : tracking_at_start,
                          !head_hit);

    for (const auto& event : events) {
        if (event.type == JudgedObjectType::SliderHead) continue;
        const SongTime_t judge_time = static_cast<SongTime_t>(event.judge_time + 0.5);
        if (event.time <= head->time) {
            const bool tracking = head_hit ? all_passed_in_range : tracker.update_to(head->time, radius);
            const SongTime_t result_time = std::max(judge_time, head->time);
            const HitResult result = tracking ? max_result_for(event.type) : miss_result_for(event.type);
            push_result(out, source_index, event.type, result, result_time, 0);
            continue;
        }
        if (event.type == JudgedObjectType::SliderTail) {
            SongTime_t hit_time = std::max(judge_time, head->time);
            const SongTime_t tail_time = static_cast<SongTime_t>(event.time + 0.5);
            bool tracking = tracker.update_to(hit_time, radius);
            if (!tracking && hit_time < tail_time)
                tracking = tracker.update_to_or_first_tracking(tail_time, radius, &hit_time);
            if (!tracking && legacy_expanded_tracking_at(frames, obj, hit_time, radius)) tracking = true;
            const HitResult result = tracking ? max_result_for(event.type) : miss_result_for(event.type);
            const SongTime_t result_time = tracking ? hit_time : std::max(tail_time, head->time);
            push_result(out, source_index, event.type, result, result_time, 0);
            continue;
        }
        SongTime_t hit_time = judge_time;
        bool tracking = tracker.update_to(judge_time, radius);
        if (!tracking) tracking = tracker.update_to_or_first_tracking(judge_time + 2, radius, &hit_time);
        if (!tracking && legacy_expanded_tracking_at(frames, obj, hit_time, radius)) tracking = true;
        const HitResult result = tracking ? max_result_for(event.type) : miss_result_for(event.type);
        push_result(out, source_index, event.type, result, tracking ? hit_time : judge_time, 0);
    }
}

double spinner_required_spins(const hitobject_t& obj)
{
    const double min_rps = difficulty_range(adjusted_od(), 90.0, 150.0, 225.0) / 60.0;
    return static_cast<int>(min_rps * (obj.duration() / 1000.0) + SPINNER_DURATION_ERROR);
}

int spinner_maximum_bonus_spins(const hitobject_t& obj, int spins_required)
{
    const double max_rps = difficulty_range(adjusted_od(), 250.0, 380.0, 430.0) / 60.0;
    return std::max(0, static_cast<int>(max_rps * (obj.duration() / 1000.0) + SPINNER_DURATION_ERROR) -
                           spins_required - SPINNER_BONUS_SPINS_GAP);
}

void judge_spinner(const std::vector<replayengine::ReplayFrame>& frames, std::vector<JudgedObject>& out, int source_index,
                   const hitobject_t& obj)
{
    const int required_spins = static_cast<int>(spinner_required_spins(obj));
    const int maximum_bonus_spins = spinner_maximum_bonus_spins(obj, required_spins);
    const int normal_tick_count = required_spins + SPINNER_BONUS_SPINS_GAP;
    const int total_tick_count = normal_tick_count + maximum_bonus_spins;

    const glm::vec2 centre(256.f, 192.f);
    double total_rotation = 0.0;
    bool have_angle = false;
    double last_angle = 0.0;
    int completed_full_spins = 0;
    std::vector<SongTime_t> full_spin_times;
    size_t index = lower_frame_index(frames, obj.start);
    for (; index < frames.size() && frames[index].ms <= obj.end; ++index) {
        if (!pressed_any(frames[index])) {
            have_angle = false;
            continue;
        }
        const glm::vec2 v = frames[index].p - centre;
        if (glm::length(v) < 1.0f) {
            have_angle = false;
            continue;
        }
        const double angle = std::atan2(v.y, v.x);
        if (have_angle) {
            double delta = angle - last_angle;
            while (delta > 3.141592653589793) delta -= 6.283185307179586;
            while (delta < -3.141592653589793) delta += 6.283185307179586;
            total_rotation += std::abs(delta) * 180.0 / 3.141592653589793;
            while (completed_full_spins < total_tick_count && total_rotation >= (completed_full_spins + 1) * 360.0) {
                full_spin_times.push_back(frames[index].ms);
                completed_full_spins++;
            }
        }
        last_angle = angle;
        have_angle = true;
    }

    for (int i = 0; i < total_tick_count; ++i) {
        const bool is_bonus_tick = i >= normal_tick_count;
        const auto type = is_bonus_tick ? JudgedObjectType::SpinnerBonus : JudgedObjectType::SpinnerTick;
        const bool hit = i < static_cast<int>(full_spin_times.size());
        const double tick_time =
            obj.start + (static_cast<double>(i + 1) / std::max(1, total_tick_count)) * static_cast<double>(obj.duration());
        push_result(out, source_index, type, hit ? max_result_for(type) : miss_result_for(type),
                    hit ? full_spin_times[i] : static_cast<SongTime_t>(tick_time + 0.5), 0);
    }

    if (required_spins <= 0) {
        push_result(out, source_index, JudgedObjectType::Spinner, HitResult::Great, obj.end, 0);
        return;
    }

    const double progress = total_rotation / 360.0 / required_spins;
    HitResult result = HitResult::Miss;
    if (progress >= 1.0)
        result = HitResult::Great;
    else if (progress > 0.9)
        result = HitResult::Ok;
    else if (progress > 0.75)
        result = HitResult::Meh;
    push_result(out, source_index, JudgedObjectType::Spinner, result, obj.end, 0);
}

void calculate_stats(MutableStats& mutable_stats, std::vector<JudgedObject>& objects)
{
    std::stable_sort(objects.begin(), objects.end(), [](const JudgedObject& lhs, const JudgedObject& rhs) {
        if (lhs.time == rhs.time) return lhs.source_hitobject_index < rhs.source_hitobject_index;
        return lhs.time < rhs.time;
    });

    double max_combo_sim = 0.0;
    int maximum_combo = 0;
    for (auto& obj : objects) {
        const HitResult max_result = max_result_for(obj.object_type);
        obj.score_contribution = base_score_for(obj.result);
        obj.max_score_contribution = base_score_for(max_result);
        obj.contributes_accuracy = affects_accuracy(obj.result);
        obj.contributes_combo = affects_combo(obj.result);

        if (affects_accuracy(max_result)) {
            mutable_stats.maximum_base_score += base_score_for(max_result);
            mutable_stats.maximum_accuracy_judgement_count++;
        }
        if (affects_combo(max_result)) {
            maximum_combo++;
            max_combo_sim += base_score_for(max_result) * std::pow(maximum_combo, 0.5);
        }

        obj.combo_before = mutable_stats.combo;
        if (affects_combo(obj.result)) {
            if (is_hit(obj.result))
                mutable_stats.combo++;
            else
                mutable_stats.combo = 0;
        }
        obj.combo_after = mutable_stats.combo;
        mutable_stats.stats.max_combo = std::max(mutable_stats.stats.max_combo, mutable_stats.combo);
        obj.max_combo_after = mutable_stats.stats.max_combo;

        if (affects_accuracy(max_result)) mutable_stats.current_accuracy_judgement_count++;
        if (affects_accuracy(obj.result)) mutable_stats.current_base_score += base_score_for(obj.result);
        if (is_bonus(obj.result)) {
            mutable_stats.current_bonus_portion += base_score_for(obj.result);
        } else if (is_scorable(obj.result)) {
            mutable_stats.current_combo_portion += base_score_for(max_result) * std::pow(mutable_stats.combo, 0.5);
        }

        if (affects_ui_bucket(obj.object_type)) {
            switch (aggregate_points(obj)) {
                case 300:
                    mutable_stats.stats.num_300++;
                    break;
                case 100:
                    mutable_stats.stats.num_100++;
                    break;
                case 50:
                    mutable_stats.stats.num_50++;
                    break;
                default:
                    mutable_stats.stats.num_miss++;
                    break;
            }
        }
    }

    mutable_stats.maximum_combo_portion = max_combo_sim;
    mutable_stats.stats.maximum_combo = maximum_combo;

    const double accuracy =
        mutable_stats.maximum_base_score > 0.0 ? mutable_stats.current_base_score / mutable_stats.maximum_base_score : 1.0;
    mutable_stats.stats.accuracy = static_cast<float>(accuracy * 100.0);
    mutable_stats.stats.weighted_accuracy = mutable_stats.stats.accuracy;

    const double combo_progress =
        mutable_stats.maximum_combo_portion > 0.0 ? mutable_stats.current_combo_portion / mutable_stats.maximum_combo_portion : 1.0;
    const double accuracy_progress = mutable_stats.maximum_accuracy_judgement_count > 0
                                         ? static_cast<double>(mutable_stats.current_accuracy_judgement_count) /
                                               mutable_stats.maximum_accuracy_judgement_count
                                         : 1.0;
    const double total_score =
        500000.0 * accuracy * combo_progress + 500000.0 * std::pow(accuracy, 5.0) * accuracy_progress +
        mutable_stats.current_bonus_portion;
    mutable_stats.stats.total_score = static_cast<uint32_t>(
        clamp_to(std::llround(total_score), 0LL, static_cast<long long>(std::numeric_limits<uint32_t>::max())));
}

float bucket_accuracy(const JudgementStats& stats)
{
    const int total = stats.num_300 + stats.num_100 + stats.num_50 + stats.num_miss;
    if (total <= 0) return 0.0f;
    return static_cast<float>(100.0 * (stats.num_50 + 2.0 * stats.num_100 + 6.0 * stats.num_300) / (6.0 * total));
}

void calculate_timing_stats(JudgementStats& stats, const std::vector<JudgedObject>& objects)
{
    double sum = 0.0;
    double sum_early = 0.0;
    double sum_late = 0.0;
    int count = 0;
    int count_early = 0;
    int count_late = 0;
    for (const auto& obj : objects) {
        if (!is_hit(obj.result)) continue;
        if (obj.object_type != JudgedObjectType::Circle && obj.object_type != JudgedObjectType::SliderHead &&
            obj.object_type != JudgedObjectType::Spinner)
            continue;
        sum += obj.hit_error;
        count++;
        if (obj.hit_error < 0) {
            sum_early += obj.hit_error;
            count_early++;
        } else {
            sum_late += obj.hit_error;
            count_late++;
        }
    }
    const double avg = count > 0 ? sum / count : 0.0;
    double variance = 0.0;
    for (const auto& obj : objects) {
        if (!is_hit(obj.result)) continue;
        if (obj.object_type != JudgedObjectType::Circle && obj.object_type != JudgedObjectType::SliderHead &&
            obj.object_type != JudgedObjectType::Spinner)
            continue;
        variance += std::pow(obj.hit_error - avg, 2.0);
    }
    stats.avg_early = static_cast<float>(count_early > 0 ? sum_early / count_early : 0.0);
    stats.avg_late = static_cast<float>(count_late > 0 ? sum_late / count_late : 0.0);
    stats.unstable_rate = static_cast<float>(count > 0 ? 10.0 * std::sqrt(variance / count) : 0.0);
}

void run_analysis(bool do_trace)
{
    cached_stats = JudgementStats{};
    cached_objects.clear();

    const auto& replay = *replayengine::CurrentView();
    const auto& frames = replay.frames();
    const HitWindows windows = make_hit_windows();
    const float radius = circle_radius();
    std::vector<int> consumed_press_frames(frames.size(), -1);

    for (int i = 0; i < static_cast<int>(beatmapengine::hitobjects.size()); ++i) {
        const auto& obj = beatmapengine::hitobjects[i];
        switch (obj.hitobject_type) {
            case HitObjectType::Circle: {
                auto result = judge_press_target(frames, consumed_press_frames, i, JudgedObjectType::Circle,
                                                 apply_mod_position(obj.pos), obj.start, windows, radius, nullptr,
                                                 nullptr, cached_objects);
                if (result) cached_objects.emplace_back(*result);
                break;
            }
            case HitObjectType::Slider:
                judge_slider(frames, consumed_press_frames, cached_objects, i, obj, windows, radius);
                break;
            case HitObjectType::Spinner:
                judge_spinner(frames, cached_objects, i, obj);
                break;
            case HitObjectType::SliderTick:
            case HitObjectType::SliderEnd:
                break;
        }
    }

    MutableStats mutable_stats;
    calculate_stats(mutable_stats, cached_objects);
    cached_stats = mutable_stats.stats;
    calculate_timing_stats(cached_stats, cached_objects);

    if (do_trace) {
        std::ofstream log("accuracy_analyzer.log");
        if (log.good()) {
            const auto& metadata = replay.metadata();
            log << "lazer judgement trace\n";
            log << std::fixed << std::setprecision(6);
            log << "\n[replay metadata]\n";
            log << "stored_counts 300=" << metadata.num_300 << " 100=" << metadata.num_100 << " 50=" << metadata.num_50
                << " miss=" << metadata.num_miss << "\n";
            log << "stored_score=" << metadata.total_score << " stored_max_combo=" << metadata.max_combo
                << " mods=" << metadata.mods << " mode=" << static_cast<int>(metadata.game_mode) << "\n";
            const int stored_total = metadata.num_300 + metadata.num_100 + metadata.num_50 + metadata.num_miss;
            const double stored_bucket_accuracy =
                stored_total > 0
                    ? 100.0 * (metadata.num_50 + 2.0 * metadata.num_100 + 6.0 * metadata.num_300) /
                          (6.0 * stored_total)
                    : 0.0;
            log << "stored_legacy_bucket_accuracy=" << stored_bucket_accuracy << "\n";

            log << "\n[beatmap / windows]\n";
            log << "OD=" << adjusted_od() << " CS=" << adjusted_cs() << " radius=" << radius << "\n";
            log << "windows great=" << windows.great << " ok=" << windows.ok << " meh=" << windows.meh
                << " miss=" << windows.miss << "\n";

            int slider_tick_hit = 0, slider_tick_total = 0;
            int slider_end_hit = 0, slider_end_total = 0;
            int spinner_spin_hit = 0, spinner_spin_total = 0;
            int spinner_bonus_hit = 0, spinner_bonus_total = 0;

            struct ScorePart {
                int count = 0;
                int hit = 0;
                double current = 0.0;
                double maximum = 0.0;
            };
            ScorePart top_level_part;
            ScorePart slider_tick_part;
            ScorePart slider_tail_part;
            ScorePart spinner_tick_part;
            ScorePart spinner_bonus_part;
            int result_great = 0, result_ok = 0, result_meh = 0, result_miss = 0;
            std::vector<HitResult> top_result_by_source(beatmapengine::hitobjects.size(), HitResult::None);
            std::vector<SongTime_t> top_time_by_source(beatmapengine::hitobjects.size(), 0);

            auto add_score_part = [](ScorePart& part, const JudgedObject& obj) {
                const HitResult max_result = max_result_for(obj.object_type);
                part.count++;
                if (is_hit(obj.result)) part.hit++;
                if (affects_accuracy(max_result)) part.maximum += base_score_for(max_result);
                if (affects_accuracy(obj.result)) part.current += base_score_for(obj.result);
            };
            auto top_level_base_from_counts = [](int great, int ok, int meh) {
                return great * 300.0 + ok * 100.0 + meh * 50.0;
            };

            for (const auto& obj : cached_objects) {
                if (affects_ui_bucket(obj.object_type)) {
                    add_score_part(top_level_part, obj);
                    if (obj.source_hitobject_index >= 0 &&
                        obj.source_hitobject_index < static_cast<int>(top_result_by_source.size())) {
                        top_result_by_source[obj.source_hitobject_index] = obj.result;
                        top_time_by_source[obj.source_hitobject_index] = obj.time;
                    }
                    switch (aggregate_points(obj)) {
                        case 300:
                            result_great++;
                            break;
                        case 100:
                            result_ok++;
                            break;
                        case 50:
                            result_meh++;
                            break;
                        default:
                            result_miss++;
                            break;
                    }
                }

                switch (obj.object_type) {
                    case JudgedObjectType::SliderTick:
                    case JudgedObjectType::SliderRepeat:
                        add_score_part(slider_tick_part, obj);
                        slider_tick_total++;
                        if (obj.result == HitResult::LargeTickHit) slider_tick_hit++;
                        break;
                    case JudgedObjectType::SliderTail:
                        add_score_part(slider_tail_part, obj);
                        slider_end_total++;
                        if (obj.result == HitResult::SliderTailHit) slider_end_hit++;
                        break;
                    case JudgedObjectType::SpinnerTick:
                        add_score_part(spinner_tick_part, obj);
                        spinner_spin_total++;
                        if (obj.result == HitResult::SmallBonus) spinner_spin_hit++;
                        break;
                    case JudgedObjectType::SpinnerBonus:
                        add_score_part(spinner_bonus_part, obj);
                        spinner_bonus_total++;
                        if (obj.result == HitResult::LargeBonus) spinner_bonus_hit++;
                        break;
                    default:
                        break;
                }
            }

            const double trace_current_base = top_level_part.current + slider_tick_part.current + slider_tail_part.current;
            const double trace_maximum_base = top_level_part.maximum + slider_tick_part.maximum + slider_tail_part.maximum;
            const double trace_accuracy =
                trace_maximum_base > 0.0 ? 100.0 * trace_current_base / trace_maximum_base : 100.0;
            const double stored_top_level_base =
                top_level_base_from_counts(metadata.num_300, metadata.num_100, metadata.num_50);
            const double top_level_base_delta = top_level_part.current - stored_top_level_base;
            const double nested_accuracy_base = slider_tick_part.current + slider_tail_part.current;
            const double recomputed_top_with_stored_visible_counts =
                stored_top_level_base + slider_tick_part.current + slider_tail_part.current;
            const double recomputed_accuracy_with_stored_visible_counts =
                trace_maximum_base > 0.0 ? 100.0 * recomputed_top_with_stored_visible_counts / trace_maximum_base : 100.0;

            log << "\n[recomputed summary]\n";
            log << "counts 300=" << cached_stats.num_300 << " 100=" << cached_stats.num_100
                << " 50=" << cached_stats.num_50 << " miss=" << cached_stats.num_miss << "\n";
            log << "counts_delta_vs_metadata 300=" << (cached_stats.num_300 - static_cast<int>(metadata.num_300))
                << " 100=" << (cached_stats.num_100 - static_cast<int>(metadata.num_100))
                << " 50=" << (cached_stats.num_50 - static_cast<int>(metadata.num_50))
                << " miss=" << (cached_stats.num_miss - static_cast<int>(metadata.num_miss)) << "\n";
            log << "weighted_accuracy=" << cached_stats.weighted_accuracy
                << " recomputed_from_parts=" << trace_accuracy << "\n";
            log << "legacy_bucket_accuracy_from_top_counts=" << bucket_accuracy(cached_stats) << "\n";
            log << "max_combo=" << cached_stats.max_combo << "/" << cached_stats.maximum_combo
                << " total_score_estimate=" << cached_stats.total_score << "\n";

            log << "\n[accuracy formula]\n";
            log << "displayed_acc = current_accuracy_base / maximum_accuracy_base * 100\n";
            log << "current_accuracy_base=" << trace_current_base << "\n";
            log << "maximum_accuracy_base=" << trace_maximum_base << "\n";
            log << "top_level_base=" << top_level_part.current << "/" << top_level_part.maximum
                << " counts Great=" << result_great << " Ok=" << result_ok << " Meh=" << result_meh
                << " Miss=" << result_miss << "\n";
            log << "slider_tick_or_repeat_base=" << slider_tick_part.current << "/" << slider_tick_part.maximum
                << " hit=" << slider_tick_part.hit << "/" << slider_tick_part.count << "\n";
            log << "slider_tail_base=" << slider_tail_part.current << "/" << slider_tail_part.maximum
                << " hit=" << slider_tail_part.hit << "/" << slider_tail_part.count << "\n";
            log << "spinner_spin_base_excluded_from_accuracy=" << spinner_tick_part.current << "/"
                << spinner_tick_part.maximum << " hit=" << spinner_tick_part.hit << "/" << spinner_tick_part.count
                << "\n";
            log << "spinner_bonus_base_excluded_from_accuracy=" << spinner_bonus_part.current << "/"
                << spinner_bonus_part.maximum << " hit=" << spinner_bonus_part.hit << "/" << spinner_bonus_part.count
                << "\n";

            log << "\n[nested hit counters]\n";
            log << "nested slider_tick=" << slider_tick_hit << "/" << slider_tick_total
                << " slider_end=" << slider_end_hit << "/" << slider_end_total
                << " spinner_spin=" << spinner_spin_hit << "/" << spinner_spin_total
                << " spinner_bonus=" << spinner_bonus_hit << "/" << spinner_bonus_total << "\n";

            log << "\n[accuracy reconciliation]\n";
            log << "stored_visible_top_level_base=" << stored_top_level_base << "\n";
            log << "recomputed_visible_top_level_base=" << top_level_part.current
                << " delta=" << top_level_base_delta << "\n";
            log << "recomputed_nested_accuracy_base=" << nested_accuracy_base
                << " slider_tick=" << slider_tick_part.current << " slider_tail=" << slider_tail_part.current << "\n";
            log << "accuracy_if_visible_counts_used_metadata_but_nested_used_recomputed="
                << recomputed_accuracy_with_stored_visible_counts << "\n";
            log << "note=displayed acc includes slider tick/tail base score, so visible bucket deltas can be hidden by nested deltas and 2dp rounding\n";

            log << "\n[non-great top-level judgements]\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& obj = cached_objects[i];
                if (!affects_ui_bucket(obj.object_type)) continue;
                if (obj.result == HitResult::Great) continue;
                log << i << ": source=" << obj.source_hitobject_index << " type=" << object_type_name(obj.object_type)
                    << " time=" << obj.time << " result=" << result_name(obj.result) << " error=" << obj.hit_error
                    << " combo=" << obj.combo_before << "->" << obj.combo_after << " score=" << obj.score_contribution
                    << "/" << obj.max_score_contribution << "\n";
            }

            log << "\n[near hit-window boundary top-level judgements]\n";
            log << "threshold=2ms around great/ok/meh windows\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& obj = cached_objects[i];
                if (!affects_ui_bucket(obj.object_type)) continue;
                if (!is_hit(obj.result)) continue;
                const double abs_error = std::abs(static_cast<double>(obj.hit_error));
                const double great_distance = std::abs(abs_error - windows.great);
                const double ok_distance = std::abs(abs_error - windows.ok);
                const double meh_distance = std::abs(abs_error - windows.meh);
                const double nearest = std::min(great_distance, std::min(ok_distance, meh_distance));
                if (nearest > 2.0) continue;
                const char* nearest_window = "great";
                double nearest_window_value = windows.great;
                if (ok_distance < great_distance && ok_distance <= meh_distance) {
                    nearest_window = "ok";
                    nearest_window_value = windows.ok;
                } else if (meh_distance < great_distance && meh_distance < ok_distance) {
                    nearest_window = "meh";
                    nearest_window_value = windows.meh;
                }
                log << i << ": source=" << obj.source_hitobject_index << " type=" << object_type_name(obj.object_type)
                    << " time=" << obj.time << " result=" << result_name(obj.result) << " error=" << obj.hit_error
                    << " nearest_window=" << nearest_window << "(" << nearest_window_value << ") distance=" << nearest
                    << "\n";
            }

            log << "\n[miss top-level nearby press candidates]\n";
            log << "up to 6 nearest press edges inside the fixed miss window for each top-level miss\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& judged = cached_objects[i];
                if (!affects_ui_bucket(judged.object_type) || judged.result != HitResult::Miss) continue;
                if (judged.source_hitobject_index < 0 ||
                    judged.source_hitobject_index >= static_cast<int>(beatmapengine::hitobjects.size()))
                    continue;

                const auto& source = beatmapengine::hitobjects[judged.source_hitobject_index];
                if (source.hitobject_type != HitObjectType::Circle && source.hitobject_type != HitObjectType::Slider)
                    continue;

                const glm::vec2 object_pos = apply_mod_position(source.pos);
                struct PressCandidate {
                    SongTime_t time = 0;
                    int offset = 0;
                    float distance = 0.f;
                    HitResult time_result = HitResult::None;
                    bool in_radius = false;
                    int keys = 0;
                    int consumed_by = -1;
                };
                std::vector<PressCandidate> candidates;
                size_t frame_index = lower_frame_index(frames, source.start - static_cast<SongTime_t>(windows.miss));
                if (frame_index == 0) frame_index = 1;
                for (; frame_index < frames.size(); ++frame_index) {
                    const auto& curr = frames[frame_index];
                    if (curr.ms > source.start + windows.miss) break;
                    const auto& prev = frames[frame_index - 1];
                    if (press_action(prev, curr) == PressAction::None) continue;
                    const int offset = curr.ms - source.start;
                    const float distance = glm::distance(object_pos, curr.p);
                    candidates.push_back({curr.ms, offset, distance, result_for(offset, windows), distance <= radius,
                                          curr.keys, consumed_press_frames[frame_index]});
                }
                std::sort(candidates.begin(), candidates.end(), [](const PressCandidate& lhs, const PressCandidate& rhs) {
                    const int lhs_abs = std::abs(lhs.offset);
                    const int rhs_abs = std::abs(rhs.offset);
                    if (lhs_abs == rhs_abs) return lhs.distance < rhs.distance;
                    return lhs_abs < rhs_abs;
                });

                log << i << ": source=" << judged.source_hitobject_index << " type=" << object_type_name(judged.object_type)
                    << " object_time=" << source.start << " judged_time=" << judged.time << "\n";
                const size_t limit = std::min<size_t>(6, candidates.size());
                for (size_t c = 0; c < limit; ++c) {
                    const auto& candidate = candidates[c];
                    log << "  press time=" << candidate.time << " offset=" << candidate.offset
                        << " distance=" << candidate.distance << "/" << radius
                        << " in_radius=" << (candidate.in_radius ? "yes" : "no")
                        << " time_result=" << result_name(candidate.time_result) << " keys=" << candidate.keys
                        << " consumed_by=" << candidate.consumed_by << "\n";
                }
                if (candidates.empty()) log << "  no press edges found\n";
            }

            log << "\n[slider tails hit after missed heads]\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& obj = cached_objects[i];
                if (obj.object_type != JudgedObjectType::SliderTail || obj.result != HitResult::SliderTailHit) continue;
                if (obj.source_hitobject_index < 0 ||
                    obj.source_hitobject_index >= static_cast<int>(top_result_by_source.size()))
                    continue;
                if (top_result_by_source[obj.source_hitobject_index] != HitResult::Miss) continue;
                const auto& source = beatmapengine::hitobjects[obj.source_hitobject_index];
                const ReplaySample head_sample = sample_replay_at(frames, top_time_by_source[obj.source_hitobject_index]);
                const ReplaySample tail_sample = sample_replay_at(frames, source.end);
                const float head_distance =
                    glm::distance(slider_ball_position_at(source, top_time_by_source[obj.source_hitobject_index]),
                                  head_sample.p);
                const float tail_distance = glm::distance(slider_ball_position_at(source, source.end), tail_sample.p);
                log << i << ": source=" << obj.source_hitobject_index << " tail_time=" << obj.time
                    << " head_time=" << top_time_by_source[obj.source_hitobject_index]
                    << " source_start=" << source.start << " source_end=" << source.end
                    << " head_tracking_distance=" << head_distance << "/" << radius
                    << " head_pressed=" << (pressed_any(head_sample) ? "yes" : "no")
                    << " tail_distance=" << tail_distance << "/" << radius
                    << " tail_pressed=" << (pressed_any(tail_sample) ? "yes" : "no")
                    << " combo=" << obj.combo_before << "->" << obj.combo_after << "\n";
            }

            log << "\n[missed slider nested judgements]\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& obj = cached_objects[i];
                if (obj.object_type != JudgedObjectType::SliderTick && obj.object_type != JudgedObjectType::SliderRepeat &&
                    obj.object_type != JudgedObjectType::SliderTail)
                    continue;
                if (is_hit(obj.result)) continue;
                log << i << ": source=" << obj.source_hitobject_index << " type=" << object_type_name(obj.object_type)
                    << " time=" << obj.time << " result=" << result_name(obj.result) << " combo=" << obj.combo_before
                    << "->" << obj.combo_after << " score=" << obj.score_contribution << "/"
                    << obj.max_score_contribution << "\n";
            }

            log << "\n[all judgements]\n";
            for (size_t i = 0; i < cached_objects.size(); ++i) {
                const auto& obj = cached_objects[i];
                log << i << ": source=" << obj.source_hitobject_index << " type=" << object_type_name(obj.object_type)
                    << " time=" << obj.time << " result=" << result_name(obj.result) << " error=" << obj.hit_error
                    << " combo=" << obj.combo_before << "->" << obj.combo_after << " score=" << obj.score_contribution
                    << "/" << obj.max_score_contribution << "\n";
            }
        }
    }

    dirty = false;
}

}  // namespace

void mark_dirty()
{
    dirty = true;
}

const JudgementStats& analyze(bool do_trace)
{
    if (dirty || do_trace) run_analysis(do_trace);
    return cached_stats;
}

const std::vector<JudgedObject>& judged_objects()
{
    analyze(false);
    return cached_objects;
}

bool has_unsupported_mods()
{
    const auto& metadata = replayengine::CurrentView()->metadata();
    return metadata.game_mode != 0 || (metadata.mods & ~SUPPORTED_MODS) != 0;
}

bool can_apply_replay_metadata()
{
    return !has_unsupported_mods() && !beatmapengine::hitobjects.empty();
}

bool apply_to_replay_metadata()
{
    if (!can_apply_replay_metadata()) return false;
    const JudgementStats& stats = analyze(false);
    auto& metadata = replayengine::MutableCurrentView()->mut_metadata();
    metadata.num_300 = static_cast<uint16_t>(clamp_to(stats.num_300, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    metadata.num_100 = static_cast<uint16_t>(clamp_to(stats.num_100, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    metadata.num_50 = static_cast<uint16_t>(clamp_to(stats.num_50, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    metadata.num_miss =
        static_cast<uint16_t>(clamp_to(stats.num_miss, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    metadata.total_score = stats.total_score;
    metadata.max_combo =
        static_cast<uint16_t>(clamp_to(stats.max_combo, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
    metadata.full_combo = stats.max_combo > 0 && stats.max_combo == stats.maximum_combo && stats.num_miss == 0;
    mark_dirty();
    return true;
}

int next_judged_object(std::function<bool(const JudgedObject&)> func)
{
    const auto& objects = judged_objects();
    const SongTime_t time = audioengine::handle->get_time();
    auto iter = std::lower_bound(objects.begin(), objects.end(), time,
                                 [](const JudgedObject& lhs, SongTime_t rhs) { return lhs.time < rhs; });
    for (; iter != objects.end(); ++iter) {
        if (func(*iter)) {
            audioengine::handle->jump_to(iter->time + 1);
            return static_cast<int>(iter - objects.begin());
        }
    }
    return -1;
}

bool get_judged_object_info(int index, int* kind, bool* is_miss, int* hit_error, int* points)
{
    const auto& objects = judged_objects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return false;
    const auto& obj = objects[index];
    switch (obj.object_type) {
        case JudgedObjectType::SliderHead:
            *kind = static_cast<int>(HitObjectType::Slider);
            break;
        case JudgedObjectType::SliderTick:
        case JudgedObjectType::SliderRepeat:
            *kind = static_cast<int>(HitObjectType::SliderTick);
            break;
        case JudgedObjectType::SliderTail:
            *kind = static_cast<int>(HitObjectType::SliderEnd);
            break;
        case JudgedObjectType::Spinner:
        case JudgedObjectType::SpinnerTick:
        case JudgedObjectType::SpinnerBonus:
            *kind = static_cast<int>(HitObjectType::Spinner);
            break;
        default:
            *kind = static_cast<int>(HitObjectType::Circle);
            break;
    }
    *is_miss = is_miss_like(obj.result);
    *hit_error = obj.hit_error;
    *points = aggregate_points(obj);
    return true;
}

bool is_miss_like(HitResult result)
{
    switch (result) {
        case HitResult::Miss:
        case HitResult::SmallTickMiss:
        case HitResult::LargeTickMiss:
            return true;
        default:
            return false;
    }
}

int aggregate_points(const JudgedObject& obj)
{
    switch (obj.result) {
        case HitResult::Great:
        case HitResult::LargeTickHit:
        case HitResult::SmallTickHit:
        case HitResult::SliderTailHit:
            return 300;
        case HitResult::Ok:
            return 100;
        case HitResult::Meh:
            return 50;
        default:
            return 0;
    }
}

}  // namespace lazer_judgement
