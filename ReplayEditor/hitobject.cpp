// clang-format off
#include "stdafx.h"
// clang-format on
#include "hitobject.hpp"

#include <cstddef>
#include <cstdint>

#include "beatmapengine.hpp"
#include "lazer_judgement.hpp"
#include "replayengine.hpp"
#include "texture.hpp"

#define MIN_SONG_TIME INT32_MIN

#define KIND_IS_CIRCLE (kind & 1)
#define KIND_IS_SLIDER (kind & 2)
#define KIND_IS_SPINNER (kind & 8)

static float approach(const SongTime_t start, const SongTime_t ms)
{
    const SongTime_t pre = beatmapengine::preempt();
    const SongTime_t first_seen = start - pre;
    const float res = RATIO(ms - first_seen, pre);
    return glm::clamp(res, 0.f, 1.f);
}

static float opacity(const SongTime_t start, const SongTime_t end, const SongTime_t ms)
{
    const SongTime_t pre = beatmapengine::preempt();
    const SongTime_t fade = beatmapengine::fadein();
    const SongTime_t hot = start - pre + fade;
    if (ms < start - pre) return 0.0f;
    if (ms < hot) {
        const SongTime_t top = ms - (start - pre);
        return RATIO(top, fade);
    }
    if (ms > end) {
        const SongTime_t top = ms - end;
        return 1.0f - RATIO(top, beatmapengine::fadeout());
    }
    return 1.0f;
}

static glm::vec2 invert_vec(const glm::vec2 &v)
{
    return glm::vec2(v.x, 384.0 - v.y);
}

static bool primary_judgement_points(const hitobject_t *obj, int *points)
{
    if (obj == nullptr || beatmapengine::hitobjects.empty()) return false;

    const hitobject_t *data = beatmapengine::hitobjects.data();
    const ptrdiff_t source_index = obj - data;
    if (source_index < 0 || source_index >= static_cast<ptrdiff_t>(beatmapengine::hitobjects.size())) return false;

    lazer_judgement::JudgedObjectType wanted_type = lazer_judgement::JudgedObjectType::Circle;
    switch (obj->hitobject_type) {
        case HitObjectType::Circle:
            wanted_type = lazer_judgement::JudgedObjectType::Circle;
            break;
        case HitObjectType::Slider:
            wanted_type = lazer_judgement::JudgedObjectType::SliderHead;
            break;
        case HitObjectType::Spinner:
            wanted_type = lazer_judgement::JudgedObjectType::Spinner;
            break;
        default:
            return false;
    }

    for (const auto &judged : lazer_judgement::judged_objects()) {
        if (judged.source_hitobject_index != source_index || judged.object_type != wanted_type) continue;
        *points = lazer_judgement::aggregate_points(judged);
        return true;
    }
    return false;
}

static void glColorJudgement(int points, float alpha)
{
    switch (points) {
        case 0:
            glColor4f(1.0f, 0.18f, 0.18f, alpha);
            break;
        case 50:
            glColor4f(1.0f, 0.85f, 0.15f, alpha);
            break;
        case 100:
            glColor4f(0.25f, 0.95f, 0.35f, alpha);
            break;
        default:
            glColor4f(1.0f, 1.0f, 1.0f, alpha);
            break;
    }
}

static void glColorHitObjectJudgement(const hitobject_t *obj, float alpha)
{
    int points = 300;
    if (!primary_judgement_points(obj, &points)) points = 300;
    glColorJudgement(points, alpha);
}

static void draw_hitcircle(glm::vec2 pos, glm::vec2 size, float alpha)
{
    textures::hitcircle->draw(pos, size * 0.5f, size);
    if (textures::hitcircleoverlay != nullptr) {
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        textures::hitcircleoverlay->draw(pos, size * 0.5f, size);
    }
}

static unsigned char comma_ahead(const char *&def, int amt)
{
    while (*def) {
        if (amt == 0) return 0;
        if (*def == ',') --amt;
        ++def;
    }
    return 1;
}

