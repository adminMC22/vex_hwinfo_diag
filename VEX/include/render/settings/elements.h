#pragma once
#include <string>
#include "imgui.h"

class c_elements
{
public:

	struct
	{
		ImVec2 padding = ImVec2(15, 15);
		ImVec2 spacing = ImVec2(15, 15);
	} content;

	struct
	{
		float titlebar_height = 30.f;
		float rounding = 4.f;
		ImVec2 favorite_size = ImVec2(12, 12);
	} child;

	struct
	{
		ImVec2 padding = ImVec2(10, 10);
		ImVec2 spacing = ImVec2(20, 20);
		float rounding = 2.f;
		ImVec2 label_padding = ImVec2(0, 1);
	} widgets;

	struct
	{
		ImVec2 size = ImVec2(50, 50);
		ImVec2 padding = ImVec2(15, 15);
		ImVec2 spacing = padding;
		float rounding = 4.f;
		float shadow_offset = 23.f;
	} section;

	struct
	{
		ImVec2 size = ImVec2(112, 50);
		ImVec2 padding = ImVec2(15, 15);
		ImVec2 spacing = padding;
		float rounding = 4.f;
		float shadow_offset = 23.f;
	} favorite_button;

	struct
	{
		float height = 15.f;
	} checkbox;

	struct
	{
		float height = 10.f;
		float multiplier = 0.42f;
		float grab_width = 5.f;
	} slider;

	struct
	{
		float height = 20.f;
		float multiplier = 0.42f;
	} dropdown;

	struct
	{
		float height = 20.f;
		ImVec2 spacing = ImVec2(10, 10);
	} selectable;

	struct
	{
		float height = 20.f;
		float multiplier = 0.42f;
		float color_height = 30.f;
	} textfield;

	struct
	{
		float height = 25.f;
	} button;

	struct
	{
		float size = 11.f;
		float rounding = 2.f;
		ImVec2 padding = ImVec2(10, 10);
		ImVec2 picker_size = ImVec2(150, 150);
		ImVec2 bar_size = ImVec2(picker_size.x, 8);
	} colorpicker;

};

inline c_elements* elements = new c_elements();