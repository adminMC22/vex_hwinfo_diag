#include <include/render/settings/functions.h>

bool c_gui::favorite_button(std::string_view label, std::string_view icon, int active)
{
    struct favorite_button_state
    {
        ImVec4 text_col = clr->favorite_button.label;
        float shadow_offset = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label.data());

    ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + SCALE(elements->favorite_button.size));
    ItemSize(rect, style.FramePadding.y);
    if (!ItemAdd(rect, id))
        return false;

    const bool selected = active == var->gui.favorite_childs;

    bool hovered, held;
    bool pressed = ButtonBehavior(rect, id, &hovered, &held, 0);

    favorite_button_state* state = gui->anim_container(&state, id);
    state->text_col = ImLerp(state->text_col, selected ? clr->widgets.label : clr->favorite_button.label, gui->fixed_speed(8.f));
    state->shadow_offset = ImLerp(state->shadow_offset, selected ? elements->section.shadow_offset : 0.f, gui->fixed_speed(12.f));

    // Render
    if (state->shadow_offset > 2.f)
        window->DrawList->AddShadowRect(rect.Min, rect.Max, draw->get_clr(clr->accent, 0.2f), SCALE(state->shadow_offset), ImVec2(0, 0), ImDrawFlags_ShadowCutOutShapeBackground, SCALE(elements->section.rounding));

    draw->render_text(window->DrawList, var->font.icons[2], rect.Min, rect.Max, draw->get_clr(state->text_col), icon.data(), NULL, NULL, ImVec2(0.16f, 0.5f));
    draw->render_text(window->DrawList, var->font.inter[0], rect.Min - SCALE(0, 1), rect.Max - SCALE(0, 1), draw->get_clr(state->text_col), label.data(), NULL, NULL, ImVec2(0.73f, 0.5f));

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

