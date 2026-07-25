#include <include/render/settings/functions.h>

void c_gui::push_style_color(ImGuiCol idx, ImU32 col)
{
    PushStyleColor(idx, col);
}

void c_gui::pop_style_color(int count)
{
    PopStyleColor(count);
}

void c_gui::push_style_var(ImGuiStyleVar idx, float val)
{
    PushStyleVar(idx, val);
}

void c_gui::push_style_var(ImGuiStyleVar idx, const ImVec2& val)
{
    PushStyleVar(idx, val);
}

void c_gui::pop_style_var(int count)
{
    PopStyleVar(count);
}

void c_gui::push_font(ImFont* font)
{
    ImGui::PushFont(font);
}

void c_gui::pop_font()
{
    ImGui::PopFont();
}

void c_gui::set_cursor_pos(const ImVec2& local_pos)
{
    SetCursorPos(local_pos);
}

void c_gui::begin_group()
{
    BeginGroup();
}

void c_gui::end_group()
{
    EndGroup();
}

void c_gui::begin_content(std::string_view name, const ImVec2& size_arg, const ImVec2& padding, const ImVec2& spacing, bool scroll, bool move)
{
    gui->push_style_var(ImGuiStyleVar_WindowPadding, padding);
    gui->begin_def_child(name.data(), size_arg, ImGuiChildFlags_None, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoSavedSettings | (scroll ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar) | (move ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoMove));
    gui->push_style_var(ImGuiStyleVar_ItemSpacing, spacing);
}

void c_gui::end_content()
{
    gui->pop_style_var();
    gui->end_def_child();
    gui->pop_style_var();
}

void c_gui::sameline(bool content)
{
    if (var->gui.favorite_childs && content && GetContentRegionAvail().x > (GetWindowSize() - SCALE(var->window.section_size.x, var->window.child_select_size.y + var->window.topbar_size.y + GetStyle().ItemSpacing.y) - GetStyle().ItemSpacing).x / 2)
        return;

    SameLine(0.f, -1.f);
}

bool begin_def_child_ex(const char* name, ImGuiID id, const ImVec2& size_arg, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
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
    const ImVec2 size_avail = GetContentRegionAvail();
    const ImVec2 size_default((child_flags & ImGuiChildFlags_AutoResizeX) ? 0.0f : size_avail.x, (child_flags & ImGuiChildFlags_AutoResizeY) ? 0.0f : size_avail.y);
    const ImVec2 size = CalcItemSize(size_arg, size_default.x, size_default.y);
    SetNextWindowSize(size);

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

    // Begin into window
    const bool ret = gui->begin(temp_window_name, NULL, window_flags);

    // Restore style
    g.Style.ChildBorderSize = backup_border_size;
    if (child_flags & ImGuiChildFlags_FrameStyle)
    {
        PopStyleVar(3);
        PopStyleColor();
    }

    ImGuiWindow* child_window = g.CurrentWindow;
    child_window->ChildId = id;

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

bool c_gui::begin_def_child(std::string_view name, const ImVec2& size_arg, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    ImGuiID id = GetCurrentWindow()->GetID(name.data());
    return begin_def_child_ex(name.data(), id, size_arg, child_flags, window_flags);
}

void c_gui::end_def_child()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* child_window = g.CurrentWindow;

    IM_ASSERT(g.WithinEndChild == false);
    IM_ASSERT(child_window->Flags & ImGuiWindowFlags_ChildWindow);   // Mismatched BeginChild()/EndChild() calls

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
    g.WithinEndChild = false;
    g.LogLinePosY = -FLT_MAX; // To enforce a carriage return
}

void c_gui::set_next_window_pos(const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot)
{
    SetNextWindowPos(pos, cond, pivot);
}

void c_gui::set_next_window_size(const ImVec2& size, ImGuiCond cond)
{
    SetNextWindowSize(size, cond);
}

ImRect c_gui::widget_rect()
{
    ImGuiWindow* window = GetCurrentWindow();
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = window->DC.CursorPos;

    gui->push_font(var->font.inter[1]);
    const ImRect widget_rect = ImRect(pos, pos + ImVec2(width, GetFontSize() + SCALE(elements->widgets.spacing.y) - SCALE(3)));
    gui->pop_font();

    return widget_rect;
}