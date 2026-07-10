// clang-format off
#include "stdafx.h"
// clang-format on

#include "tools.hpp"

#include <algorithm>
#include <vector>

#include "audioengine.hpp"
#include "lazer_judgement.hpp"
#include "ui.hpp"

namespace tool
{

namespace
{

class SelectTool : public Tool
{
   public:
    void OnMouseUp(const glm::vec2& mouse) override;
    void OnMouseDown(const glm::vec2& mouse) override;
    void OnMouseMove(const glm::vec2& mouse) override;
    void Draw() override;

   private:
    bool ApplyMutation(replayengine::Replay& replay);

    bool m_enabled = false;
    glm::vec2 m_v0{0.f, 0.f};
    glm::vec2 m_v1{0.f, 0.f};
};

class GrabTool : public Tool
{
   public:
    void OnMouseUp(const glm::vec2& mouse) override;
    void OnMouseDown(const glm::vec2& mouse) override;
    void OnMouseMove(const glm::vec2& mouse) override;
    void Draw() override;

   private:
    void DoWeighting(replayengine::Replay* replay, const glm::vec2& a, const glm::vec2& b, const I64 start,
                     const I64 end, const bool rev);
    bool ApplyMutation(replayengine::Replay* replay);

    bool m_enabled = false;
    glm::vec2 m_v0{0.f, 0.f};
    glm::vec2 m_v1{0.f, 0.f};
    std::vector<replayengine::ReplayFrame> m_frame_buf;
};

class BrushTool : public Tool
{
   public:
    void OnMouseUp(const glm::vec2& mouse) override;
    void OnMouseDown(const glm::vec2& mouse) override;
    void OnMouseMove(const glm::vec2& mouse) override;
    void Draw() override;

   private:
    bool ApplyMutation(replayengine::Replay* replay);
    void CaptureTargets(const replayengine::Replay& replay, const glm::vec2& mouse);
    float EffectiveRadius() const;

    struct BrushTarget
    {
        I64 index;
        glm::vec2 original_position;
        float weight;
    };

