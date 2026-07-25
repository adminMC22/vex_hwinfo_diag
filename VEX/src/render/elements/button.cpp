#include <include/render/settings/functions.h>

bool c_gui::button(std::string_view label)
{
    bool& favorite_state = var->gui.child_states[GetCurrentWindow()->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    struct button_state
    {
        bool clicked = false;
        float alpha = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->button.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->button.height) / 2));
    ItemSize(total, style.FramePadding.y);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held, 0);

    // Render
    button_state* state = gui->anim_container(&state, id);

    if (pressed)
        state->clicked = true;

    state->alpha = ImClamp(state->alpha + (gui->fixed_speed(8.f) * (state->clicked ? 1.f : -1.f)), 0.f, 1.f);

    if (state->alpha >= 0.9f)
        state->clicked = false;

    window->DrawList->AddRectFilled(rect.Min, rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));
    window->DrawList->AddRectFilled(rect.Min, rect.Max, draw->get_clr(clr->accent, state->alpha), SCALE(elements->widgets.rounding));
    draw->render_text(window->DrawList, var->font.inter[1], rect.Min - SCALE(0, 1), rect.Max - SCALE(0, 1), draw->get_clr(clr->widgets.label), label.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}
