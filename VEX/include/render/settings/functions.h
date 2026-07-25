#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "../data/images.h"
#include "colors.h"
#include "variables.h"
#include "elements.h"
#include <vector>
#include <sstream>
#include <string>
#include <format>
#include <chrono>
#include <deque>
#include <algorithm>

using namespace ImGui;

#define SCALE(...) scale_impl(__VA_ARGS__, var->dpi)

inline ImVec2 scale_impl(const ImVec2& vec, float dpi) {
    return ImVec2(roundf(vec.x * dpi), roundf(vec.y * dpi));
}

inline ImVec2 scale_impl(float x, float y, float dpi) {
    return ImVec2(roundf(x * dpi), roundf(y * dpi));
}

inline float scale_impl(float var, float dpi) {
    return roundf(var * dpi);
}

struct keybind_state
{
    int mode = 0;
    int key = 0;
    bool value = false;
};

class c_gui
{
public:


    template <typename T>
    T* anim_container(T** state_ptr, ImGuiID id)
    {
        T* state = static_cast<T*>(GetStateStorage()->GetVoidPtr(id));
        if (!state)
            GetStateStorage()->SetVoidPtr(id, state = new T());
        else if (state)
            GetStateStorage()->GetVoidPtr(id);

        *state_ptr = state;
        return state;
    }

    float fixed_speed(float speed) { return speed / ImGui::GetIO().Framerate; };

    bool begin(std::string_view name, bool* p_open = nullptr, ImGuiWindowFlags flags = ImGuiWindowFlags_None, bool shadow = false, ImU32 shadow_col = 0, float shadow_size = 0.f, bool blur = true);

    void end();

    void push_style_color(ImGuiCol idx, ImU32 col);

    void pop_style_color(int count = 1);

    void push_style_var(ImGuiStyleVar idx, float val);

    void push_style_var(ImGuiStyleVar idx, const ImVec2& val);

    void pop_style_var(int count = 1);

    void push_font(ImFont* font);

    void pop_font();

    void set_cursor_pos(const ImVec2& local_pos);

    void begin_group();

    void end_group();

    void begin_content(std::string_view name, const ImVec2& size_arg = ImVec2(0, 0), const ImVec2& padding = ImVec2(0, 0), const ImVec2& spacing = ImVec2(0, 0), bool scroll = false, bool move = true);

    void end_content();

    void sameline(bool content = true);

    bool begin_def_child(std::string_view name, const ImVec2& size_arg = ImVec2(0, 0), ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);

    void end_def_child();

    void set_next_window_pos(const ImVec2& pos, ImGuiCond cond = 0, const ImVec2& pivot = ImVec2(0, 0));

    void set_next_window_size(const ImVec2& size, ImGuiCond cond = 0);

    void begin_child(std::string_view name, std::string_view icon, const ImVec2& size = ImVec2(0, 0));

    void end_child();

    bool favorite_button(std::string_view label, std::string_view icon, int active);

    bool section(std::string_view icon, int section_id, int& section_variable);

    ImRect widget_rect();

    bool checkbox(std::string_view label, bool* callback);

    bool checkbox_with_color(std::string_view label, bool* callback, float col[4], bool alpha = false);
    
    // Version with multiple color pickers
    bool checkbox_with_color(std::string_view label, bool* callback, float colors[][4], int color_count, bool alpha = false);
    
    // Version with two color pointers (more practical for var->config)
    bool checkbox_with_color(std::string_view label, bool* callback, float* color1, float* color2 = nullptr, bool alpha = false);

    bool checkbox_with_key(std::string_view label, bool* callback, keybind_state* sstate);

    bool slider_float(std::string_view label, float* v, float v_min, float v_max, const char* format = "%.1f", ImGuiSliderFlags flags = 0);

    bool slider_int(std::string_view label, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0, bool dpi = false);

    bool hue_slider(std::string_view label, float col[4]);

    bool dropdown(std::string_view label, int* current_item, const char* const items[], int items_count);

    void multi_dropdown(std::string_view label, std::vector<int>& variable, const char* labels[], int count);

    bool text_field(std::string_view label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = 0, void* user_data = NULL);
    
    bool color_text_field(std::string_view label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = 0, void* user_data = NULL);

    bool button(std::string_view label);

    bool color_edit(std::string_view label, float col[4], ImGuiColorEditFlags flags = 0);

    bool keybind(std::string_view label, keybind_state* state);

    void render();

};

inline c_gui* gui = new c_gui();

enum fade_direction : int
{
    vertically,
    horizontally,
    diagonally,
    diagonally_reversed,
};

class c_draw
{
public:
    ImU32 get_clr(const ImVec4& col, float alpha = 1.f);

    void rect_filled_multi_color(ImDrawList* draw, const ImVec2& p_min, const ImVec2& p_max, ImU32 col_upr_left, ImU32 col_upr_right, ImU32 col_bot_right, ImU32 col_bot_left, float rounding = 0.f, ImDrawFlags flags = 0);

    void fade_rect_filled(ImDrawList* draw, const ImVec2& pos_min, const ImVec2& pos_max, ImU32 col_one, ImU32 col_two, fade_direction direction, float rounding = 0.f, ImDrawFlags flags = 0);

    void render_text(ImDrawList* draw_list, ImFont* font, const ImVec2& pos_min, const ImVec2& pos_max, ImU32 color, const char* text, const char* text_display_end = NULL, const ImVec2* text_size_if_known = NULL, const ImVec2& align = ImVec2(0.f, 0.f), const ImRect* clip_rect = NULL);

    void radial_gradient(ImDrawList* draw_list, const ImVec2& center, float radius, ImU32 col_in, ImU32 col_out);

    void set_linear_color_alpha(ImDrawList* draw_list, int vert_start_idx, int vert_end_idx, ImVec2 gradient_p0, ImVec2 gradient_p1, ImU32 col0, ImU32 col1);

    void watermark(std::string_view name);
};

inline c_draw* draw = new c_draw();

struct notify_state
{
    int notify_id;
    std::string text;  // Change: std::string instead of std::string_view
    bool warning;
    float notify_alpha = 0.f;
    float notify_offset = 0.f;
    bool active_notify = true;
    float notify_timer = 0.f;
};

class c_notify
{
public:
    void setup_notify();

    void add_notify(std::string text, bool warning);

private:
    void render_notify(int cur_notify_value, float notify_alpha, float notify_offset, float notify_percentage, const std::string& text, bool warnin);

    float notify_time = 15.f;

    int notify_count = 0;

    std::vector<notify_state> notifications;

};

inline c_notify* notify = new c_notify();
