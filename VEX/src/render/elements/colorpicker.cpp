#include <include/render/settings/functions.h>

void render_color_rect_with_alpha_checkboard(ImDrawList* draw_list, ImVec2 p_min, ImVec2 p_max, ImU32 col, float grid_step, ImVec2 grid_off, float rounding, ImDrawFlags flags)
{
    if ((flags & ImDrawFlags_RoundCornersMask_) == 0)
        flags = ImDrawFlags_RoundCornersDefault_;
    if (((col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) < 0xFF)
    {
        ImU32 col_bg1 = GetColorU32(ImAlphaBlendColors(IM_COL32(34, 32, 40, 255), col));
        ImU32 col_bg2 = GetColorU32(ImAlphaBlendColors(IM_COL32(10, 7, 19, 255), col));
        draw_list->AddRectFilled(p_min, p_max, col_bg1, rounding, flags);

        int yi = 0;
        for (float y = p_min.y + grid_off.y; y < p_max.y; y += grid_step, yi++)
        {
            float y1 = ImClamp(y, p_min.y, p_max.y), y2 = ImMin(y + grid_step, p_max.y);
            if (y2 <= y1)
                continue;
            for (float x = p_min.x + grid_off.x + (yi & 1) * grid_step; x < p_max.x; x += grid_step * 2.0f)
            {
                float x1 = ImClamp(x, p_min.x, p_max.x), x2 = ImMin(x + grid_step, p_max.x);
                if (x2 <= x1)
                    continue;
                ImDrawFlags cell_flags = ImDrawFlags_RoundCornersNone;
                if (y1 <= p_min.y) { if (x1 <= p_min.x) cell_flags |= ImDrawFlags_RoundCornersTopLeft; if (x2 >= p_max.x) cell_flags |= ImDrawFlags_RoundCornersTopRight; }
                if (y2 >= p_max.y) { if (x1 <= p_min.x) cell_flags |= ImDrawFlags_RoundCornersBottomLeft; if (x2 >= p_max.x) cell_flags |= ImDrawFlags_RoundCornersBottomRight; }

                // Combine flags
                cell_flags = (flags == ImDrawFlags_RoundCornersNone || cell_flags == ImDrawFlags_RoundCornersNone) ? ImDrawFlags_RoundCornersNone : (cell_flags & flags);
                draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col_bg2, rounding, cell_flags);
            }
        }
    }
    else
    {
        draw_list->AddRectFilled(p_min, p_max, col, rounding, flags);
    }
}

bool color_button(const char* desc_id, const ImVec4& col, bool opened)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID((std::stringstream{} << desc_id << "color_button").str().c_str());

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + SCALE(elements->colorpicker.size, elements->colorpicker.size));
    ItemSize(rect, 0.0f);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held);

    draw->render_text(window->DrawList, var->font.icons[9], rect.Min, rect.Max, draw->get_clr(col, 1.f), "A", NULL, NULL, ImVec2(0.5f, 0.5f));

    return pressed;
}

static void color_edit_restore_h(const float* col, float* H)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.ColorEditCurrentID != 0);
    if (g.ColorEditSavedID != g.ColorEditCurrentID || g.ColorEditSavedColor != ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0)))
        return;
    *H = g.ColorEditSavedHue;
}

static void color_edit_restore_hs(const float* col, float* H, float* S, float* V)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.ColorEditCurrentID != 0);
    if (g.ColorEditSavedID != g.ColorEditCurrentID || g.ColorEditSavedColor != ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0)))
        return;

    // When S == 0, H is undefined.
    // When H == 1 it wraps around to 0.
    if (*S == 0.0f || (*H == 0.0f && g.ColorEditSavedHue == 1))
        *H = g.ColorEditSavedHue;

    // When V == 0, S is undefined.
    if (*V == 0.0f)
        *S = g.ColorEditSavedSat;
}

