#pragma once
#include "imgui.h"

class c_colors
{
public:

    ImVec4 accent = ImColor(100, 180, 255); // lightblue

    struct
    {
        ImVec4 background = ImColor(0, 0, 0);       // black
        ImVec4 name = ImColor(255, 255, 255);       // white
        ImVec4 year = ImColor(100, 180, 255);           // lightblue
    } window;

    struct
    {
        ImVec4 label = ImColor(255, 255, 255);
        ImVec4 background = ImColor(10, 15, 30);    // dark blue
        ImVec4 stroke = ImColor(60, 120, 200);      // mid blue
        ImVec4 line = ImColor(40, 80, 160);         // darker blue
        ImVec4 favorite_inactive = ImColor(40, 80, 130);
        ImVec4 favorite_active = ImColor(100, 200, 255); // bright blue
    } child;

    struct
    {
        ImVec4 label = ImColor(235, 238, 255);
        ImVec4 label_inactive = ImColor(60, 100, 150);
        ImVec4 background = ImColor(15, 20, 40);    // dark navy
    } widgets;

    struct
    {
        ImVec4 icon = ImColor(100, 180, 255);       // lightblue
    } section;

    struct
    {
        ImVec4 label = ImColor(100, 200, 255);      // bright blue
    } favorite_button;

    struct
    {
        ImVec4 background = ImColor(10, 15, 30);
    } dropdown;

    struct
    {
        ImVec4 background = ImColor(10, 15, 30);
    } colorpicker;

    struct
    {
        ImVec4 background = ImColor(10, 15, 30);
        ImVec4 button_bg = ImColor(30, 60, 100);
    } keybind;

    struct
    {
        ImVec4 background = ImColor(10, 15, 30);
        ImVec4 label = ImColor(235, 238, 255);
    } notify;

    struct
    {
        ImVec4 background = ImColor(10, 15, 30);
        ImVec4 text = ImColor(255, 255, 255);
        ImVec4 line = ImColor(60, 120, 200);
    } watermark;

};

inline c_colors* clr = new c_colors();