hitobject_t::hitobject_t(const char *def) noexcept
    : hit_sound(0), is_miss(true), hit_error(0), stack_count(0), left(nullptr), right(nullptr), max_in_subtree(MIN_SONG_TIME)
{
    int kind;
    if (sscanf_s(def, "%f,%f,%d,%d,%d", &pos.x, &pos.y, &start, &kind, &hit_sound) < 4) fatal(".osr bad format");
    slider = nullptr;
    if (KIND_IS_CIRCLE) {
        end = start;
        hitobject_type = HitObjectType::Circle;
    } else if (KIND_IS_SLIDER) {
        if (comma_ahead(def, 5)) fatal("bad slider def");
        slider = slider_t::from_def(def, pos, start, end);
        end = start + static_cast<SongTime_t>(slider->duration() * slider->repeat);
        hitobject_type = HitObjectType::Slider;
    } else if (KIND_IS_SPINNER) {
        if (comma_ahead(def, 5)) fatal("bad spinner def");
        if (sscanf_s(def, "%d", &end) != 1) fatal(".osr bad format");
        hitobject_type = HitObjectType::Spinner;
        if (start >= end) {
            end = start + 1;
        }
    } else {
        fatal(".osr bad format");
        end = start;
        hitobject_type = HitObjectType::Circle;
    }
}

hitobject_t::hitobject_t(const glm::vec2 &my_pos, SongTime_t my_start, SongTime_t my_end,
                         HitObjectType my_hit_object_type) noexcept
    : pos(my_pos),
      start(my_start),
      end(my_end),
      hitobject_type(my_hit_object_type),
      slider(nullptr),
      hit_sound(0),
      is_miss(true),
      hit_error(0),
      stack_count(0),
      left(nullptr),
      right(nullptr),
      max_in_subtree(MIN_SONG_TIME)
{
}

void hitobject_t::draw_bg(SongTime_t ms) const
{
    switch (hitobject_type) {
        case HitObjectType::Circle:
        case HitObjectType::Slider: {
            const float o = opacity(start, end, ms);
            glm::vec2 size = glm::vec2(beatmapengine::circleradius() * 2.0f);
            if (slider) {
                slider->draw(ms, start, end, o);
                glColorHitObjectJudgement(this, o);
                if (beatmapengine::hitobjects_inverted)
                    draw_hitcircle(invert_vec(slider->end_pos()), size, o);
                else
                    draw_hitcircle(slider->end_pos(), size, o);
            }
            glColorHitObjectJudgement(this, o);
            if (beatmapengine::hitobjects_inverted)
                draw_hitcircle(invert_vec(pos), size, o);
            else
                draw_hitcircle(pos, size, o);
            break;
        }
    }
}

void hitobject_t::draw_fg(SongTime_t ms) const
{
    const float o = opacity(start, end, ms);
    switch (hitobject_type) {
        case HitObjectType::Spinner: {
            glColor4f(0.0f, 0.5f, 0.5f, o);
            const glm::vec2 smallsize(25.0f, 25.0f);
            const glm::vec2 bigsize(350.0f, 350.0f);
            const glm::vec2 middle(256.0f, 192.0);
            const float amt = glm::clamp(RATIO(ms - start, end - start), 0.f, 1.f);
            const glm::vec2 varsize = glm::mix(bigsize, smallsize, amt);
            textures::approachcircle->draw(middle, smallsize * 0.5f, smallsize);
            textures::approachcircle->draw(middle, varsize * 0.5f, varsize);
            break;
        }
        case HitObjectType::Circle:
        case HitObjectType::Slider: {
            glColorHitObjectJudgement(this, o);
            glm::vec2 size = glm::vec2(beatmapengine::circleradius() * 2.0f);
            size *= glm::mix(4.0f, 1.0f, approach(start, ms));
            if (beatmapengine::hitobjects_inverted)
                textures::approachcircle->draw(invert_vec(pos), size * 0.5f, size);
            else
                textures::approachcircle->draw(pos, size * 0.5f, size);
            break;
        }
        case HitObjectType::SliderTick: {
            glm::vec2 size = glm::vec2(beatmapengine::circleradius() * 0.5f);
            glColor4f(1.0f, 1.0f, 1.0f, o);
            if (beatmapengine::hitobjects_inverted)
                textures::slidertick->draw(invert_vec(pos), size * 0.5f, size);
            else
                textures::slidertick->draw(pos, size * 0.5f, size);
            break;
        }
    }
}

void hitobject_t::destroy()
{
    if (slider) {
        delete slider;
        slider = nullptr;
    }
}

SongTime_t hitobject_t::animation_start() const
{
    return start - beatmapengine::preempt();
}

SongTime_t hitobject_t::animation_end() const
{
    return end + beatmapengine::fadeout();
}
