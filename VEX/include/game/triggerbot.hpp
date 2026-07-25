#include <include/common.hpp>
#include <include/game/sdk/ue_object.hpp>
#include <include/game/sdk/aactor.hpp>
#include <include/render/render.hpp>
#include <include/game/engine/game_engine.hpp>

namespace sky::game::triggerbot {

	using namespace sky::game::engine;
	using namespace sky::game::sdk;

	struct BoneMap {
		int head, neck, chest;
		int lUpperArm, lForeArm, lHand;
		int rUpperArm, rForeArm, rHand;
		int stomach, pelvis;
		int lThigh, lKnee, lFoot;
		int rThigh, rKnee, rFoot;
	};

	std::unordered_map<int, BoneMap> boneMaps = {
		{ 104, {8,21,6, 23,24,25, 49,50,51, 4,3, 77,78,80, 84,85,87} },
		{ 101, {8,21,6, 23,24,25, 49,50,51, 4,3, 75,76,78, 82,83,85} },
		{ 103, {8,9,6, 33,30,32, 58,55,57, 4,3, 63,65,69, 77,79,83} }
	};

	/**
	 * @brief Interface for the triggerbot system
	 */
	class ITriggerbot {
	private:
		std::chrono::steady_clock::time_point last_shot_time;
		
		float DistanceBetweenCross(float X, float Y)
		{
			float xdist = X - (float)(sky::render::m_render->Width / 2.0f);
			float ydist = Y - (float)(sky::render::m_render->Height / 2.0f);

			// Return Euclidean distance
			return sqrtf((xdist * xdist) + (ydist * ydist));
		}
		
		// Optimized version without sqrt for comparisons
		float DistanceSquaredBetweenCross(float X, float Y)
		{
			float xdist = X - (float)(sky::render::m_render->Width / 2.0f);
			float ydist = Y - (float)(sky::render::m_render->Height / 2.0f);

			// Return squared distance (faster for comparisons)
			return (xdist * xdist) + (ydist * ydist);
		}
		
		bool CanShoot() {
			auto now = std::chrono::steady_clock::now();
			auto time_since_last_shot = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_shot_time);
			return time_since_last_shot.count() >= var->config.triggerbot.delay_shot;
		}
		
		// Calculate distance from point to line (skeleton)
		float DistanceToLine(float px, float py, float x1, float y1, float x2, float y2) {
			float A = px - x1;
			float B = py - y1;
			float C = x2 - x1;
			float D = y2 - y1;
			
			float dot = A * C + B * D;
			float len_sq = C * C + D * D;
			
			if (len_sq == 0) {
				// Line has zero length, calculate distance to point
				return sqrtf(A * A + B * B);
			}
			
			float param = dot / len_sq;
			
			float xx, yy;
			if (param < 0) {
				xx = x1;
				yy = y1;
			} else if (param > 1) {
				xx = x2;
				yy = y2;
			} else {
				xx = x1 + param * C;
				yy = y1 + param * D;
			}
			
			float dx = px - xx;
			float dy = py - yy;
			return sqrtf(dx * dx + dy * dy);
		}
		
		// Check if crosshair is near skeleton
		bool IsNearSkeleton(const std::array<std::array<sdk::FVector2D, 2>, 16>& skeleton, float threshold) {
			float crossX = (float)(sky::render::m_render->Width / 2.0f);
			float crossY = (float)(sky::render::m_render->Height / 2.0f);
			
			for (const auto& line : skeleton) {
				float dist = DistanceToLine(crossX, crossY, line[0].X, line[0].Y, line[1].X, line[1].Y);
				if (dist <= threshold) {
					return true;
				}
			}
			return false;
		}

	public:
		ITriggerbot() = default;
		~ITriggerbot() = default;

		void run(sdk::AActor actor, std::array<sdk::FVector2D, 17> bones, std::array<std::array<sdk::FVector2D, 2>, 16> skeleton) {
			if(!var->config.triggerbot.enabled) {
				return;
			}
			if(!sky::render::m_render->IsKeyPushing(var->config.triggerbot.key)) {
				return;
			}
			
			// Check delay before shooting
			if(!CanShoot()) {
				return;
			}
			
			// Check if crosshair is near skeleton (2 pixel threshold)
			if (IsNearSkeleton(skeleton, 2.0f)) {
				sky::driver::g_driver->move_mouse(0, 0, 0x1); //Left mouse down
				sky::driver::g_driver->move_mouse(0, 0, 0x2); //Left mouse up
				
				// Update last shot time
				last_shot_time = std::chrono::steady_clock::now();
			}
		}
	};

	std::shared_ptr<ITriggerbot> m_triggerbot = std::make_shared<ITriggerbot>();
} // namespace sky::game::aimbot