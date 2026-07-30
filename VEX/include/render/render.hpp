#pragma once

#include <common.hpp>
#include <include/render/settings/functions.h>
#include <include/render/data/fonts.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_freetype.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

namespace sky::render {

	class RENDER {
	public:
		RENDER() = default;
		~RENDER();

		bool init();
		void begin();
		void end();
		void shutdown();
		float get_fps() const;

		void UpdateKeyStates();
		void UpdateMouseState();

		void set_streamer_mode(bool enabled) { m_streamer_mode = enabled; }

		HWND get_window_handle() const { return hWnd; }
		ID3D11Device* get_device() const { return device; }
		ID3D11DeviceContext* get_device_context() const { return device_context; }

		float Width{ 0.0f };
		float Height{ 0.0f };

    public:
        void AddText(ImVec2 pos, ImU32 color, const char* text, const char* text_end = nullptr) {
            ImGui::GetBackgroundDrawList()->AddText(pos, color, text, text_end);
        }

		void DrawLine(const ImVec2& aPoint1, const ImVec2 aPoint2, ImU32 aColor, const FLOAT aLineWidth) {
			// Get screen dimensions for validation
			ImGuiIO& io = ImGui::GetIO();
			float screen_width = io.DisplaySize.x;
			float screen_height = io.DisplaySize.y;
			
			// Check if points are valid (not NaN or infinite)
			if (!std::isfinite(aPoint1.x) || !std::isfinite(aPoint1.y) || 
				!std::isfinite(aPoint2.x) || !std::isfinite(aPoint2.y)) {
				return; // Do not draw if coordinates invalid
			}
			
			// Check if points are within screen bounds (with margin)
			const float margin = 100.0f;
			if (aPoint1.x < -margin || aPoint1.x > screen_width + margin || 
				aPoint1.y < -margin || aPoint1.y > screen_height + margin ||
				aPoint2.x < -margin || aPoint2.x > screen_width + margin || 
				aPoint2.y < -margin || aPoint2.y > screen_height + margin) {
				return; // Do not draw if outside limits
			}
			
			// Check if line is not too long (avoid absurd lines)
			float dx = aPoint2.x - aPoint1.x;
			float dy = aPoint2.y - aPoint1.y;
			float distance = sqrt(dx * dx + dy * dy);
			
			// Limit maximum distance
			const float max_distance = (screen_width + screen_height) * 1.5f;
			if (distance > max_distance) {
				return; // Do not draw if distance is absurd
			}
			
			auto vList = ImGui::GetBackgroundDrawList();
			vList->AddLine(aPoint1, aPoint2, aColor, aLineWidth);
		}

		void DrawSkeletonLine(const ImVec2& from, const ImVec2& to, ImU32 color, float thickness, float curve_factor = 0.12f) {
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			
			// Get screen dimensions for validation
			ImGuiIO& io = ImGui::GetIO();
			float screen_width = io.DisplaySize.x;
			float screen_height = io.DisplaySize.y;
			
			// Check if points are within screen bounds (with margin)
			const float margin = 50.0f;
			if (from.x < -margin || from.x > screen_width + margin || 
				from.y < -margin || from.y > screen_height + margin ||
				to.x < -margin || to.x > screen_width + margin || 
				to.y < -margin || to.y > screen_height + margin) {
				return; // Do not draw if outside limits
			}
			
			// Calculate distance and direction
			float dx = to.x - from.x;
			float dy = to.y - from.y;
			float distance = sqrt(dx * dx + dy * dy);
			
			// If distance is very small, draw straight line
			if (distance < 15.0f) {
				draw->AddLine(from, to, color, thickness);
				return;
			}
			
			// Calculate midpoint
			ImVec2 mid = ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
			
			// Calculate perpendicular for natural curvature
			float perpX = -dy / distance;
			float perpY = dx / distance;
			
			// Adjust curvature based on distance (more subtle for skeleton)
			float curve_offset = distance * curve_factor;
			ImVec2 control = ImVec2(
				mid.x + perpX * curve_offset, 
				mid.y + perpY * curve_offset
			);
			
			// Check if control point is also within bounds
			if (control.x < -margin || control.x > screen_width + margin || 
				control.y < -margin || control.y > screen_height + margin) {
				// If control point is outside, use straight line
				draw->AddLine(from, to, color, thickness);
				return;
			}
			
			// Draw smooth quadratic Bezier curve
			draw->AddBezierQuadratic(from, control, to, color, thickness, 12);
		}