ImVec4 imu32_to_imvec4(ImU32 color)
{
    float r = ((color >> 0) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    return ImVec4(r, g, b, a);
}

bool saturation_slider(std::string_view label, float* v, float v_min, float v_max, float power, ImU32 col)
{
    struct saturation_state
    {
        int current_section;
        bool active_dragging = false;
        float value = 0.f;
        float lerp = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    const ImVec2 pos = window->DC.CursorPos;
    const float width = GetContentRegionAvail().x;
    const ImRect rect(pos, pos + ImVec2(width, SCALE(15)));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    const float section_count = (v_max - v_min) / power + 1;
    const float section_size = rect.GetWidth() / section_count;
    bool value_changed = false;

    saturation_state* state = gui->anim_container(&state, id);

    state->current_section = (*v - v_min) / power;

    if (ItemHoverable(rect, id, 0) && g.IO.MouseDown[0])
        state->active_dragging = true;

    if (state->active_dragging && !g.IO.MouseDown[0])
        state->active_dragging = false;

    SetCursorScreenPos(ImVec2(rect.Min));
    InvisibleButton("DPI_BUTTON", rect.GetSize());

    if (state->active_dragging)
    {
        float mouse_pos = g.IO.MousePos.x - rect.Min.x;
        int new_section = static_cast<int>(mouse_pos / section_size);

        float section_center = (new_section + 0.5f) * section_size;
        if (mouse_pos >= section_center - section_size && mouse_pos <= section_center + section_size)
            state->current_section = ImClamp(new_section, 0, static_cast<int>(section_count - 1));
    }

    state->value = v_min + power * state->current_section;

    if (state->active_dragging)
    {
        *v = state->value;
        value_changed = true;
    }

    state->lerp = ImLerp(state->lerp, section_size * state->current_section, gui->fixed_speed(10.f));

    ImVec4 clr = imu32_to_imvec4(col);

    ImVec4 hue_color_f(clr.x, clr.y, clr.z, style.Alpha);
    ImVec4 hue_color_f2(clr.x, clr.y, clr.z, style.Alpha);
    ColorConvertRGBtoHSV(clr.x, clr.y, clr.z, hue_color_f2.x, hue_color_f2.y, hue_color_f2.z);

    for (int i = 1; i < v_max + 1; i++)
    {
        ColorConvertHSVtoRGB(hue_color_f2.x, (0.14f * i), 1, hue_color_f.x, hue_color_f.y, hue_color_f.z);
        window->DrawList->AddRectFilled(rect.Min + ImVec2((i - 1) * section_size, SCALE(2)), ImVec2(rect.Min.x + i * section_size, rect.Max.y - SCALE(2)), draw->get_clr(hue_color_f), SCALE(elements->widgets.rounding), i == 1 ? ImDrawFlags_RoundCornersLeft : i == 7 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);
    }

    ColorConvertHSVtoRGB(hue_color_f2.x, (0.14f * *v), 1, hue_color_f.x, hue_color_f.y, hue_color_f.z);
    
    window->DrawList->AddShadowRect(rect.Min + ImVec2(state->lerp, -SCALE(1)), ImVec2(rect.Min.x + state->lerp + section_size, rect.Max.y + SCALE(1)), draw->get_clr(ImVec4(0.f, 0.f, 0.f, 1.f)), SCALE(20.f), ImVec2(0, 0));
    window->DrawList->AddRectFilled(rect.Min + ImVec2(state->lerp, -SCALE(1)), ImVec2(rect.Min.x + state->lerp + section_size, rect.Max.y + SCALE(1)), draw->get_clr(clr), SCALE(elements->widgets.rounding));

    return value_changed;
}

bool color_picker(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col, int saturation, ImU32& hue_col)
{
    struct picker_state
    {
        float lerp[2] = { 0, 0 };
        float saved = 0.f;
        bool clicked = false;
    };

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImDrawList* draw_list = window->DrawList;
    ImGuiStyle& style = g.Style;
    ImGuiIO& io = g.IO;

    const float width = CalcItemWidth();
    const bool is_readonly = ((g.NextItemData.ItemFlags | g.CurrentItemFlags) & ImGuiItemFlags_ReadOnly) != 0;
    g.NextItemData.ClearFlags();

    PushID(label);
    const bool set_current_color_edit_id = (g.ColorEditCurrentID == 0);
    if (set_current_color_edit_id)
        g.ColorEditCurrentID = window->IDStack.back();
    BeginGroup();

    if (!(flags & ImGuiColorEditFlags_NoSidePreview))
        flags |= ImGuiColorEditFlags_NoSmallPreview;

    // Context menu: display and store options.
    if (!(flags & ImGuiColorEditFlags_NoOptions))
        ColorPickerOptionsPopup(col, flags);

    // Read stored options
    if (!(flags & ImGuiColorEditFlags_PickerMask_))
        flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_PickerMask_;
    if (!(flags & ImGuiColorEditFlags_InputMask_))
        flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_InputMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_InputMask_;
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_PickerMask_)); // Check that only 1 is selected
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));  // Check that only 1 is selected
    if (!(flags & ImGuiColorEditFlags_NoOptions))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_AlphaBar);

    // Setup
    int components = (flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4;
    bool alpha_bar = (flags & ImGuiColorEditFlags_AlphaBar) && !(flags & ImGuiColorEditFlags_NoAlpha);
    ImVec2 picker_pos = window->DC.CursorPos;

    ImVec2 hue_bar_pos = picker_pos + ImVec2(0, SCALE(elements->colorpicker.picker_size.y) + SCALE(elements->colorpicker.padding.x));
    ImVec2 alpha_bar_pos = hue_bar_pos + ImVec2(0, SCALE(elements->colorpicker.bar_size.y) + SCALE(elements->colorpicker.padding.x));

    float backup_initial_col[4];
    memcpy(backup_initial_col, col, components * sizeof(float));

    float H = col[0], S = col[1], V = col[2];
    float R = col[0], G = col[1], B = col[2];

    if (flags & ImGuiColorEditFlags_InputRGB)
    {
        // Hue is lost when converting from grayscale rgb (saturation=0). Restore it.
        ColorConvertRGBtoHSV(R, G, B, H, S, V);
        color_edit_restore_hs(col, &H, &S, &V);
    }
    else if (flags & ImGuiColorEditFlags_InputHSV)
    {
        ColorConvertHSVtoRGB(H, S, V, R, G, B);
    }

    picker_state* animstate = gui->anim_container(&animstate, GetID((std::stringstream{} << GetCurrentWindow()->ID << "saved_pos").str().c_str()));

    if (IsWindowHovered() && g.IO.MouseClicked[0])
    {
        if (g.IO.MouseDown[0])
            animstate->clicked = true;
    }
    if (!g.IO.MouseDown[0])
        animstate->clicked = false;

    bool value_changed = false, value_changed_h = false, value_changed_sv = false;
    PushItemFlag(ImGuiItemFlags_NoNav, true);
    // SV rectangle logic
    InvisibleButton("sv", SCALE(elements->colorpicker.picker_size));
    if (IsItemActive() && !is_readonly)
    {
        S = ImSaturate((io.MousePos.x - picker_pos.x) / (SCALE(elements->colorpicker.picker_size.x) - 1)) * (0.14f * saturation);
        animstate->saved = io.MousePos.x;
        V = 1.0f - ImSaturate((io.MousePos.y - picker_pos.y) / (SCALE(elements->colorpicker.picker_size.y) - 1));
        color_edit_restore_h(col, &H); // Greatly reduces hue jitter and reset to 0 when hue == 255 and color is rapidly modified using SV square.
        value_changed = value_changed_sv = true;
    }
    else if (animstate->clicked)
    {
        S = ImSaturate((animstate->saved - picker_pos.x) / (SCALE(elements->colorpicker.picker_size.x) - 1)) * (0.14f * saturation);
        value_changed = value_changed_sv = true;
    }

    // Hue bar logic
    SetCursorScreenPos(hue_bar_pos);
    InvisibleButton("hue", SCALE(elements->colorpicker.bar_size));
    if (IsItemActive() && !is_readonly)
    {
        H = 1.0f - ImSaturate((io.MousePos.x - picker_pos.x) / (SCALE(elements->colorpicker.bar_size.x) - 1));
        value_changed = value_changed_h = true;
    }

    // Alpha bar logic
    if (alpha_bar)
    {
        SetCursorScreenPos(alpha_bar_pos);
        InvisibleButton("alpha", SCALE(elements->colorpicker.bar_size));
        if (IsItemActive())
        {
            col[3] = ImSaturate((io.MousePos.x - picker_pos.x) / (SCALE(elements->colorpicker.bar_size.x) - 1));
            value_changed = true;
        }
    }
    PopItemFlag(); // ImGuiItemFlags_NoNav

    // Convert back color to RGB
    if (value_changed_h || value_changed_sv)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);
            g.ColorEditSavedHue = H;
            g.ColorEditSavedSat = S;
            g.ColorEditSavedID = g.ColorEditCurrentID;
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0));
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            col[0] = H;
            col[1] = S;
            col[2] = V;
        }
    }

    // R,G,B and H,S,V slider color editor
    bool value_changed_fix_hue_wrap = false;

    // Try to cancel hue wrap (after ColorEdit4 call), if any
    if (value_changed_fix_hue_wrap && (flags & ImGuiColorEditFlags_InputRGB))
    {
        float new_H, new_S, new_V;
        ColorConvertRGBtoHSV(col[0], col[1], col[2], new_H, new_S, new_V);
        if (new_H <= 0 && H > 0)
        {
            if (new_V <= 0 && V != new_V)
                ColorConvertHSVtoRGB(H, S, new_V <= 0 ? V * 0.5f : new_V, col[0], col[1], col[2]);
            else if (new_S <= 0)
                ColorConvertHSVtoRGB(H, new_S <= 0 ? S * 0.5f : new_S, new_V, col[0], col[1], col[2]);
        }
    }

    if (value_changed)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            R = col[0];
            G = col[1];
            B = col[2];
            ColorConvertRGBtoHSV(R, G, B, H, S, V);
            color_edit_restore_hs(col, &H, &S, &V);   // Fix local Hue as display below will use it immediately.
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            H = col[0];
            S = col[1];
            V = col[2];
            ColorConvertHSVtoRGB(H, S, V, R, G, B);
        }
    }

    if ((flags & ImGuiColorEditFlags_NoInputs) == 0)
    {
        ImGuiColorEditFlags sub_flags_to_forward = ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_InputMask_ | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_AlphaPreviewHalf;
        ImGuiColorEditFlags sub_flags = (flags & sub_flags_to_forward) | ImGuiColorEditFlags_NoPicker;
        if (flags & ImGuiColorEditFlags_DisplayHex || (flags & ImGuiColorEditFlags_DisplayMask_) == 0)
        {
            SetCursorPosY(GetCursorPosY() + SCALE(10));
            value_changed |= gui->color_edit("##hex", col, sub_flags | ImGuiColorEditFlags_DisplayHex);
        }
    }

    const int style_alpha8 = IM_F32_TO_INT8_SAT(style.Alpha);
    const ImU32 col_black = IM_COL32(0, 0, 0, style_alpha8);
    const ImU32 col_white = IM_COL32(255, 255, 255, style_alpha8);
    const ImU32 col_midgrey = IM_COL32(128, 128, 128, style_alpha8);
    const ImU32 col_hues[6 + 1] = {
        IM_COL32(255,0,0,style_alpha8),    // 7
        IM_COL32(255,0,255,style_alpha8),  // 6
        IM_COL32(0,0,255,style_alpha8),    // 5
        IM_COL32(0,255,255,style_alpha8),  // 4
        IM_COL32(0,255,0,style_alpha8),    // 3
        IM_COL32(255,255,0,style_alpha8),  // 2
        IM_COL32(255,0,0,style_alpha8)     // 1
    };

    ImVec4 hue_color_f(1, 1, 1, style.Alpha); ColorConvertHSVtoRGB(H, 1 * (0.14f * saturation), 1, hue_color_f.x, hue_color_f.y, hue_color_f.z);
    ImU32 hue_color32 = ColorConvertFloat4ToU32(hue_color_f);
    ImU32 user_col32_striped_of_alpha = ColorConvertFloat4ToU32(ImVec4(R, G, B, style.Alpha)); // Important: this is still including the main rendering/style alpha!!
    hue_col = hue_color32;

    ImVec2 sv_cursor_pos;

    // Render SV Square
    draw->rect_filled_multi_color(draw_list, picker_pos, picker_pos + SCALE(elements->colorpicker.picker_size), col_white, hue_color32, hue_color32, col_white, SCALE(elements->colorpicker.rounding + 1));
    draw->rect_filled_multi_color(draw_list, picker_pos, picker_pos + SCALE(elements->colorpicker.picker_size) + SCALE(0, 1), 0, 0, col_black, col_black, SCALE(elements->colorpicker.rounding));

    sv_cursor_pos.x = ImClamp(IM_ROUND(picker_pos.x + ImSaturate(S / (0.14f * saturation)) * SCALE(elements->colorpicker.picker_size.x)), picker_pos.x + 2, picker_pos.x + SCALE(elements->colorpicker.picker_size.x) - 2); // Sneakily prevent the circle to stick out too much
    sv_cursor_pos.y = ImClamp(IM_ROUND(picker_pos.y + ImSaturate(1 - V) * SCALE(elements->colorpicker.picker_size.y)), picker_pos.y + 2, picker_pos.y + SCALE(elements->colorpicker.picker_size.y) - 2);

    // Render Hue Bar
    for (int i = 0; i < 6; ++i)
        draw->rect_filled_multi_color(draw_list, hue_bar_pos + ImVec2(i * (SCALE(elements->colorpicker.bar_size.x) / 6) - (i == 5 ? 1 : 0), 0), hue_bar_pos + ImVec2((i + 1) * (SCALE(elements->colorpicker.bar_size.x) / 6) + (i == 0 ? 1 : 0), SCALE(elements->colorpicker.bar_size.y)), col_hues[i], col_hues[i + 1], col_hues[i + 1], col_hues[i], SCALE(elements->widgets.rounding), i == 0 ? ImDrawFlags_RoundCornersLeft : i == 5 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);

    float hue_bar_circle_pos = IM_ROUND((1.0f - H) * SCALE(elements->colorpicker.bar_size.x));
    hue_bar_circle_pos = ImClamp(hue_bar_circle_pos, 4.f, SCALE(elements->colorpicker.bar_size.x) - 5.f);
    animstate->lerp[0] = ImLerp(animstate->lerp[0], hue_bar_circle_pos, gui->fixed_speed(20.f));
    draw_list->AddShadowCircle(hue_bar_pos + ImVec2(animstate->lerp[0], SCALE(elements->colorpicker.bar_size.y) / 2), SCALE(elements->colorpicker.bar_size.y) / 2, col_black, 30.f, ImVec2(0, 0), ImDrawFlags_ShadowCutOutShapeBackground);
    draw_list->AddCircle(hue_bar_pos + ImVec2(animstate->lerp[0], SCALE(elements->colorpicker.bar_size.y) / 2), SCALE(elements->colorpicker.bar_size.y) / 2, col_white, 30, SCALE(2.f));

    // Render cursor/preview circle (clamp S/V within 0..1 range because floating points colors may lead HSV values to be out of range)
    draw_list->AddShadowCircle(sv_cursor_pos, SCALE(elements->colorpicker.bar_size.y) / 2, col_black, 30.f, ImVec2(0, 0));
    draw_list->AddCircleFilled(sv_cursor_pos, SCALE(elements->colorpicker.bar_size.y) / 2, user_col32_striped_of_alpha, 20);
    draw_list->AddCircle(sv_cursor_pos, SCALE(elements->colorpicker.bar_size.y) / 2, col_white, 20);

    // Render alpha bar
    if (alpha_bar)
    {
        float alpha = ImSaturate(col[3]);
        render_color_rect_with_alpha_checkboard(draw_list, alpha_bar_pos, alpha_bar_pos + SCALE(elements->colorpicker.bar_size), ImColor(0, 0, 0, 0), SCALE(elements->colorpicker.bar_size.y) / 2, ImVec2(0, 0), SCALE(elements->widgets.rounding), 0);
        draw->rect_filled_multi_color(draw_list, alpha_bar_pos, alpha_bar_pos + SCALE(elements->colorpicker.bar_size), user_col32_striped_of_alpha & ~IM_COL32_A_MASK, user_col32_striped_of_alpha, user_col32_striped_of_alpha, user_col32_striped_of_alpha & ~IM_COL32_A_MASK, SCALE(elements->widgets.rounding));
        float alpha_bar_circle_pos = IM_ROUND(alpha * SCALE(elements->colorpicker.bar_size.x));
        alpha_bar_circle_pos = ImClamp(alpha_bar_circle_pos, 5.f, SCALE(elements->colorpicker.bar_size.x) - 5.f);
        animstate->lerp[1] = ImLerp(animstate->lerp[1], alpha_bar_circle_pos, gui->fixed_speed(20.f));
        draw_list->AddShadowCircle(alpha_bar_pos + ImVec2(animstate->lerp[1], SCALE(elements->colorpicker.bar_size.y) / 2), SCALE(elements->colorpicker.bar_size.y) / 2, col_black, 30.f, ImVec2(0, 0), ImDrawFlags_ShadowCutOutShapeBackground);
        draw_list->AddCircle(alpha_bar_pos + ImVec2(animstate->lerp[1], SCALE(elements->colorpicker.bar_size.y) / 2), SCALE(elements->colorpicker.bar_size.y) / 2, col_white, 30, SCALE(2.f));
    }

    EndGroup();

    if (value_changed && memcmp(backup_initial_col, col, components * sizeof(float)) == 0)
        value_changed = false;
    if (value_changed && g.LastItemData.ID != 0) // In case of ID collision, the second EndGroup() won't catch g.ActiveId
        MarkItemEdited(g.LastItemData.ID);

    if (set_current_color_edit_id)
        g.ColorEditCurrentID = 0;
    PopID();

    return value_changed;
}

