#pragma once
// clang-format off
#include "stdafx.h"
// clang-format on

#include <functional>
#include <vector>

namespace lazer_judgement
{

enum class HitResult {
    None,
    Miss,
    Meh,
    Ok,
    Great,
    SmallTickMiss,
    SmallTickHit,
    LargeTickMiss,
    LargeTickHit,
    SliderTailHit,
    SmallBonus,
    LargeBonus,
    IgnoreMiss,
};

enum class JudgedObjectType { Circle, SliderHead, SliderTick, SliderRepeat, SliderTail, Spinner, SpinnerTick, SpinnerBonus };

struct JudgedObject {
    int source_hitobject_index = -1;
    JudgedObjectType object_type = JudgedObjectType::Circle;
    HitResult result = HitResult::None;
    SongTime_t time = 0;
    int hit_error = 0;
    int combo_before = 0;
    int combo_after = 0;
    int max_combo_after = 0;
    int score_contribution = 0;
    int max_score_contribution = 0;
    bool contributes_accuracy = false;
    bool contributes_combo = false;
};

struct JudgementStats {
    int num_300 = 0;
    int num_100 = 0;
    int num_50 = 0;
    int num_miss = 0;
    float accuracy = 0.f;
    float weighted_accuracy = 0.f;
    float avg_early = 0.f;
    float avg_late = 0.f;
    float unstable_rate = 0.f;
    int max_combo = 0;
    int maximum_combo = 0;
    uint32_t total_score = 0;
};

void mark_dirty();
const JudgementStats& analyze(bool do_trace = false);
const std::vector<JudgedObject>& judged_objects();
bool has_unsupported_mods();
bool can_apply_replay_metadata();
bool apply_to_replay_metadata();
int next_judged_object(std::function<bool(const JudgedObject&)> func);
bool get_judged_object_info(int index, int* kind, bool* is_miss, int* hit_error, int* points);
bool is_miss_like(HitResult result);
int aggregate_points(const JudgedObject& obj);

}  // namespace lazer_judgement
