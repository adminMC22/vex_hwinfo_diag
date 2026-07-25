#include <include/render/settings/functions.h>

// Use ImGui's built-in SliderBehaviorT template function instead of our custom implementation
// This avoids the template instantiation issues with ScaleRatioFromValueT/ScaleValueFromRatioT

bool slider_behavior(const ImRect& bb, ImGuiID id, ImGuiDataType data_type, void* p_v, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, ImRect* out_grab_bb)
{
    // Read imgui.cpp "API BREAKING CHANGES" section for 1.78 if you hit this assert.
    IM_ASSERT((flags == 1 || (flags & ImGuiSliderFlags_InvalidMask_) == 0) && "Invalid ImGuiSliderFlags flag!  Has the 'float power' argument been mistakenly cast to flags? Call function with ImGuiSliderFlags_Logarithmic flags instead.");

    switch (data_type)
    {
    case ImGuiDataType_S8: { ImS32 v32 = (ImS32) * (ImS8*)p_v;  bool r = SliderBehaviorT<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS8*)p_min, *(const ImS8*)p_max, format, flags, out_grab_bb); if (r) *(ImS8*)p_v = (ImS8)v32;  return r; }
    case ImGuiDataType_U8: { ImU32 v32 = (ImU32) * (ImU8*)p_v;  bool r = SliderBehaviorT<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU8*)p_min, *(const ImU8*)p_max, format, flags, out_grab_bb); if (r) *(ImU8*)p_v = (ImU8)v32;  return r; }
    case ImGuiDataType_S16: { ImS32 v32 = (ImS32) * (ImS16*)p_v; bool r = SliderBehaviorT<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS16*)p_min, *(const ImS16*)p_max, format, flags, out_grab_bb); if (r) *(ImS16*)p_v = (ImS16)v32; return r; }
    case ImGuiDataType_U16: { ImU32 v32 = (ImU32) * (ImU16*)p_v; bool r = SliderBehaviorT<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU16*)p_min, *(const ImU16*)p_max, format, flags, out_grab_bb); if (r) *(ImU16*)p_v = (ImU16)v32; return r; }
    case ImGuiDataType_S32:
        IM_ASSERT(*(const ImS32*)p_min >= INT_MIN / 2 && *(const ImS32*)p_max <= INT_MAX / 2);
        return SliderBehaviorT<ImS32, ImS32, float >(bb, id, data_type, (ImS32*)p_v, *(const ImS32*)p_min, *(const ImS32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U32:
        IM_ASSERT(*(const ImU32*)p_max <= UINT_MAX / 2);
        return SliderBehaviorT<ImU32, ImS32, float >(bb, id, data_type, (ImU32*)p_v, *(const ImU32*)p_min, *(const ImU32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_S64:
        IM_ASSERT(*(const ImS64*)p_min >= LLONG_MIN / 2 && *(const ImS64*)p_max <= LLONG_MAX / 2);
        return SliderBehaviorT<ImS64, ImS64, double>(bb, id, data_type, (ImS64*)p_v, *(const ImS64*)p_min, *(const ImS64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U64:
        IM_ASSERT(*(const ImU64*)p_max <= ULLONG_MAX / 2);
        return SliderBehaviorT<ImU64, ImS64, double>(bb, id, data_type, (ImU64*)p_v, *(const ImU64*)p_min, *(const ImU64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Float:
        IM_ASSERT(*(const float*)p_min >= -FLT_MAX / 2.0f && *(const float*)p_max <= FLT_MAX / 2.0f);
        return SliderBehaviorT<float, float, float >(bb, id, data_type, (float*)p_v, *(const float*)p_min, *(const float*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Double:
        IM_ASSERT(*(const double*)p_min >= -DBL_MAX / 2.0f && *(const double*)p_max <= DBL_MAX / 2.0f);
        return SliderBehaviorT<double, double, double>(bb, id, data_type, (double*)p_v, *(const double*)p_min, *(const double*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_COUNT: break;
    }
    IM_ASSERT(0);
    return false;
}

bool slider_scalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, bool dpi)
{
    struct slider_state
    {
        float slide = SCALE(10.f);
        ImVec4 label_clr = clr->widgets.label_inactive;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->slider.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->slider.height) / 2));
    // Use more area to allow full range (increase from 0.42f to 0.65f)
    const float extended_multiplier = 0.65f; // More area than default 0.42f
    const ImRect clickable(ImVec2(rect.Max.x - round(rect.GetWidth() * extended_multiplier), rect.Min.y), rect.Max);

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(total, 0);
    if (!ItemAdd(total, id, &clickable, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(clickable, id, g.LastItemData.InFlags);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        // Tabbing or CTRL-clicking on Slider turns it into an input box
        const bool clicked = hovered && IsMouseClicked(0, 0, id);
        const bool make_active = (clicked || g.NavActivateId == id);
        if (make_active && clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if ((clicked && g.IO.KeyCtrl) || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;

        if (make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }

    // Use full clickable area for maximum extension
    const ImRect slider_area = clickable;

    // Slider behavior - use full area without padding to reach extremes
    ImRect grab_bb;
    const bool value_changed = slider_behavior(slider_area, id, data_type, p_data, p_min, p_max, format, flags, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);

    slider_state* state = gui->anim_container(&state, id);

    if (var->gui.content_alpha >= 0.5f)
    {
        state->label_clr = ImLerp(state->label_clr, IsItemActive() ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
        // Grab position relative to full slider area
        state->slide = ImLerp(state->slide, grab_bb.GetCenter().x - slider_area.Min.x, gui->fixed_speed(20.f));
    }

    // Visual track area (with small padding only for aesthetics)
    const float visual_padding = SCALE(5.0f);
    const ImVec2 track_min = ImVec2(slider_area.Min.x + visual_padding, slider_area.GetCenter().y - SCALE(3.0f));
    const ImVec2 track_max = ImVec2(slider_area.Max.x - visual_padding, slider_area.GetCenter().y + SCALE(3.0f));

    // Grab position mapped to visual track (both using same coordinate system)
    const float grab_center_x = slider_area.Min.x + state->slide;
    const float track_width = track_max.x - track_min.x;
    const float slider_width = slider_area.GetWidth();
    const float track_ratio = (grab_center_x - slider_area.Min.x) / slider_width;
    const float visual_grab_x = track_min.x + (track_ratio * track_width);

    // Filled track (from start to current position)
    if (visual_grab_x > track_min.x)
    {
        window->DrawList->AddRectFilled(track_min, ImVec2(visual_grab_x, track_max.y), draw->get_clr(clr->accent), SCALE(elements->widgets.rounding));
    }

    // Empty track (from current position to end)
    if (visual_grab_x < track_max.x)
    {
        window->DrawList->AddRectFilled(ImVec2(visual_grab_x, track_min.y), track_max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));
    }

    // Draw the grab (slider handle) - use the SAME visual position as the track
    const float grab_width = SCALE(elements->slider.grab_width);
    const ImVec2 grab_min = ImVec2(visual_grab_x - grab_width * 0.5f, slider_area.Min.y);
    const ImVec2 grab_max = ImVec2(visual_grab_x + grab_width * 0.5f, slider_area.Max.y);
    window->DrawList->AddRectFilled(grab_min, grab_max, draw->get_clr(clr->widgets.label), SCALE(elements->widgets.rounding));

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label, NULL, NULL, ImVec2(0.f, 0.5f));
    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), ImVec2(clickable.Min.x - SCALE(10), total.Max.y) - SCALE(elements->widgets.label_padding), draw->get_clr(clr->widgets.label), value_buf, value_buf_end, NULL, ImVec2(1.f, 0.5f));

    if (var->gui.content_alpha <= 0.1f && !dpi)
    {
        state->label_clr = clr->widgets.label_inactive;
        state->slide = SCALE(10.f);
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (temp_input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
    return value_changed;
}

bool c_gui::slider_float(std::string_view label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    bool& favorite_state = var->gui.child_states[GetCurrentWindow()->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    return slider_scalar(label.data(), ImGuiDataType_Float, v, &v_min, &v_max, format, flags, false);
}

bool c_gui::slider_int(std::string_view label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags, bool dpi)
{
    bool& favorite_state = var->gui.child_states[GetCurrentWindow()->ChildId];

    if (!dpi && !favorite_state && var->gui.favorite_childs) return false;

    return slider_scalar(label.data(), ImGuiDataType_S32, v, &v_min, &v_max, format, flags, dpi);
}

static void scolor_edit_restore_hs(const float* col, float* H, float* S, float* V)
{
    ImGuiContext& g = *GImGui;

    if (*S == 0.0f || (*H == 0.0f && g.ColorEditSavedHue == 1))
        *H = g.ColorEditSavedHue;

    if (*V == 0.0f) *S = g.ColorEditSavedSat;
}

bool c_gui::hue_slider(std::string_view label, float col[4])
{
    struct hue_state
    {
        ImVec4 text;
        float hue, circle;
        float bar0_line_x;
    };

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();

    ImDrawList* draw_list = window->DrawList;
    ImGuiStyle& style = g.Style;
    ImGuiIO& io = g.IO;

    ImVec2 pos = window->DC.CursorPos;

    float content = GetContentRegionAvail().x;
    float rect_height = 10.f;
    float hue_height = 2.f;

    const ImRect slider(pos, pos + ImVec2(content, SCALE(rect_height)));

    ItemSize(slider);

    if (!ItemAdd(slider, GetID(label.data()))) return false;

    float H = col[0], S = col[1], V = col[2];
    float R = col[0], G = col[1], B = col[2];
    bool value_changed = false;

    ColorConvertRGBtoHSV(R, G, B, H, S, V);
    scolor_edit_restore_hs(col, &H, &S, &V);

    SetCursorScreenPos(ImVec2(pos.x, slider.GetCenter().y - SCALE(rect_height) / 2));
    InvisibleButton(label.data(), ImVec2(content, SCALE(rect_height)));

    if (IsItemActive())
    {
        float new_H = 1.f - ImSaturate((io.MousePos.x - pos.x) / (content - 1));
        float delta_H = new_H - H;
        if (abs(delta_H) > 0.5f) delta_H -= round(delta_H);
        H = fmod(H + delta_H * 0.5f + 1.0f, 1.0f);
        value_changed = true;
    }

    ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);

    const int style_alpha8 = IM_F32_TO_INT8_SAT(style.Alpha);
    const ImU32 col_hues[7] = { IM_COL32(255,0,0,style_alpha8), IM_COL32(255,0,255,style_alpha8), IM_COL32(0,0,255,style_alpha8),IM_COL32(0,255,255,style_alpha8), IM_COL32(0,255,0,style_alpha8), IM_COL32(255,255,0,style_alpha8), IM_COL32(255,0,0,style_alpha8) };

    hue_state* state = gui->anim_container(&state, GetID(label.data()));
    //state->text = ImLerp(state->text, IsItemActive() ? clr->c_text.text_active : clr->c_text.text, gui->fixed_speed(set->c_element.speed_animation));
    state->hue = ImLerp(state->hue, ImClamp(IM_ROUND(pos.x + (1.f - H) * content), pos.x + 4, pos.x + content - 4) - pos.x, gui->fixed_speed(20.f));

    for (int i = 0; i < 6; ++i)
        draw->rect_filled_multi_color(draw_list, ImVec2(pos.x + i * (content / 6) - (i == 5 ? 1 : 0), slider.Min.y + SCALE(3)), ImVec2(pos.x + (i + 1) * (content / 6) + (i == 0 ? 1 : 0), slider.Max.y - SCALE(3)), col_hues[i], col_hues[i + 1], col_hues[i + 1], col_hues[i], SCALE(2.f), i == 0 ? ImDrawFlags_RoundCornersLeft : i == 5 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);
    window->DrawList->AddRectFilled(slider.Min + ImVec2(state->hue - SCALE(3), 0), ImVec2(slider.Min.x + state->hue + SCALE(3), slider.Max.y), draw->get_clr(ImVec4(1.f, 1.f, 1.f, 1.f)), SCALE(elements->widgets.rounding));

    return value_changed;
}