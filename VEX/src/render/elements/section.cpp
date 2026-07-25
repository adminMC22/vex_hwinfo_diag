#include <include/render/settings/functions.h>

bool c_gui::section(std::string_view icon, int section_id, int& section_variable)
{
    struct section_state
    {
        ImVec4 icon_col[2] = { clr->section.icon, clr->section.icon };
        float shadow_offset = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID((std::stringstream{} << "section_" << icon).str().c_str());
    const bool selected = section_id == section_variable;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + SCALE(elements->section.size));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);

    if (pressed)
        section_variable = section_id;


    section_state* state = gui->anim_container(&state, id);
    state->icon_col[0] = ImLerp(state->icon_col[0], selected ? clr->accent : clr->section.icon, gui->fixed_speed(8.f));
    state->icon_col[1] = ImLerp(state->icon_col[1], selected ? ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, clr->accent.w) : clr->section.icon, gui->fixed_speed(12.f));
    state->shadow_offset = ImLerp(state->shadow_offset, selected ? elements->section.shadow_offset : 0.f, gui->fixed_speed(12.f));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    draw->render_text(window->DrawList, var->font.icons[1], rect.Min, rect.Max, draw->get_clr(ImVec4(1.f, 1.f, 1.f, 1.f)), icon.data(), NULL, NULL, ImVec2(0.5f, 0.5f));
    const int vtx_end = window->DrawList->VtxBuffer.Size;

    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, rect.Min + SCALE(0, 10), ImVec2(rect.Min.x, rect.Max.y - SCALE(10)), draw->get_clr(state->icon_col[0]), draw->get_clr(state->icon_col[1]));

    if (state->shadow_offset > 2.f)
        window->DrawList->AddShadowRect(rect.Min, rect.Max, draw->get_clr(clr->accent, 0.2f), SCALE(state->shadow_offset), ImVec2(0, 0), ImDrawFlags_ShadowCutOutShapeBackground, SCALE(elements->section.rounding));

    return pressed;
}
