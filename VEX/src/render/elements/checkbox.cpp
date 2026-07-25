#include <include/render/settings/functions.h>

void render_checkmark(ImDrawList* draw_list, ImVec2 pos, ImU32 col, float sz)
{
    float thickness = SCALE(1.f);
    pos += ImVec2(thickness * 0.25f, thickness * 0.25f);

    float third = sz / 3.0f;
    float bx = pos.x + third;
    float by = pos.y + sz - third * 0.5f;
    draw_list->PathLineTo(ImVec2(bx - third, by - third));
    draw_list->PathLineTo(ImVec2(bx, by));
    draw_list->PathLineTo(ImVec2(bx + third * 2.0f, by - third * 2.0f));
    draw_list->PathStroke(col, 0, thickness);
}

bool c_gui::checkbox(std::string_view label, bool* callback)
{
    struct checkbox_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float radius = 0.f;
        float rounding = 0.f;
        float alpha[2] = { 0.f, 0.f };
    };

    ImGuiWindow* window = GetCurrentWindow();
    bool& favorite_state = var->gui.child_states[window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->checkbox.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->checkbox.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - SCALE(elements->checkbox.height), rect.Min.y), rect.Max);

    ItemSize(total, 0);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    checkbox_state* state = gui->anim_container(&state, id);
    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, *callback ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        state->alpha[0] = ImClamp(state->alpha[0] + (gui->fixed_speed(6.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->alpha[1] = ImClamp(state->alpha[1] + (gui->fixed_speed(10.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->radius = ImLerp(state->radius, *callback ? clickable.GetWidth() / 2.f : 0.f, gui->fixed_speed(12.f));
        state->rounding = ImLerp(state->rounding, *callback ? SCALE(elements->widgets.rounding) : SCALE(30.f), gui->fixed_speed(12.f));
    }

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), draw->get_clr(ImVec4(1.f, 1.f, 1.f, state->alpha[0])), state->rounding);
    const int vtx_end = window->DrawList->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, clickable.Min, clickable.Max, draw->get_clr(clr->accent), draw->get_clr(ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, 1.f)));

    window->DrawList->PushClipRect(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), true);
    render_checkmark(window->DrawList, clickable.Min + SCALE(3.f, 3.f), draw->get_clr(clr->widgets.label, state->alpha[1]), SCALE(8.f));
    window->DrawList->PopClipRect();

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    if (var->gui.content_alpha <= 0.1f)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->radius = 0.f;
        state->rounding = 0.f;
        state->alpha[0] = 0.f;
        state->alpha[1] = 0.f;
    }

    return pressed;
}

bool c_gui::checkbox_with_color(std::string_view label, bool* callback, float col[4], bool alpha)
{
    struct checkbox_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float radius = 0.f;
        float rounding = 0.f;
        float alpha[2] = { 0.f, 0.f };
    };

    ImGuiWindow* window = GetCurrentWindow();
    bool& favorite_state = var->gui.child_states[window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->checkbox.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->checkbox.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - SCALE(elements->checkbox.height), rect.Min.y), rect.Max);

    ItemSize(total, 0);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(clickable, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    checkbox_state* state = gui->anim_container(&state, id);
    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, *callback ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        state->alpha[0] = ImClamp(state->alpha[0] + (gui->fixed_speed(6.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->alpha[1] = ImClamp(state->alpha[1] + (gui->fixed_speed(10.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->radius = ImLerp(state->radius, *callback ? clickable.GetWidth() / 2.f : 0.f, gui->fixed_speed(12.f));
        state->rounding = ImLerp(state->rounding, *callback ? SCALE(elements->widgets.rounding) : SCALE(30.f), gui->fixed_speed(12.f));
    }

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), draw->get_clr(ImVec4(1.f, 1.f, 1.f, state->alpha[0])), state->rounding);
    const int vtx_end = window->DrawList->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, clickable.Min, clickable.Max, draw->get_clr(clr->accent), draw->get_clr(ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, 1.f)));

    window->DrawList->PushClipRect(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), true);
    render_checkmark(window->DrawList, clickable.Min + SCALE(3.f, 3.f), draw->get_clr(clr->widgets.label, state->alpha[1]), SCALE(8.f));
    window->DrawList->PopClipRect();

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    ImVec2 stored_pos = GetCursorScreenPos();

    SetCursorScreenPos(clickable.Min - SCALE(25, -2));
    gui->color_edit((std::stringstream{} << label << "picker").str(), col, alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_None);

    SetCursorScreenPos(stored_pos);

    if (var->gui.content_alpha <= 0.1f)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->radius = 0.f;
        state->rounding = 0.f;
        state->alpha[0] = 0.f;
        state->alpha[1] = 0.f;
    }

    return pressed;
}

