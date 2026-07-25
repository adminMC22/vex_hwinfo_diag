#include <include/render/settings/functions.h>

bool selectable_ex(const char* label, bool active)
{
    struct selectable_state
    {
        float alpha = 0.f;
        ImVec4 label_clr = clr->widgets.label_inactive;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + ImVec2(width, SCALE(elements->selectable.height)));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered = IsItemHovered();
    bool pressed = hovered && g.IO.MouseClicked[0];
    if (pressed)
        MarkItemEdited(id);

    selectable_state* state = gui->anim_container(&state, id);
    state->label_clr = ImLerp(state->label_clr, active ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(10.f));
    state->alpha = ImClamp(state->alpha + (gui->fixed_speed(8.f) * (active ? 1.f : -1.f)), 0.f, 1.f);

    window->DrawList->AddCircleFilled(ImVec2(rect.Max.x - SCALE(3.f), rect.GetCenter().y), SCALE(3.f), draw->get_clr(clr->accent, state->alpha));
    draw->render_text(window->DrawList, var->font.inter[1], rect.Min - SCALE(elements->widgets.label_padding), rect.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label, NULL, NULL, ImVec2(0.f, 0.5f));

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

bool selectable(const char* label, bool* p_selected)
{
    if (selectable_ex(label, *p_selected))
    {
        *p_selected = !*p_selected;
        return true;
    }
    return false;
}

static float calc_combo_size(int items_count, float item_size)
{
    ImGuiContext& g = *GImGui;
    if (items_count <= 0)
        return FLT_MAX;
    return item_size * items_count + g.Style.ItemSpacing.y * (items_count - 1) + (g.Style.WindowPadding.y * 2);
}

static const char* items_array_getter(void* data, int idx)
{
    const char* const* items = (const char* const*)data;
    return items[idx];
}

bool begin_combo(const char* label, const char* preview_value, int val, ImGuiComboFlags flags, bool multi)
{
    struct combo_state
    {
        ImVec4 label_clr = clr->widgets.label_inactive;
        float alpha = 0.f;
        bool combo_opened = false;
        bool hovered = false;
    };

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();

    ImGuiNextWindowDataFlags backup_next_window_data_flags = g.NextWindowData.Flags;
    g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume those values
    if (window->SkipItems)
        return false;

    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)); // Can't use both flags together
    if (flags & ImGuiComboFlags_WidthFitPreview)
        IM_ASSERT((flags & (ImGuiComboFlags_NoPreview | ImGuiComboFlags_CustomPreview)) == 0);

    val = val > 5 ? 5 : val;

    const ImRect total = gui->widget_rect();
    const ImRect rect(ImVec2(total.Min.x, total.GetCenter().y - SCALE(elements->dropdown.height) / 2), ImVec2(total.Max.x, total.GetCenter().y + SCALE(elements->dropdown.height) / 2));
    const ImRect clickable(ImVec2(rect.Max.x - round(rect.GetWidth() * elements->dropdown.multiplier), rect.Min.y), rect.Max);
    ItemSize(total, style.FramePadding.y);
    if (!ItemAdd(total, id))
        return false;

    // Open on click
    const bool rect_hovered = ItemHoverable(rect, id, g.LastItemData.InFlags);
    bool hovered, held;
    bool pressed = ButtonBehavior(clickable, id, &hovered, &held);

    combo_state* state = gui->anim_container(&state, id);

    state->alpha = ImClamp(state->alpha + (gui->fixed_speed(8.f) * (state->combo_opened ? 1.f : -1.f)), 0.f, 1.f);
    state->label_clr = ImLerp(state->label_clr, state->combo_opened ? clr->widgets.label : clr->widgets.label_inactive, gui->fixed_speed(18.f));

    if (hovered && g.IO.MouseClicked[0] || state->combo_opened && g.IO.MouseClicked[0] && !state->hovered)
        state->combo_opened = !state->combo_opened;

    draw->render_text(window->DrawList, var->font.inter[1], total.Min - SCALE(elements->widgets.label_padding), total.Max - SCALE(elements->widgets.label_padding), draw->get_clr(state->label_clr), label, NULL, NULL, ImVec2(0.f, 0.5f));

    window->DrawList->AddRectFilled(clickable.Min, clickable.Max, draw->get_clr(clr->widgets.background), SCALE(elements->widgets.rounding));
    draw->render_text(window->DrawList, var->font.inter[2], ImVec2(clickable.Min.x + SCALE(6), total.Min.y - SCALE(1)), ImVec2(clickable.Max.x - SCALE(20), total.Max.y - SCALE(1)), draw->get_clr(clr->widgets.label), preview_value, 0, 0, ImVec2(0.f, 0.5f));
    draw->render_text(window->DrawList, var->font.icons[4], clickable.Min, clickable.Max, draw->get_clr(clr->widgets.label), "D", 0, 0, ImVec2(0.95f, 0.5f));

    if (!IsRectVisible(rect.Min, rect.Max + ImVec2(0, 2)))
    {
        state->combo_opened = false;
        state->alpha = 0.f;
    }

    if (!state->combo_opened && state->alpha <= 0.1f)
        return false;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_Tooltip | (!multi || (multi && val < 5) ? ImGuiWindowFlags_NoScrollbar : 0);

    gui->push_style_var(ImGuiStyleVar_Alpha, state->alpha);
    gui->push_style_color(ImGuiCol_PopupBg, draw->get_clr(clr->dropdown.background));
    gui->push_style_var(ImGuiStyleVar_PopupBorderSize, 0.f);
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, SCALE(elements->selectable.spacing));
    SetNextWindowPos(clickable.GetTL());
    SetNextWindowSize(ImVec2(clickable.GetWidth(), calc_combo_size(val, SCALE(elements->selectable.height))));
    gui->begin((std::stringstream{} << window->Name << label << id).str(), NULL, window_flags, true, draw->get_clr(clr->accent, 0.3f), SCALE(30.f), 0);
    {

        SetWindowFocus();
        state->hovered = IsWindowHovered();

        if (!state->combo_opened)
            SetScrollY(0.f);

        if (!multi)
            if (state->hovered && g.IO.MouseClicked[0])
                state->combo_opened = false;

    }
    return true;
}

