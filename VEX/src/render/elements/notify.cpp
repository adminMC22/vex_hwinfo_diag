#include <include/render/settings/functions.h>

void c_notify::add_notify(std::string text, bool warning)
{
    notifications.push_back({ notify_count++, text, warning });
}

void c_notify::setup_notify()
{
    const float screen_padding = 20.f;
    const float cover_screen = -100.f;
    const float speed = 4.f; // TO CHANGE THE DURATION USE notify_timer

    int cur_notify_value = 0;
    for (auto& notification : notifications)
    {
        if (notification.active_notify)
            notification.notify_timer += speed * ImGui::GetIO().DeltaTime;

        if (notification.notify_timer >= notify_time)
            notification.active_notify = false;

        notification.notify_alpha = ImClamp(notification.notify_alpha + (gui->fixed_speed(4.f) * (notification.active_notify ? 1.f : -1.f)), 0.f, 1.f);
        notification.notify_offset = ImLerp(notification.notify_offset, notification.active_notify ? screen_padding : cover_screen, gui->fixed_speed(12.f));

        if (notification.notify_alpha <= 0.f && !notification.active_notify)
            cur_notify_value--;

        render_notify(cur_notify_value, notification.notify_alpha, notification.notify_offset, notification.notify_timer, notification.text, notification.warning);
        cur_notify_value++;
    }
}

void c_notify::render_notify(int cur_notify_value, float notify_alpha, float notify_offset, float notify_duration, const std::string& text, bool warning)
{
    const float font_width = var->font.inter[3]->CalcTextSizeA(var->font.inter[3]->FontSize, FLT_MAX, -1, text.data()).x;
    SetNextWindowSize(ImVec2(SCALE(150) + font_width, SCALE(40)));
    SetNextWindowPos(ImVec2(notify_offset, SCALE(20) + cur_notify_value * (SCALE(40) + SCALE(20))));
    gui->push_style_var(ImGuiStyleVar_Alpha, notify_alpha);
    gui->begin((std::stringstream() << "notify" << cur_notify_value).str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip);
    {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* drawlist = ImGui::GetWindowDrawList();
        ImGuiStyle* style = &ImGui::GetStyle();

        {
            style->WindowPadding = ImVec2(0.f, 0.f);
            style->PopupBorderSize = 0.f;
            style->PopupRounding = SCALE(4.f);
        }

        const ImVec4 accent_color = warning ? ImColor(255, 189, 141) :  ImColor(139, 255, 84);

        {
            drawlist->AddRectFilled(pos, pos + size, draw->get_clr(clr->notify.background), SCALE(2.f));
            draw->render_text(drawlist, var->font.icons[8], pos + SCALE(15, 0), pos + ImVec2(SCALE(40), size.y), draw->get_clr(accent_color), warning ? "F" : "G", NULL, NULL, ImVec2(0.5f, 0.5f));
            draw->render_text(drawlist, var->font.inter[3], pos + SCALE(55, -2), pos + size - SCALE(0, 2), draw->get_clr(accent_color), warning ? "Warning" : "Success", NULL, NULL, ImVec2(0.f, 0.5f));
            draw->render_text(drawlist, var->font.inter[3], pos + SCALE(130, -2), pos + size - SCALE(0, 2), draw->get_clr(clr->notify.label), text.data(), NULL, NULL, ImVec2(0.f, 0.5f));
        }
    }
    gui->end();
    gui->pop_style_var();
}