bool c_gui::checkbox_with_color(std::string_view label, bool* callback, float colors[][4], int color_count, bool alpha)
{
    struct checkbox_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float radius = 0.f;
        float rounding = 0.f;
        float alpha[2] = { 0.f, 0.f };
    };

    ImGuiWindow* window = GetCurrentWindow();
    bool& favorite_state = var->gui.child_states[window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->checkbox.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->checkbox.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - SCALE(elements->checkbox.height), rect.Min.y), rect.Max);

    ItemSize(total, 0);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(clickable, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    checkbox_state* state = gui->anim_container(&state, id);
    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, *callback ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        state->alpha[0] = ImClamp(state->alpha[0] + (gui->fixed_speed(6.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->alpha[1] = ImClamp(state->alpha[1] + (gui->fixed_speed(10.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->radius = ImLerp(state->radius, *callback ? clickable.GetWidth() / 2.f : 0.f, gui->fixed_speed(12.f));
        state->rounding = ImLerp(state->rounding, *callback ? SCALE(elements->widgets.rounding) : SCALE(30.f), gui->fixed_speed(12.f));
    }

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), draw->get_clr(ImVec4(1.f, 1.f, 1.f, state->alpha[0])), state->rounding);
    const int vtx_end = window->DrawList->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, clickable.Min, clickable.Max, draw->get_clr(clr->accent), draw->get_clr(ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, 1.f)));

    window->DrawList->PushClipRect(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), true);
    render_checkmark(window->DrawList, clickable.Min + SCALE(3.f, 3.f), draw->get_clr(clr->widgets.label, state->alpha[1]), SCALE(8.f));
    window->DrawList->PopClipRect();

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    // Save current position
    ImVec2 stored_pos = GetCursorScreenPos();

    // Render multiple color pickers side by side
    const float picker_width = SCALE(25.0f);
    const float picker_spacing = SCALE(5.0f);

    for (int i = 0; i < color_count && i < 4; ++i) // Limit to 4 color pickers to avoid overload
    {
        float offset_x = picker_width * i + picker_spacing * i;
        SetCursorScreenPos(clickable.Min - ImVec2(picker_width + offset_x, -SCALE(2.0f)));

        std::stringstream picker_id;
        picker_id << label << "_picker_" << i;

        gui->color_edit(picker_id.str(), colors[i], alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_None);
    }

    // Restore position
    SetCursorScreenPos(stored_pos);

    if (var->gui.content_alpha <= 0.1f)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->radius = 0.f;
        state->rounding = 0.f;
        state->alpha[0] = 0.f;
        state->alpha[1] = 0.f;
    }

    return pressed;
}

