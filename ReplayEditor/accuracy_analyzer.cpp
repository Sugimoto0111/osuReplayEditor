// clang-format off
#include "stdafx.h"
// clang-format on

#include "accuracy_analyzer.hpp"

#include <utility>

#include "lazer_judgement.hpp"

void accuracy_analyzer::analyze(Stats* stats, bool do_trace)
{
    const auto& lazer_stats = lazer_judgement::analyze(do_trace);
    if (stats == nullptr) return;

    stats->num_300 = lazer_stats.num_300;
    stats->num_100 = lazer_stats.num_100;
    stats->num_50 = lazer_stats.num_50;
    stats->num_miss = lazer_stats.num_miss;
    stats->accuracy = lazer_stats.accuracy;
    stats->avg_early = lazer_stats.avg_early;
    stats->avg_late = lazer_stats.avg_late;
    stats->unstable_rate = lazer_stats.unstable_rate;
}

int accuracy_analyzer::next_hitobject(std::function<bool(const lazer_judgement::JudgedObject&)> func)
{
    return lazer_judgement::next_judged_object(std::move(func));
}