bool begin_child_ex(const char* name, const char* icon, ImGuiID id, const ImVec2& size_arg, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    struct child_state
    {
        float state = 0.f;
        float slow = 0.f;
        ImVec4 favorite_col = clr->child.favorite_inactive;
    };

    child_state* state = gui->anim_container(&state, id);
    bool& favorite_state = var->gui.child_states[id];

    ImGuiContext& g = *GImGui;
    ImGuiWindow* parent_window = g.CurrentWindow;
    IM_ASSERT(id != 0);

    // Sanity check as it is likely that some user will accidentally pass ImGuiWindowFlags into the ImGuiChildFlags argument.
    const ImGuiChildFlags ImGuiChildFlags_SupportedMask_ = ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_FrameStyle;
    IM_UNUSED(ImGuiChildFlags_SupportedMask_);
    IM_ASSERT((child_flags & ~ImGuiChildFlags_SupportedMask_) == 0 && "Illegal ImGuiChildFlags value. Did you pass ImGuiWindowFlags values instead of ImGuiChildFlags?");
    IM_ASSERT((window_flags & ImGuiWindowFlags_AlwaysAutoResize) == 0 && "Cannot specify ImGuiWindowFlags_AlwaysAutoResize for BeginChild(). Use ImGuiChildFlags_AlwaysAutoResize!");
    if (child_flags & ImGuiChildFlags_AlwaysAutoResize)
    {
        IM_ASSERT((child_flags & (ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY)) == 0 && "Cannot use ImGuiChildFlags_ResizeX or ImGuiChildFlags_ResizeY with ImGuiChildFlags_AlwaysAutoResize!");
        IM_ASSERT((child_flags & (ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY)) != 0 && "Must use ImGuiChildFlags_AutoResizeX or ImGuiChildFlags_AutoResizeY with ImGuiChildFlags_AlwaysAutoResize!");
    }
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    if (window_flags & ImGuiWindowFlags_AlwaysUseWindowPadding)
        child_flags |= ImGuiChildFlags_AlwaysUseWindowPadding;
#endif
    if (child_flags & ImGuiChildFlags_AutoResizeX)
        child_flags &= ~ImGuiChildFlags_ResizeX;
    if (child_flags & ImGuiChildFlags_AutoResizeY)
        child_flags &= ~ImGuiChildFlags_ResizeY;

    // Set window flags
    window_flags |= ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoTitleBar;
    window_flags |= (parent_window->Flags & ImGuiWindowFlags_NoMove); // Inherit the NoMove flag
    if (child_flags & (ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize))
        window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if ((child_flags & (ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY)) == 0)
        window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    // Special framed style
    if (child_flags & ImGuiChildFlags_FrameStyle)
    {
        PushStyleColor(ImGuiCol_ChildBg, g.Style.Colors[ImGuiCol_FrameBg]);
        PushStyleVar(ImGuiStyleVar_ChildRounding, g.Style.FrameRounding);
        PushStyleVar(ImGuiStyleVar_ChildBorderSize, g.Style.FrameBorderSize);
        PushStyleVar(ImGuiStyleVar_WindowPadding, g.Style.FramePadding);
        child_flags |= ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding;
        window_flags |= ImGuiWindowFlags_NoMove;
    }

    // Forward child flags
    g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasChildFlags;
    g.NextWindowData.ChildFlags = child_flags;

    // Forward size
    // Important: Begin() has special processing to switch condition to ImGuiCond_FirstUseEver for a given axis when ImGuiChildFlags_ResizeXXX is set.
    // (the alternative would to store conditional flags per axis, which is possible but more code)


    ImVec2 size = size_arg;
    if (size.x <= 0)
        size.x = (GetWindowWidth() - SCALE(elements->content.spacing.x) - SCALE(elements->content.padding.x * 2.f)) / 2.f;
    if (size.y <= 0)
        size.y = state->state;

    gui->set_next_window_size(size - SCALE(0, elements->child.titlebar_height));
    gui->set_next_window_pos(parent_window->DC.CursorPos + SCALE(0, elements->child.titlebar_height));

    // Build up name. If you need to append to a same child from multiple location in the ID stack, use BeginChild(ImGuiID id) with a stable value.
    // FIXME: 2023/11/14: commented out shorted version. We had an issue with multiple ### in child window path names, which the trailing hash helped workaround.
    // e.g. "ParentName###ParentIdentifier/ChildName###ChildIdentifier" would get hashed incorrectly by ImHashStr(), trailing _%08X somehow fixes it.
    const char* temp_window_name;
    /*if (name && parent_window->IDStack.back() == parent_window->ID)
        ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%s", parent_window->Name, name); // May omit ID if in root of ID stack
    else*/
    if (name)
        ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%s_%08X", parent_window->Name, name, id);
    else
        ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%08X", parent_window->Name, id);

    // Set style
    const float backup_border_size = g.Style.ChildBorderSize;
    if ((child_flags & ImGuiChildFlags_Border) == 0)
        g.Style.ChildBorderSize = 0.0f;

    state->favorite_col = ImLerp(state->favorite_col, favorite_state ? clr->child.favorite_active : clr->child.favorite_inactive, gui->fixed_speed(12.f));

    if (var->gui.content_alpha <= 0.1f)
    {
        state->state = 0.f;
        state->slow = 0.f;
        state->favorite_col = clr->child.favorite_inactive;
    }

    parent_window->DrawList->AddRect(parent_window->DC.CursorPos, parent_window->DC.CursorPos + size, draw->get_clr(clr->child.stroke), SCALE(elements->child.rounding), 0, SCALE(1.f));
    parent_window->DrawList->AddRectFilled(parent_window->DC.CursorPos + SCALE(0, 1), parent_window->DC.CursorPos + size - SCALE(0, 1), draw->get_clr(clr->child.background), SCALE(elements->child.rounding));
    parent_window->DrawList->AddLine(parent_window->DC.CursorPos + SCALE(0, elements->child.titlebar_height - 1), parent_window->DC.CursorPos + ImVec2(size.x, SCALE(elements->child.titlebar_height - 1)), draw->get_clr(clr->child.line), SCALE(1.f));

    draw->render_text(parent_window->DrawList, var->font.icons[3], parent_window->DC.CursorPos, parent_window->DC.CursorPos + ImVec2(size.x, SCALE(elements->child.titlebar_height)), draw->get_clr(clr->accent), icon, NULL, NULL, ImVec2(0.035f, 0.5f));
    draw->render_text(parent_window->DrawList, var->font.inter[0], parent_window->DC.CursorPos - SCALE(-30, 1), parent_window->DC.CursorPos + ImVec2(size.x, SCALE(elements->child.titlebar_height - 1)), draw->get_clr(clr->child.label), name, NULL, NULL, ImVec2(0.f, 0.5f));

    ImVec2 stored_pos = GetCursorScreenPos();

    SetCursorScreenPos(parent_window->DC.CursorPos + ImVec2(size.x - SCALE(elements->child.favorite_size.x) - SCALE(10), SCALE(9)));
    if (InvisibleButton((std::stringstream{} << name << "favorite_button").str().c_str(), SCALE(elements->child.favorite_size)) && !var->gui.favorite_childs)
    {
        favorite_state = !favorite_state;
    }

    SetCursorScreenPos(stored_pos);
    draw->render_text(parent_window->DrawList, var->font.icons[2], parent_window->DC.CursorPos, parent_window->DC.CursorPos + ImVec2(size.x, SCALE(elements->child.titlebar_height)), draw->get_clr(state->favorite_col), "A", NULL, NULL, ImVec2(0.963f, 0.5f));

    // Begin into window
    const bool ret = gui->begin(temp_window_name, NULL, window_flags, 0, 0, 0, 0);

    // Restore style
    g.Style.ChildBorderSize = backup_border_size;
    if (child_flags & ImGuiChildFlags_FrameStyle)
    {
        PopStyleVar(3);
        PopStyleColor();
    }

    ImGuiWindow* child_window = g.CurrentWindow;
    child_window->ChildId = id;
    state->slow = ImLerp(state->slow, child_window->ContentSize.y + SCALE(elements->child.titlebar_height) + SCALE(elements->widgets.padding.y) * 2, gui->fixed_speed(12.f));
    state->state = state->slow;
    // Set the cursor to handle case where the user called SetNextWindowPos()+BeginChild() manually.
    // While this is not really documented/defined, it seems that the expected thing to do.
    if (child_window->BeginCount == 1)
        parent_window->DC.CursorPos = child_window->Pos;

    // Process navigation-in immediately so NavInit can run on first frame
    // Can enter a child if (A) it has navigable items or (B) it can be scrolled.
    const ImGuiID temp_id_for_activation = ImHashStr("##Child", 0, id);
    if (g.ActiveId == temp_id_for_activation)
        ClearActiveID();
    if (g.NavActivateId == id && !(window_flags & ImGuiWindowFlags_NavFlattened) && (child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY))
    {
        FocusWindow(child_window);
        NavInitWindow(child_window, false);
        SetActiveID(temp_id_for_activation, child_window); // Steal ActiveId with another arbitrary id so that key-press won't activate child item
        g.ActiveIdSource = g.NavInputSource;
    }
    return ret;
}