bool c_gui::checkbox_with_color(std::string_view label, bool* callback, float* color1, float* color2, bool alpha)
{
    struct checkbox_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float radius = 0.f;
        float rounding = 0.f;
        float alpha[2] = { 0.f, 0.f };
    };

    ImGuiWindow* window = GetCurrentWindow();
    bool& favorite_state = var->gui.child_states[window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->checkbox.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->checkbox.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - SCALE(elements->checkbox.height), rect.Min.y), rect.Max);

    ItemSize(total, 0);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(clickable, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    checkbox_state* state = gui->anim_container(&state, id);
    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, *callback ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        state->alpha[0] = ImClamp(state->alpha[0] + (gui->fixed_speed(6.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->alpha[1] = ImClamp(state->alpha[1] + (gui->fixed_speed(10.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->radius = ImLerp(state->radius, *callback ? clickable.GetWidth() / 2.f : 0.f, gui->fixed_speed(12.f));
        state->rounding = ImLerp(state->rounding, *callback ? SCALE(elements->widgets.rounding) : SCALE(30.f), gui->fixed_speed(12.f));
    }

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), draw->get_clr(ImVec4(1.f, 1.f, 1.f, state->alpha[0])), state->rounding);
    const int vtx_end = window->DrawList->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, clickable.Min, clickable.Max, draw->get_clr(clr->accent), draw->get_clr(ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, 1.f)));

    window->DrawList->PushClipRect(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), true);
    render_checkmark(window->DrawList, clickable.Min + SCALE(3.f, 3.f), draw->get_clr(clr->widgets.label, state->alpha[1]), SCALE(8.f));
    window->DrawList->PopClipRect();

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    // Save current position
    ImVec2 stored_pos = GetCursorScreenPos();

    // First color picker (color1)
    SetCursorScreenPos(clickable.Min - SCALE(25, -2));
    gui->color_edit((std::stringstream{} << label << "_picker1").str(), color1, alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_None);

    // Second color picker (color2) - only if provided
    if (color2 != nullptr)
    {
        SetCursorScreenPos(clickable.Min - SCALE(55, -2)); // Further left
        gui->color_edit((std::stringstream{} << label << "_picker2").str(), color2, alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_None);
    }

    // Restore position
    SetCursorScreenPos(stored_pos);

    if (var->gui.content_alpha <= 0.1f)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->radius = 0.f;
        state->rounding = 0.f;
        state->alpha[0] = 0.f;
        state->alpha[1] = 0.f;
    }

    return pressed;
}

bool c_gui::checkbox_with_key(std::string_view label, bool* callback, keybind_state* sstate)
{
    struct checkbox_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float radius = 0.f;
        float rounding = 0.f;
        float alpha[2] = { 0.f, 0.f };
    };

    ImGuiWindow* window = GetCurrentWindow();
    bool& favorite_state = var->gui.child_states[window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->checkbox.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->checkbox.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - SCALE(elements->checkbox.height), rect.Min.y), rect.Max);

    ItemSize(total, 0);
    if (!ItemAdd(total, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(clickable, id, &hovered, &held);
    if (pressed)
        *callback = !(*callback);

    checkbox_state* state = gui->anim_container(&state, id);
    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, *callback ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        state->alpha[0] = ImClamp(state->alpha[0] + (gui->fixed_speed(6.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->alpha[1] = ImClamp(state->alpha[1] + (gui->fixed_speed(10.f) * (*callback ? 1.f : -1.f)), 0.f, 1.f);
        state->radius = ImLerp(state->radius, *callback ? clickable.GetWidth() / 2.f : 0.f, gui->fixed_speed(12.f));
        state->rounding = ImLerp(state->rounding, *callback ? SCALE(elements->widgets.rounding) : SCALE(30.f), gui->fixed_speed(12.f));
    }

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));

    const int vtx_start = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), draw->get_clr(ImVec4(1.f, 1.f, 1.f, state->alpha[0])), state->rounding);
    const int vtx_end = window->DrawList->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vtx_start, vtx_end, clickable.Min, clickable.Max, draw->get_clr(clr->accent), draw->get_clr(ImVec4(clr->accent.x * 0.2f, clr->accent.y * 0.2f, clr->accent.z * 0.2f, 1.f)));

    window->DrawList->PushClipRect(clickable.GetCenter() - ImVec2(state->radius, state->radius), clickable.GetCenter() + ImVec2(state->radius, state->radius), true);
    render_checkmark(window->DrawList, clickable.Min + SCALE(3.f, 3.f), draw->get_clr(clr->widgets.label, state->alpha[1]), SCALE(8.f));
    window->DrawList->PopClipRect();

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label.data(), NULL, NULL, ImVec2(0.f, 0.5f));

    ImVec2 stored_pos = GetCursorScreenPos();

    SetCursorScreenPos(clickable.Min - SCALE(25, -2));

    gui->keybind((std::stringstream{} << id << "KEY").str(), sstate);

    SetCursorScreenPos(stored_pos);

    if (var->gui.content_alpha <= 0.1f)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->radius = 0.f;
        state->rounding = 0.f;
        state->alpha[0] = 0.f;
        state->alpha[1] = 0.f;
    }

    return pressed;
}