    bool m_enabled = false;
    glm::vec2 m_down_mouse{0.f, 0.f};
    glm::vec2 m_current_mouse{0.f, 0.f};
    std::vector<BrushTarget> m_targets;
};

SelectTool local_select_tool_container;
GrabTool local_grab_tool_container;
BrushTool local_brush_tool_container;

ToolType local_current_tool_type = ToolType::Select;

bool is_between(const float v, const float a, const float b)
{
    return (v >= a && v <= b) || (v >= b && v <= a);
}

}  // namespace

Tool* current_tool = &local_select_tool_container;
float brush_radius = 60.f;
I64 editor_window_start_ms = 0;
I64 editor_window_end_ms = 0;

ToolType CurrentToolType()
{
    return local_current_tool_type;
}

void CurrentToolType(ToolType new_type)
{
    if (local_current_tool_type == new_type) {
        return;
    }
    local_current_tool_type = new_type;
    switch (local_current_tool_type) {
        case ToolType::Select:
            current_tool = &local_select_tool_container;
            break;
        case ToolType::Grab:
            current_tool = &local_grab_tool_container;
            break;
        case ToolType::Brush:
            current_tool = &local_brush_tool_container;
            break;
    }
}

// ----------
// SelectTool
// ----------

void SelectTool::OnMouseUp(const glm::vec2& mouse)
{
    m_enabled = false;
    m_v1 = mouse;

    using ::replayengine::Replay;
    replayengine::MutateCurrentView([this](Replay& replay) { return ApplyMutation(replay); });
}

void SelectTool::OnMouseDown(const glm::vec2& mouse)
{
    m_enabled = true;
    m_v0 = mouse;
}

void SelectTool::OnMouseMove(const glm::vec2& mouse)
{
    m_v1 = mouse;
}

bool SelectTool::ApplyMutation(replayengine::Replay& replay)
{
    using ::replayengine::ReplayFrame;
    const SongTime_t ms = audioengine::handle->get_time();
    auto start =
        std::lower_bound(replay.frames().begin(), replay.frames().end(), ms - ui::trail_length, CmpMs<ReplayFrame>());
    auto end = std::lower_bound(replay.frames().begin(), replay.frames().end(), ms, CmpMs<ReplayFrame>());
    if (start == replay.frames().end()) return false;
    if (end != replay.frames().end()) ++end;
    I64 new_mark_in = -1;
    I64 new_mark_out = -1;
    for (auto iter = start; iter != end; ++iter) {
        if (is_between(iter->p.x, m_v0.x, m_v1.x) && is_between(iter->p.y, m_v0.y, m_v1.y)) {
            if (new_mark_in == -1) {
                new_mark_in = iter - replay.frames().begin();
            }
            new_mark_out = iter - replay.frames().begin();
        }
    }
    if (new_mark_in == -1 || new_mark_out == -1) return false;
    replay.place_marks_at(new_mark_in, new_mark_out);
    return true;
}

void SelectTool::Draw()
{
    if (!m_enabled) return;
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    glColorBlue();
    glVertex2f(m_v0.x, m_v0.y);
    glVertex2f(m_v1.x, m_v0.y);
    glVertex2f(m_v1.x, m_v1.y);
    glVertex2f(m_v0.x, m_v1.y);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

// --------
// GrabTool
// --------

void GrabTool::OnMouseUp(const glm::vec2& mouse)
{
    m_enabled = false;
    m_v1 = mouse;
    if (ApplyMutation(replayengine::MutableCurrentView())) lazer_judgement::mark_dirty();
}

void GrabTool::OnMouseDown(const glm::vec2& mouse)
{
    if (!replayengine::CurrentView()->are_all_marks_consistent()) {
        return;
    }
    m_enabled = true;
    m_v0 = mouse;
    replayengine::DuplicateCurrentView();
    m_frame_buf = replayengine::CurrentView()->frames();
}

void GrabTool::OnMouseMove(const glm::vec2& mouse)
{
    m_v1 = mouse;
    if (m_enabled) {
        if (ApplyMutation(replayengine::MutableCurrentView())) lazer_judgement::mark_dirty();
    }
}

void SetEditorWindow(I64 start_ms, I64 end_ms)
{
    if (start_ms <= end_ms) {
        editor_window_start_ms = start_ms;
        editor_window_end_ms = end_ms;
    } else {
        editor_window_start_ms = end_ms;
        editor_window_end_ms = start_ms;
    }
}

void GrabTool::DoWeighting(replayengine::Replay* replay, const glm::vec2& a, const glm::vec2& b, const I64 start,
                           const I64 end, const bool rev)
{
    float d = 0.f;
    for (I64 i = start; i < end; ++i) {
        const glm::vec2& p0 = m_frame_buf[i - 1].p;
        const glm::vec2& p1 = m_frame_buf[i].p;
        d += glm::distance(p1, p0);
    }
    const glm::vec2 dir = m_v1 - m_v0;
    float d0 = 0.f;
    constexpr float eps = 0.00001f;
    for (I64 i = start; i < end; ++i) {
        const glm::vec2& p0 = m_frame_buf[i - 1].p;
        const glm::vec2& p = m_frame_buf[i].p;
        d0 += glm::distance(p, p0);
        float r = 1.f;
        if (d > eps && d0 > eps) {
            r = rev ? ((d - d0) / d) : (d0 / d);
        }
        replay->mut_frames()[i].p = p + dir * r;
    }
}

bool GrabTool::ApplyMutation(replayengine::Replay* replay)
{
    if (!replay->are_all_marks_consistent()) {
        return false;
    }
    const I64 mark_in = replay->mark_in();
    const I64 mark_mid = replay->mark_mid();
    const I64 mark_out = replay->mark_out();
    const glm::vec2& a = replay->mark_in_frame().p;
    const glm::vec2& b = replay->mark_mid_frame().p;
    const glm::vec2& c = replay->mark_out_frame().p;
    replayengine::MutableCurrentView()->mut_frames()[mark_mid].p = m_frame_buf[mark_mid].p + (m_v1 - m_v0);
    DoWeighting(replay, a, b, mark_in + 1, mark_mid, false);
    DoWeighting(replay, c, b, mark_mid + 1, mark_out + 1, true);
    return true;
}

void GrabTool::Draw()
{
    if (!m_enabled) return;
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    glColorBlue();
    glVertex2f(m_v0.x, m_v0.y);
    glVertex2f(m_v1.x, m_v1.y);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

// ---------
// BrushTool
// ---------

void BrushTool::OnMouseUp(const glm::vec2& mouse)
{
    m_enabled = false;
    m_current_mouse = mouse;
    if (ApplyMutation(replayengine::MutableCurrentView())) lazer_judgement::mark_dirty();
    m_targets.clear();
}

void BrushTool::OnMouseDown(const glm::vec2& mouse)
{
    m_enabled = true;
    m_down_mouse = mouse;
    m_current_mouse = mouse;
    replayengine::DuplicateCurrentView();
    CaptureTargets(*replayengine::CurrentView(), mouse);
}

void BrushTool::OnMouseMove(const glm::vec2& mouse)
{
    m_current_mouse = mouse;
    if (m_enabled) {
        if (ApplyMutation(replayengine::MutableCurrentView())) lazer_judgement::mark_dirty();
    }
}

bool BrushTool::ApplyMutation(replayengine::Replay* replay)
{
    if (replay == nullptr || m_targets.empty()) {
        return false;
    }

    const glm::vec2 delta = m_current_mouse - m_down_mouse;
    bool changed = false;
    auto& frames = replay->mut_frames();
    for (const BrushTarget& target : m_targets) {
        if (target.index < 0 || static_cast<size_t>(target.index) >= frames.size()) continue;
        const glm::vec2 next_position = target.original_position + delta * target.weight;
        if (glm::distance(frames[target.index].p, next_position) > 0.001f) {
            frames[target.index].p = next_position;
            changed = true;
        }
    }
    return changed;
}

void BrushTool::CaptureTargets(const replayengine::Replay& replay, const glm::vec2& mouse)
{
    m_targets.clear();

    const float radius = EffectiveRadius();
    const auto& frames = replay.frames();
    for (I64 i = 0; i < static_cast<I64>(frames.size()); ++i) {
        const replayengine::ReplayFrame& frame = frames[i];
        if (frame.ms < editor_window_start_ms || frame.ms > editor_window_end_ms) continue;

        const float d = glm::distance(frame.p, mouse);
        if (d > radius) continue;

        BrushTarget target;
        target.index = i;
        target.original_position = frame.p;
        target.weight = 1.f - d / radius;
        m_targets.push_back(target);
    }
}

float BrushTool::EffectiveRadius() const
{
    return std::max(1.f, brush_radius * 0.5f);
}

void BrushTool::Draw()
{
    constexpr int N = 20;
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    glColorBlue();
    constexpr float two_pi = 6.28318f;
    const float radius = EffectiveRadius();
    for (int i = 0; i < N; ++i) {
        const float angle = RATIO(i, N) * two_pi;
        glVertex2f(m_current_mouse.x + glm::cos(angle) * radius, m_current_mouse.y + glm::sin(angle) * radius);
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

}  // namespace tool
