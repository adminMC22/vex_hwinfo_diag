#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "imgui.h"
#include <map>

struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct ID3D11ShaderResourceView;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

class c_variables
{
public:
	struct
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
		ImVec2 size = ImVec2(740, 530);
		ImVec2 padding = ImVec2(0, 0);
		ImVec2 spacing = ImVec2(10, 10);
		float shadow_size = 0.f;
		float border_size = 0.f;
		float rounding = 4.f;
		ImVec2 logo_size = ImVec2(80, 80);
		ImVec2 section_size = ImVec2(80, 340);
		ImVec2 child_select_size = ImVec2(270, 80);
		ImVec2 gui_dpi_size = ImVec2(255, 40);
		ImVec2 hue_slider_size = ImVec2(370, 30);
		ImVec2 topbar_size = ImVec2(size.x - section_size.x - spacing.x, 30);
		float scrollbar_size = 4.f;
	} window;

	struct
	{
		ImFont* inter[4];
		ImFont* icons[10];
	} font;

	struct
	{
		bool enabled = true;
		float content_alpha = 0.f;

		int section_count = 0;
		const char* section_icons[2] = { "E", "S" };

		bool favorite_childs = false;
		std::map<ImGuiID, bool> child_states;

		std::string cheat_name = "lunar";
		std::string cheat_year = "2026";

		ID3D11ShaderResourceView* logo_resource = nullptr;
	} gui;

	struct {
		struct {
		    bool enabled = false;
		    bool draw_fov = true;
		    int bone = 0;
		    int mode = 0;
		    int key = VK_RBUTTON;
		    float fov = 20.f;
		    int recoil = 1;
		    int smooth = 5;
		} aimbot;

		struct {
			bool enabled = false;
			int key = VK_XBUTTON2;
			int bone = 0;
			int mode = 0;
			int delay_shot = 100;
		} triggerbot;

		struct {
			bool enabled = true;
			bool agent_name = true;
			bool skeleton = true;
			bool box = true;
			bool lines = true;
			bool head = true;
			bool health = true;
			bool distance = true;
			bool visible_check = true;
			bool spike = true;
			float not_visible_color[4] = { 1.f, 0.0f, 0.0f, 1.f };
			float visible_color[4] = { 0.4f, 0.7f, 1.0f, 1.f };

			bool skills = true;
			bool skills_name = true;
			bool skills_distance = true;
			float skills_color[4] = { 1.f, 0.0f, 0.0f, 1.f };

			bool lineup_helper = true;
			bool streamer_mode = false;

			
		} esp;

	} config;

	float dpi = 1.f;
	int stored_dpi = 100;
	bool dpi_changed = true;

	IDirect3DDevice9* device_dx9 = nullptr;
	ID3D11Device* device_dx11 = nullptr;
	ID3D11DeviceContext* device_context = nullptr;
	IDXGISwapChain* swap_chain = nullptr;
};

inline c_variables* var = new c_variables();
