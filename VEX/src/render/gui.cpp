#include <common.hpp>
#include <render/settings/functions.h>

void c_gui::render()
{
	if (!var->gui.enabled)
		return;

	notify->setup_notify();

	static ImVec2 preview_pos;
	static int active_tab = 0;

	gui->set_next_window_size(SCALE(var->window.size));
	gui->set_next_window_pos(ImGui::GetIO().DisplaySize / 2 - SCALE(var->window.size) / 2, ImGuiCond_Once);
	gui->begin(xorstr_("menu"), nullptr, var->window.flags, 0, 0, 0, 0);
	{
		const ImVec2 pos = GetWindowPos();
		const ImVec2 size = GetWindowSize();
		ImDrawList* drawlist = GetWindowDrawList();
		ImGuiStyle* style = &GetStyle();
		preview_pos = pos + ImVec2(size.x + SCALE(10), SCALE(90));

		{
			style->WindowPadding = SCALE(var->window.padding);
			style->ItemSpacing = SCALE(var->window.spacing);
			style->WindowRounding = SCALE(var->window.rounding);
			style->ChildRounding = style->WindowRounding;
			style->WindowBorderSize = SCALE(var->window.border_size);
			style->WindowShadowSize = SCALE(var->window.shadow_size);
			style->ScrollbarSize = SCALE(var->window.scrollbar_size);
		}

		{
			gui->push_style_color(ImGuiCol_ChildBg, draw->get_clr(clr->window.background, 0.8f));

			gui->begin_content(xorstr_("logo_rect"), SCALE(var->window.logo_size));
			{
				// Logo - colored rect (no D3DX11 dependency)
				ImVec2 window_pos = GetWindowPos();
				ImVec2 window_size = GetWindowSize();
				ImVec2 logo_size = SCALE(ImVec2(32.0f, 32.0f));

				ImVec2 logo_pos = ImVec2(
					window_pos.x + (window_size.x - logo_size.x) * 0.5f,
					window_pos.y + (window_size.y - logo_size.y) * 0.5f
				);
			
				GetWindowDrawList()->AddRectFilled(logo_pos, logo_pos + logo_size, ImColor(255, 255, 255, 80), 4.0f);
			}
			gui->end_content();

			gui->sameline(false);

			gui->begin_content(xorstr_("child_select"), SCALE(var->window.child_select_size), SCALE(elements->favorite_button.padding), SCALE(elements->favorite_button.spacing));
			{
				if (gui->favorite_button(xorstr_("Favorites"), xorstr_("A"), 1))
					var->gui.favorite_childs = true;

				gui->sameline(false);

				if (gui->favorite_button(xorstr_("Standart"), xorstr_("B"), 0))
					var->gui.favorite_childs = false;
			}
			gui->end_content();

			gui->begin_content(xorstr_("sections"), ImVec2(SCALE(var->window.section_size.x), var->window.section_size.y), SCALE(elements->section.padding), SCALE(elements->section.spacing));
			{
				for (int i = 0; i < IM_ARRAYSIZE(var->gui.section_icons); i++)
					gui->section(var->gui.section_icons[i], i, var->gui.section_count);

				var->window.section_size.y = GetCurrentWindow()->ContentSize.y + GetStyle().WindowPadding.y * 2;
			}
			gui->end_content();

			gui->sameline(false);

			gui->pop_style_color();
		}

		{
			gui->push_style_color(ImGuiCol_ChildBg, draw->get_clr(clr->window.background, 0.5f));

			var->gui.content_alpha = ImClamp(var->gui.content_alpha + (gui->fixed_speed(6.f) * (var->gui.section_count == active_tab ? 1.f : -1.f)), 0.f, 1.f);

			if (var->gui.content_alpha == 0.f)
				active_tab = var->gui.section_count;

			gui->begin_content(xorstr_("functional"), GetWindowSize() - SCALE(var->window.section_size.x, var->window.child_select_size.y + var->window.topbar_size.y + GetStyle().ItemSpacing.y) - GetStyle().ItemSpacing, SCALE(elements->content.padding), SCALE(elements->content.spacing), false, false);
			{
				gui->push_style_var(ImGuiStyleVar_Alpha, var->gui.content_alpha);

				if (active_tab == 0)
				{
					gui->begin_group();
					{
						gui->begin_child(xorstr_("Players"), xorstr_("F"));
						{
							static float enabled_col[4] = { 0.4f, 1.f, 0.65f, 1.f };
							static bool enabled = true;

							if (gui->checkbox(xorstr_("Enabled"), &var->config.esp.enabled)) {
								notify->add_notify(std::format("ESP {0}", var->config.esp.enabled ? "ENABLED" : "DISABLED"), !var->config.esp.enabled);
							}

							gui->checkbox_with_color(xorstr_("Visible check"), &var->config.esp.visible_check, var->config.esp.visible_color, var->config.esp.not_visible_color, true);
							gui->checkbox(xorstr_("Name"), &var->config.esp.agent_name);
							gui->checkbox(xorstr_("Skeleton"), &var->config.esp.skeleton);
							gui->checkbox(xorstr_("Box"), &var->config.esp.box);
							gui->checkbox(xorstr_("Lines"), &var->config.esp.lines);
							gui->checkbox(xorstr_("Head"), &var->config.esp.head);
							gui->checkbox(xorstr_("Health"), &var->config.esp.health);
							gui->checkbox(xorstr_("Distance"), &var->config.esp.distance);
						}
						gui->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						gui->begin_child(xorstr_("Skills"), xorstr_("I"));
						{
							gui->checkbox_with_color(xorstr_("Skills"), &var->config.esp.skills, &var->config.esp.skills_color, true);
							gui->checkbox(xorstr_("Name"), &var->config.esp.skills_name);
							gui->checkbox(xorstr_("Distance"), &var->config.esp.skills_distance);
						}
						gui->end_child();

						gui->begin_child(xorstr_("Misc"), xorstr_("M"));
						{
							gui->checkbox(xorstr_("Lineup"), &var->config.esp.lineup_helper);
							gui->checkbox(xorstr_("Spike"), &var->config.esp.spike);
							gui->checkbox(xorstr_("Stream Mode"), &var->config.esp.streamer_mode);
						}
						gui->end_child();
					}
					gui->end_group();
				}
				else if (active_tab == 1)
				{
					gui->begin_group();
					{
						gui->begin_child(xorstr_("Notify"), xorstr_("L"));
						{
							if (gui->button(xorstr_("Save config")))
								notify->add_notify(std::string(xorstr_("Config saved with success!")), false);

							if (gui->button(xorstr_("Load Config")))
								notify->add_notify(std::string(xorstr_("Config loaded with success")), false);
						}
						gui->end_child();
					}
					gui->end_group();
				}
				}
				gui->pop_style_var();
			}
			gui->end_content();

			gui->set_cursor_pos(GetCursorPos() + ImVec2(SCALE(var->window.section_size.x) + style->ItemSpacing.x, 0));
			gui->begin_content(xorstr_("topbar_content"), SCALE(var->window.topbar_size));
			{
				draw->render_text(GetWindowDrawList(), var->font.inter[2], GetWindowPos() - SCALE(-10, 1), GetWindowPos() + GetWindowSize() - SCALE(0, 1), draw->get_clr(clr->window.name), var->gui.cheat_name.c_str(), NULL, NULL, ImVec2(0.f, 0.5f));
				draw->render_text(GetWindowDrawList(), var->font.inter[2], GetWindowPos() - SCALE(0, 1), GetWindowPos() + GetWindowSize() - SCALE(10, 1), draw->get_clr(clr->window.year), var->gui.cheat_year.c_str(), NULL, NULL, ImVec2(1.f, 0.5f));
			}
			gui->end_content();
			gui->pop_style_color();
		}
	}
	gui->end();

	draw->watermark("lunar v1.0");
}