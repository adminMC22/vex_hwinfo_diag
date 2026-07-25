#pragma once
#include "imgui.h"

class c_colors
{
public:

    ImVec4 accent = ImColor(151, 26, 40); // bordô (cor principal da logo)

    struct
    {
        ImVec4 background = ImColor(0, 0, 0);       // preto
        ImVec4 name = ImColor(255, 255, 255);       // branco para contraste
        ImVec4 year = ImColor(255, 255, 255);           // vermelho escuro
    } window;

    struct
    {
        ImVec4 label = ImColor(255, 255, 255);
        ImVec4 background = ImColor(20, 20, 20);    // quase preto
        ImVec4 stroke = ImColor(50, 0, 0);          // vermelho bem escuro
        ImVec4 line = ImColor(75, 0, 0);            // bordô escuro
        ImVec4 favorite_inactive = ImColor(80, 20, 30);
        ImVec4 favorite_active = ImColor(200, 40, 50); // vermelho vivo para destaque
    } child;

    struct
    {
        ImVec4 label = ImColor(235, 238, 255);
        ImVec4 label_inactive = ImColor(128, 0, 0);
        ImVec4 background = ImColor(30, 0, 0);      // bordô muito escuro
    } widgets;

    struct
    {
        ImVec4 icon = ImColor(151, 26, 40);
    } section;

    struct
    {
        ImVec4 label = ImColor(200, 40, 50);
    } favorite_button;

    struct
    {
        ImVec4 background = ImColor(15, 0, 0);
    } dropdown;

    struct
    {
        ImVec4 background = ImColor(15, 0, 0);
    } colorpicker;

    struct
    {
        ImVec4 background = ImColor(15, 0, 0);
        ImVec4 button_bg = ImColor(50, 0, 0);
    } keybind;

    struct
    {
        ImVec4 background = ImColor(15, 0, 0);
        ImVec4 label = ImColor(235, 238, 255);
    } notify;

    struct
    {
        ImVec4 background = ImColor(15, 0, 0);
        ImVec4 text = ImColor(255, 255, 255);
        ImVec4 line = ImColor(128, 0, 0);
    } watermark;

};

inline c_colors* clr = new c_colors();