bool c_gui::color_edit(std::string_view label, float col[4], ImGuiColorEditFlags flags)
{
    struct color_state
    {
        bool active = false;
        bool hovered = false;
        float alpha = 0.f;
        float saturation = 4;
        ImU32 hue_col;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const float square_sz = GetFrameHeight();
    const char* label_display_end = FindRenderedTextEnd(label.data());
    float w_full = CalcItemWidth();
    g.NextItemData.ClearFlags();

    BeginGroup();
    PushID(label.data());
    const bool set_current_color_edit_id = (g.ColorEditCurrentID == 0);
    if (set_current_color_edit_id)
        g.ColorEditCurrentID = window->IDStack.back();

    // If we're not showing any slider there's no point in doing any HSV conversions
    const ImGuiColorEditFlags flags_untouched = flags;
    if (flags & ImGuiColorEditFlags_NoInputs)
        flags = (flags & (~ImGuiColorEditFlags_DisplayMask_)) | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoOptions;

    // Context menu: display and modify options (before defaults are applied)
    if (!(flags & ImGuiColorEditFlags_NoOptions))
        ColorEditOptionsPopup(col, flags);

    // Read stored options
    if (!(flags & ImGuiColorEditFlags_DisplayMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DisplayMask_);
    if (!(flags & ImGuiColorEditFlags_DataTypeMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DataTypeMask_);
    if (!(flags & ImGuiColorEditFlags_PickerMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_);
    if (!(flags & ImGuiColorEditFlags_InputMask_))
        flags |= (g.ColorEditOptions & ImGuiColorEditFlags_InputMask_);
    flags |= (g.ColorEditOptions & ~(ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_DisplayMask_)); // Check that only 1 is selected
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));   // Check that only 1 is selected

    const bool alpha = (flags & ImGuiColorEditFlags_NoAlpha) == 0;
    const bool hdr = (flags & ImGuiColorEditFlags_HDR) != 0;
    const int components = alpha ? 4 : 3;
    const float w_button = (flags & ImGuiColorEditFlags_NoSmallPreview) ? 0.0f : (square_sz + style.ItemInnerSpacing.x);
    const float w_inputs = ImMax(w_full - w_button, 1.0f);
    w_full = w_inputs + w_button;

    // Convert to the formats we need
    float f[4] = { col[0], col[1], col[2], alpha ? col[3] : 1.0f };
    if ((flags & ImGuiColorEditFlags_InputHSV) && (flags & ImGuiColorEditFlags_DisplayRGB))
        ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
    else if ((flags & ImGuiColorEditFlags_InputRGB) && (flags & ImGuiColorEditFlags_DisplayHSV))
    {
        // Hue is lost when converting from grayscale rgb (saturation=0). Restore it.
        ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);
        color_edit_restore_hs(col, &f[0], &f[1], &f[2]);
    }
    int i[4] = { IM_F32_TO_INT8_UNBOUND(f[0]), IM_F32_TO_INT8_UNBOUND(f[1]), IM_F32_TO_INT8_UNBOUND(f[2]), IM_F32_TO_INT8_UNBOUND(f[3]) };

    bool value_changed = false;
    bool value_changed_as_float = false;

    const ImVec2 pos = window->DC.CursorPos;
    const float inputs_offset_x = (style.ColorButtonPosition == ImGuiDir_Left) ? w_button : 0.0f;

    color_state* state = gui->anim_container(&state, GetID(label.data()));

    if ((flags & ImGuiColorEditFlags_DisplayHex) != 0 && (flags & ImGuiColorEditFlags_NoInputs) == 0)
    {
        // RGB Hexadecimal Input
        char buf[64];
        if (alpha)
            ImFormatString(buf, IM_ARRAYSIZE(buf), "#%02X%02X%02X%02X", ImClamp(i[0], 0, 255), ImClamp(i[1], 0, 255), ImClamp(i[2], 0, 255), ImClamp(i[3], 0, 255));
        else
            ImFormatString(buf, IM_ARRAYSIZE(buf), "#%02X%02X%02X", ImClamp(i[0], 0, 255), ImClamp(i[1], 0, 255), ImClamp(i[2], 0, 255));
        SetNextItemWidth(w_inputs);
        if (gui->color_text_field("##Text", buf, IM_ARRAYSIZE(buf), ImGuiInputTextFlags_CharsUppercase))
        {
            value_changed = true;
            char* p = buf;
            while (*p == '#' || ImCharIsBlankA(*p))
                p++;
            i[0] = i[1] = i[2] = 0;
            i[3] = 0xFF; // alpha default to 255 is not parsed by scanf (e.g. inputting #FFFFFF omitting alpha)
            int r;
            if (alpha)
                r = sscanf(p, "%02X%02X%02X%02X", (unsigned int*)&i[0], (unsigned int*)&i[1], (unsigned int*)&i[2], (unsigned int*)&i[3]); // Treat at unsigned (%X is unsigned)
            else
                r = sscanf(p, "%02X%02X%02X", (unsigned int*)&i[0], (unsigned int*)&i[1], (unsigned int*)&i[2]);
            IM_UNUSED(r); // Fixes C6031: Return value ignored: 'sscanf'.
        }
    }

    ImGuiWindow* picker_active_window = NULL;
    if (!(flags & ImGuiColorEditFlags_NoSmallPreview))
    {
        const ImVec4 col_v4(col[0], col[1], col[2], alpha ? col[3] : 1.0f);
        if (color_button(label.data(), col_v4, state->active))
            g.ColorPickerRef = col_v4;

        if (IsItemActive() && g.IO.MouseClicked[0] || (state->active && g.IO.MouseClicked[0] && !state->hovered))
            state->active = !state->active;

        state->alpha = ImClamp(state->alpha + (8.f * g.IO.DeltaTime * (state->active ? 1.f : -1.f)), 0.f, 1.f);

        if (!IsRectVisible(g.LastItemData.Rect.Min, g.LastItemData.Rect.Max + ImVec2(0, 2)))
        {
            state->active = false;
            state->alpha = 0.f;
        }

        SetNextWindowSize(SCALE(170, flags & ImGuiColorEditFlags_AlphaBar ? 272 : 254));
        SetNextWindowPos(g.LastItemData.Rect.GetTL());
        gui->push_style_var(ImGuiStyleVar_Alpha, state->alpha);
        gui->push_style_var(ImGuiStyleVar_WindowRounding, SCALE(elements->widgets.rounding));
        gui->push_style_var(ImGuiStyleVar_PopupBorderSize, SCALE(0.f));
        gui->push_style_color(ImGuiCol_PopupBg, draw->get_clr(clr->colorpicker.background));
        if (state->alpha >= 0.01f);
        {
            gui->begin((std::stringstream{} << "picker_window" << GetID(label.data())).str(), NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse, true, draw->get_clr(clr->accent, 0.3f), SCALE(30.f), 0);
            {
                if (state->active)
                    SetWindowFocus();
                state->hovered = IsWindowHovered();
                picker_active_window = g.CurrentWindow;

                ImGuiColorEditFlags picker_flags_to_forward = ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_ | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_AlphaBar;
                ImGuiColorEditFlags picker_flags = (flags_untouched & picker_flags_to_forward) | ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf;

                saturation_slider((std::stringstream{} << GetCurrentWindow()->ID << "saturatuion").str().c_str(), &state->saturation, 1.f, 7.f, 1.f, state->hue_col);
                
                gui->set_cursor_pos(SCALE(elements->colorpicker.padding.x, 36.f));
                gui->begin_group();

                value_changed |= color_picker("##picker", col, picker_flags, &g.ColorPickerRef.x, state->saturation, state->hue_col);

                gui->end_group();

            }
            gui->end();
        }
        gui->pop_style_var(3);
        gui->pop_style_color(1);
    }

    // Convert back
    if (value_changed && picker_active_window == NULL)
    {
        if (!value_changed_as_float)
            for (int n = 0; n < 4; n++)
                f[n] = i[n] / 255.0f;
        if ((flags & ImGuiColorEditFlags_DisplayHSV) && (flags & ImGuiColorEditFlags_InputRGB))
        {
            g.ColorEditSavedHue = f[0];
            g.ColorEditSavedSat = f[1];
            ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
            g.ColorEditSavedID = g.ColorEditCurrentID;
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(f[0], f[1], f[2], 0));
        }
        if ((flags & ImGuiColorEditFlags_DisplayRGB) && (flags & ImGuiColorEditFlags_InputHSV))
            ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);

        col[0] = f[0];
        col[1] = f[1];
        col[2] = f[2];
        if (alpha)
            col[3] = f[3];
    }

    if (set_current_color_edit_id)
        g.ColorEditCurrentID = 0;
    PopID();
    EndGroup();

    // When picker is being actively used, use its active id so IsItemActive() will function on ColorEdit4().
    if (picker_active_window && g.ActiveId != 0 && g.ActiveIdWindow == picker_active_window)
        g.LastItemData.ID = g.ActiveId;

    if (value_changed && g.LastItemData.ID != 0) // In case of ID collision, the second EndGroup() won't catch g.ActiveId
        MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}