		void DrawPlayerName(const ImVec2& position, const std::string& name, ImU32 color) {
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			
			// Check if name is valid
			if (name.empty()) {
				return;
			}
			
			// Get default font
			ImFont* font = ImGui::GetFont();
			if (!font) {
				return;
			}
			
			// Calculate text size
			ImVec2 text_size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, name.c_str());
			
			// Check if coordinates are valid
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || 
				!std::isfinite(text_size.x) || !std::isfinite(text_size.y)) {
				return;
			}
			
			// Calculate centered position
			ImVec2 text_pos = ImVec2(
				position.x - (text_size.x * 0.5f),  // Center horizontally
				position.y                          // Direct Y position
			);
			
			// Draw semi-transparent background for better readability
			const float padding = 2.0f;
			ImVec2 bg_min = ImVec2(text_pos.x - padding, text_pos.y - padding);
			ImVec2 bg_max = ImVec2(text_pos.x + text_size.x + padding, text_pos.y + text_size.y + padding);
			
			draw->AddRectFilled(bg_min, bg_max, IM_COL32(0, 0, 0, 120), 2.0f); // Dark background with rounded corners
			
			// Draw the name
			draw->AddText(font, font->FontSize, text_pos, color, name.c_str());
		}

		void DrawTracerLine(const ImVec2& target_pos, ImU32 color, float thickness = 1.0f) {
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			
			// Get screen size
			ImGuiIO& io = ImGui::GetIO();
			float screen_width = io.DisplaySize.x;
			float screen_height = io.DisplaySize.y;
			
			// Validate if screen dimensions are valid
			if (screen_width <= 0.0f || screen_height <= 0.0f || 
				!std::isfinite(screen_width) || !std::isfinite(screen_height)) {
				return; // Do not draw if invalid screen dimensions
			}
			
			// Check if target_pos is valid (not NaN or infinite)
			if (!std::isfinite(target_pos.x) || !std::isfinite(target_pos.y)) {
				return; // Do not draw if invalid target coordinates
			}
			
			// SPECIFIC VALIDATIONS TO AVOID LINES TO INVALID CORNERS
			
			// Detect suspicious coordinates near (0,0) - top-left corner
			if ((target_pos.x >= -10.0f && target_pos.x <= 10.0f) && 
				(target_pos.y >= -10.0f && target_pos.y <= 10.0f)) {
				return; // Do not draw if too close to top-left corner
			}
			
			// Detect coordinates very close to screen edges (suspicious)
			const float edge_threshold = 20.0f;
			if ((target_pos.x >= -edge_threshold && target_pos.x <= edge_threshold) || // Very close to left edge
				(target_pos.x >= screen_width - edge_threshold && target_pos.x <= screen_width + edge_threshold) || // Very close to right edge
				(target_pos.y >= -edge_threshold && target_pos.y <= edge_threshold) || // Very close to top
				(target_pos.y >= screen_height - edge_threshold && target_pos.y <= screen_height + edge_threshold)) { // Very close to bottom
				
				// Check if not a valid position inside screen
				if (target_pos.x < edge_threshold || target_pos.x > screen_width - edge_threshold ||
					target_pos.y < edge_threshold || target_pos.y > screen_height - edge_threshold) {
					return; // Do not draw if in suspicious edges
				}
			}
			
			// Origin point: bottom center of screen
			ImVec2 screen_center_bottom = ImVec2(screen_width * 0.5f, screen_height);
			
			// Validate if origin point is valid
			if (!std::isfinite(screen_center_bottom.x) || !std::isfinite(screen_center_bottom.y)) {
				return; // Do not draw if invalid origin
			}
			
			// Allow exiting screen up to 20px, but block if exiting much further
			const float allowed_outside = 20.0f;
			if (target_pos.x < -allowed_outside || target_pos.x > screen_width + allowed_outside || 
				target_pos.y < -allowed_outside || target_pos.y > screen_height + allowed_outside) {
				return; // Do not draw if more than 20px outside screen
			}
			
			// Check if line will not be too long (avoid absurd lines)
			float dx = target_pos.x - screen_center_bottom.x;
			float dy = target_pos.y - screen_center_bottom.y;
			float distance = sqrt(dx * dx + dy * dy);
			
			// Validate if distance is finite
			if (!std::isfinite(distance)) {
				return; // Do not draw if invalid distance
			}
			
			// Limit maximum distance da tracer line (mais restritivo)
			const float max_distance = sqrt(screen_width * screen_width + screen_height * screen_height) * 0.8f; // 80% of diagonal (more restrictive)
			if (distance > max_distance || distance < 10.0f) {
				return; // Do not draw if distance is absurd ou muito pequena
			}
			
			// FINAL VALIDATION: Check if line does not pass through suspicious coordinates
			// Calculate some intermediate line points to verify
			for (float t = 0.1f; t <= 0.9f; t += 0.2f) {
				float check_x = screen_center_bottom.x + (dx * t);
				float check_y = screen_center_bottom.y + (dy * t);
				
				// If any intermediate point is in suspicious area, do not draw
				if ((check_x >= -10.0f && check_x <= 10.0f && check_y >= -10.0f && check_y <= 10.0f) ||
					check_x < -allowed_outside || check_y < -allowed_outside || 
					check_x > screen_width + allowed_outside || check_y > screen_height + allowed_outside) {
					return; // Do not draw if line passes through problematic area
				}
			}
			
			// Draw straight line from bottom center to target
			draw->AddLine(screen_center_bottom, target_pos, color, thickness);
		}

		void HealthBar(float x, float y, float w, float h, int value, int v_max)
		{
			if (v_max <= 0) return;
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(0.f, 0.f, 0.f, 0.725f), 0.f, 0);
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ((h / float(v_max)) * (float)value)), ImColor(std::fmin(510 * (v_max - value) / 100, 255), std::fmin(510 * value / 100, 255), 25, 255), 0.0f, 0);
		}

		// Structure for health bar animation
		struct health_bar_state {
			float animated_health = 100.0f;
		};

		// Structure for damage flash effect on box
		struct damage_flash_state {
			float last_health = 100.0f;
			float flash_alpha = 0.0f;
			float flash_timer = 0.0f;
		};

		// Helper function to convert HSV to RGB
		ImU32 HSVtoRGB(float h, float s, float v, float a = 1.0f) {
			float c = v * s;
			float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
			float m = v - c;
			
			float r, g, b;
			if (h >= 0 && h < 60) {
				r = c; g = x; b = 0;
			} else if (h >= 60 && h < 120) {
				r = x; g = c; b = 0;
			} else if (h >= 120 && h < 180) {
				r = 0; g = c; b = x;
			} else if (h >= 180 && h < 240) {
				r = 0; g = x; b = c;
			} else if (h >= 240 && h < 300) {
				r = x; g = 0; b = c;
			} else {
				r = c; g = 0; b = x;
			}
			
			return IM_COL32((r + m) * 255, (g + m) * 255, (b + m) * 255, a * 255);
		}

		void HorizontalHealthBar(float x, float y, float w, float h, int value, int v_max, ImGuiID id = 0)
		{
			// Animation system to smooth health changes
			health_bar_state* state = nullptr;
			if (id != 0) {
				state = static_cast<health_bar_state*>(ImGui::GetStateStorage()->GetVoidPtr(id));
				if (!state) {
					state = new health_bar_state();
					state->animated_health = (float)value; // Initialize with current value
					ImGui::GetStateStorage()->SetVoidPtr(id, state);
				}
				
				// Animate health: slow decay, fast rise
				float target_health = (float)value;
				float speed = (target_health > state->animated_health) ? 15.0f : 4.0f; // Fast rise, slow descent
				state->animated_health = ImLerp(state->animated_health, target_health, ImGui::GetIO().DeltaTime * speed);
			}
			
			// Use animated health if available, otherwise use real value
			float display_health = (state && id != 0) ? state->animated_health : (float)value;
			float healthPercent = display_health / (float)v_max;
			float currentWidth = w * healthPercent;
			
			// Draw dark border
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1), ImColor(0, 0, 0, 180), 0.f, 0);
			
			// Draw gray background for empty part
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(40, 40, 40, 200), 0.0f, 0);
			
			// If there is health to show, create HSV gradient based on current health
			if (currentWidth > 0.0f) {
				// Calculate HUE based on player current health
				// 100% health = Green (120°), 0% health = Red (0°)
				float startHue = 120.0f * healthPercent; // HUE at start of bar
				float endHue = 0.0f;                     // HUE no fim da barra (sempre vermelho)
				
				// If health is very high, make more subtle gradient
				if (healthPercent > 0.8f) {
					endHue = 90.0f; // Green to Yellow-Green
				} else if (healthPercent > 0.5f) {
					endHue = 60.0f; // Green/Yellow to Yellow
				} else if (healthPercent > 0.2f) {
					endHue = 30.0f; // Yellow to Orange
				}
				// else: endHue = 0.0f (Red)
				
				// Use only a smooth gradient
				ImU32 startColor = HSVtoRGB(startHue, 1.0f, 1.0f);
				ImU32 endColor = HSVtoRGB(endHue, 1.0f, 1.0f);
				
				// Draw ultra smooth single gradient
				ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(
					ImVec2(x, y),
					ImVec2(x + currentWidth, y + h),
					startColor,  // Top-left: Color based on current health
					endColor,    // Top-right: Transition color
					endColor,    // Bottom-right: Transition color
					startColor   // Bottom-left: Color based on current health
				);
			}
		}

		void CorneredBox(ImVec2 bottomCenter, float boxWidth, float pTop, float pBottom, ImColor color, float thickness, bool filled = true, float current_health = -1.0f, ImGuiID id = 0, float rounding = 4.0f)
		{
			// Damage flash system
			damage_flash_state* flash_state = nullptr;
			if (current_health >= 0.0f && id != 0) {
				flash_state = static_cast<damage_flash_state*>(ImGui::GetStateStorage()->GetVoidPtr(id));
				if (!flash_state) {
					flash_state = new damage_flash_state();
					flash_state->last_health = current_health; // Initialize with current health
					ImGui::GetStateStorage()->SetVoidPtr(id, flash_state);
				}
				
				// Detect damage (health decreased)
				if (current_health < flash_state->last_health) {
					flash_state->flash_alpha = 80.0f; // Initial alpha of red flash
					flash_state->flash_timer = 0.0f;   // Reset timer
				}
				
				// Update previous health
				flash_state->last_health = current_health;
				
				// Animate flash (fast fade out)
				if (flash_state->flash_alpha > 0.0f) {
					flash_state->flash_timer += ImGui::GetIO().DeltaTime;
					// Flash lasts 0.3 seconds
					float flash_duration = 0.3f;
					float fade_progress = flash_state->flash_timer / flash_duration;
					
					if (fade_progress >= 1.0f) {
						flash_state->flash_alpha = 0.0f;
					} else {
						// Smooth fade out
						flash_state->flash_alpha = 80.0f * (1.0f - fade_progress);
					}
				}
			}

			float halfWidth = boxWidth / 2.0f;
			float boxHeight = (pBottom - pTop + 2.f);

			float cornerLengthX = boxHeight * 0.10f; // 10% da altura para horizontal
			float cornerLengthY = boxHeight * 0.10f; // 10% da altura para vertical

			ImDrawList* draw = ImGui::GetForegroundDrawList();

			ImVec2 topLeft(bottomCenter.x - halfWidth, pTop);
			ImVec2 bottomRight(bottomCenter.x + halfWidth, pBottom);

			if (filled) {
				// Normal box background with rounded corners
				draw->AddRectFilled(
					topLeft, bottomRight,
					IM_COL32(
						0,
						0,
						0,
						50   // alpha transparency
					),
					rounding  // Cantos arredondados
				);
				
				// Red flash when taking damage (also with rounded corners)
				if (flash_state && flash_state->flash_alpha > 0.0f) {
					draw->AddRectFilled(
						topLeft, bottomRight,
						IM_COL32(
							255,  // Red
							0,    // Green
							0,    // Azul
							(int)flash_state->flash_alpha  // Alpha animado
						),
						rounding  // Rounded corners on flash too
					);
				}
			}

			// Draw only rounded corners (original style)
			const float PI = 3.14159265359f;
			
			// Top Left Corner (rounded arc)
			ImVec2 tl_center = ImVec2(bottomCenter.x - halfWidth + rounding, pTop + rounding);
			draw->PathClear();
			draw->PathArcTo(tl_center, rounding, PI, PI * 1.5f, 8);
			draw->PathStroke(color, false, thickness);
			// Top left corner lines
			draw->AddLine(ImVec2(bottomCenter.x - halfWidth, pTop + rounding),
				ImVec2(bottomCenter.x - halfWidth, pTop + cornerLengthY), color, thickness);
			draw->AddLine(ImVec2(bottomCenter.x - halfWidth + rounding, pTop),
				ImVec2(bottomCenter.x - halfWidth + cornerLengthX, pTop), color, thickness);

			// Top Right Corner (rounded arc)
			ImVec2 tr_center = ImVec2(bottomCenter.x + halfWidth - rounding, pTop + rounding);
			draw->PathClear();
			draw->PathArcTo(tr_center, rounding, PI * 1.5f, PI * 2.0f, 8);
			draw->PathStroke(color, false, thickness);
			// Top right corner lines
			draw->AddLine(ImVec2(bottomCenter.x + halfWidth, pTop + rounding),
				ImVec2(bottomCenter.x + halfWidth, pTop + cornerLengthY), color, thickness);
			draw->AddLine(ImVec2(bottomCenter.x + halfWidth - rounding, pTop),
				ImVec2(bottomCenter.x + halfWidth - cornerLengthX, pTop), color, thickness);

			// Bottom Left Corner (rounded arc)
			ImVec2 bl_center = ImVec2(bottomCenter.x - halfWidth + rounding, pBottom - rounding);
			draw->PathClear();
			draw->PathArcTo(bl_center, rounding, PI * 0.5f, PI, 8);
			draw->PathStroke(color, false, thickness);
			// Bottom left corner lines
			draw->AddLine(ImVec2(bottomCenter.x - halfWidth, pBottom - rounding),
				ImVec2(bottomCenter.x - halfWidth, pBottom - cornerLengthY), color, thickness);
			draw->AddLine(ImVec2(bottomCenter.x - halfWidth + rounding, pBottom),
				ImVec2(bottomCenter.x - halfWidth + cornerLengthX, pBottom), color, thickness);

			// Bottom Right Corner (rounded arc)
			ImVec2 br_center = ImVec2(bottomCenter.x + halfWidth - rounding, pBottom - rounding);
			draw->PathClear();
			draw->PathArcTo(br_center, rounding, 0.0f, PI * 0.5f, 8);
			draw->PathStroke(color, false, thickness);
			// Bottom right corner lines
			draw->AddLine(ImVec2(bottomCenter.x + halfWidth, pBottom - rounding),
				ImVec2(bottomCenter.x + halfWidth, pBottom - cornerLengthY), color, thickness);
			draw->AddLine(ImVec2(bottomCenter.x + halfWidth - rounding, pBottom),
				ImVec2(bottomCenter.x + halfWidth - cornerLengthX, pBottom), color, thickness);
		}

		void DrawCircle(ImVec2 center, float radius, const ImU32& color, int segments, float thickness)
		{
			ImGui::GetBackgroundDrawList()->AddCircle(center, radius, color, segments, thickness);
		}

		void DrawCircleFilled(ImVec2 center, float radius, const ImU32& color, int segments)
		{
			ImGui::GetBackgroundDrawList()->AddCircleFilled(center, radius, color, segments);
		}

		inline bool IsKeyPushing(int vKey) { return (GetAsyncKeyState(vKey) & 0x8000) != 0; }
		inline bool IsKeyPushed(int vKey) {
			static bool bSaveState[0x100] = { 0 };
			const bool bPrevState = bSaveState[(uint8_t)vKey];
			const bool bCurrState = IsKeyPushing(vKey);
			const bool Result = !bPrevState && bCurrState;
			bSaveState[(uint8_t)vKey] = bCurrState;
			return Result;
		}

	private:
		void create_target_view();
		void cleanup_target_view();

	private:
		HWND hWnd { nullptr };
		WNDCLASSW window_class { };
		ID3D11Device* device { nullptr };
		ID3D11DeviceContext* device_context { nullptr };
		IDXGISwapChain* swap_chain { nullptr };
		ID3D11RenderTargetView* render_target_view { nullptr };
		bool m_streamer_mode { false };
	};

	inline std::unique_ptr<RENDER> m_render = std::make_unique<RENDER>();

} // namespace sky::ui