void end_combo()
{
    gui->pop_style_color();
    gui->pop_style_var(3);
    gui->end();
}

bool combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items)
{
    ImGuiContext& g = *GImGui;

    // Call the getter to obtain the preview string which is a parameter to BeginCombo()
    const char* preview_value = NULL;
    if (*current_item >= 0 && *current_item < items_count)
        preview_value = getter(user_data, *current_item);

    // The old Combo() API exposed "popup_max_height_in_items". The new more general BeginCombo() API doesn't have/need it, but we emulate it here.
    if (popup_max_height_in_items != -1 && !(g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint))
        SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, calc_combo_size(popup_max_height_in_items, 31.f)));

    if (!begin_combo(label, preview_value, items_count, ImGuiComboFlags_None, false))
        return false;

    // Display items
    // FIXME-OPT: Use clipper (but we need to disable it on the appearing frame to make sure our call to SetItemDefaultFocus() is processed)
    bool value_changed = false;
    for (int i = 0; i < items_count; i++)
    {
        const char* item_text = getter(user_data, i);
        if (item_text == NULL)
            item_text = "*Unknown item*";

        PushID(i);
        const bool item_selected = (i == *current_item);
        if (selectable_ex(item_text, item_selected) && *current_item != i)
        {
            value_changed = true;
            *current_item = i;
        }
        if (item_selected)
            SetItemDefaultFocus();
        PopID();
    }

    end_combo();

    if (value_changed)
        MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}

bool c_gui::dropdown(std::string_view label, int* current_item, const char* const items[], int items_count)
{
    bool& favorite_state = var->gui.child_states[GetCurrentWindow()->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return false;

    const bool value_changed = combo(label.data(), current_item, items_array_getter, (void*)items, items_count, -1);
    return value_changed;
}

void c_gui::multi_dropdown(std::string_view label, std::vector<int>& variable, const char* labels[], int count)
{
    bool& favorite_state = var->gui.child_states[GetCurrentWindow()->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return;

    std::string preview = "Select";

    for (auto i = 0, j = 0; i < count; i++)
    {
        if (variable[i])
        {
            if (j)
                preview += (", ") + (std::string)labels[i];
            else
                preview = labels[i];

            j++;
        }
    }

    if (begin_combo(label.data(), preview.c_str(), count, 0, true))
    {
        for (auto i = 0; i < count; i++)
        {
            selectable(labels[i], (bool*)&variable[i]);
        }
        end_combo();
    }

    preview = ("Select");
}