void c_gui::begin_child(std::string_view name, std::string_view icon, const ImVec2& size)
{
    ImGuiID id = GetCurrentWindow()->GetID(name.data());
    bool& favorite_state = var->gui.child_states[id];

    if (!favorite_state && var->gui.favorite_childs) return;

    gui->push_style_var(ImGuiStyleVar_WindowPadding, SCALE(elements->widgets.padding));
    gui->push_style_color(ImGuiCol_ChildBg, draw->get_clr(ImVec4(0.f, 0.f, 0.f, 0.f)));
    begin_child_ex(name.data(), icon.data(), id, size, ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, SCALE(0, 0));
}

void c_gui::end_child()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* child_window = g.CurrentWindow;

    bool& favorite_state = var->gui.child_states[child_window->ChildId];

    if (!favorite_state && var->gui.favorite_childs) return;

    IM_ASSERT(g.WithinEndChild == false);
    IM_ASSERT(child_window->Flags & ImGuiWindowFlags_ChildWindow);   // Mismatched BeginChild()/EndChild() calls

    gui->pop_style_var();

    g.WithinEndChild = true;
    ImVec2 child_size = child_window->Size;
    gui->end();
    if (child_window->BeginCount == 1)
    {
        ImGuiWindow* parent_window = g.CurrentWindow;
        ImRect bb(parent_window->DC.CursorPos, parent_window->DC.CursorPos + child_size);
        ItemSize(child_size);
        if ((child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY) && !(child_window->Flags & ImGuiWindowFlags_NavFlattened))
        {
            ItemAdd(bb, child_window->ChildId);
            RenderNavHighlight(bb, child_window->ChildId);

            // When browsing a window that has no activable items (scroll only) we keep a highlight on the child (pass g.NavId to trick into always displaying)
            if (child_window->DC.NavLayersActiveMask == 0 && child_window == g.NavWindow)
                RenderNavHighlight(ImRect(bb.Min - ImVec2(2, 2), bb.Max + ImVec2(2, 2)), g.NavId, ImGuiNavHighlightFlags_Compact);
        }
        else
        {
            // Not navigable into
            ItemAdd(bb, 0);

            // But when flattened we directly reach items, adjust active layer mask accordingly
            if (child_window->Flags & ImGuiWindowFlags_NavFlattened)
                parent_window->DC.NavLayersActiveMaskNext |= child_window->DC.NavLayersActiveMaskNext;
        }
        if (g.HoveredWindow == child_window)
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;
    }
    gui->pop_style_var();
    gui->pop_style_color();
    g.WithinEndChild = false;
    g.LogLinePosY = -FLT_MAX; // To enforce a carriage return
}
