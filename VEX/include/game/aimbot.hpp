#include <include/common.hpp>
#include <include/game/sdk/ue_object.hpp>
#include <include/game/sdk/aactor.hpp>
#include <include/render/render.hpp>
#include <include/game/engine/game_engine.hpp>

namespace sky::game::aimbot {

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
	 * @brief Interface for the aimbot system
	 */
	class IAimbot {
	private:
		sdk::AActor m_target;
		std::uintptr_t bone_array;
		float		distance = 9999.f;

		void reset_target()
		{
			m_target = sdk::AActor();
			distance = 9999.f;
		}

		sdk::FRotator CalculateAngles(sdk::FVector src, sdk::FVector dst)
		{
			auto delta = src - dst;
			double hyp = sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
			if(hyp < 0.0001) {
				return sdk::FRotator(0.0, 0.0, 0.0);
			}
			sdk::FRotator angles;
			angles.Pitch = (double)(acos(delta.Z / hyp) * sdk::Constants::RAD_TO_DEG);
			angles.Yaw = (double)(atan(delta.Y / delta.X) * sdk::Constants::RAD_TO_DEG);
			angles.Roll = 0.0;

			angles.Pitch += 270.f;
			if (angles.Pitch > 360.f) {
				angles.Pitch -= 360.f;
			}
			if (delta.X >= 0.0f) {
				angles.Yaw += 180.0f;
			}
			if (angles.Yaw < 0.f) {
				angles.Yaw += 360.f;
			}

			return angles;
		}
	public:
		IAimbot() = default;
		~IAimbot() = default;

		float DistanceBetweenCross(float X, float Y)
		{
			float xdist = X - (float)(sky::render::m_render->Width / 2.0f);
			float ydist = Y - (float)(sky::render::m_render->Height / 2.0f);

			// Manually square the differences and take the square root.
			float Hypotenuse = sqrtf((xdist * xdist) + (ydist * ydist));
			return Hypotenuse;
		}

        void try_select_target(sdk::AActor candidate, std::array<sdk::FVector2D, 17> bones)
        {
			if (!var->config.aimbot.enabled)
				return;

			for (const auto& bone : bones) {
				const float distToCross = DistanceBetweenCross((float)bone.X, (float)bone.Y);
				if (distToCross < var->config.aimbot.fov && distToCross < distance) {
					distance = distToCross;
					m_target = candidate;
					bone_array = candidate.BoneArray();
				}
			}
        }

		void run() {
			if (!var->config.aimbot.enabled)
			{
				reset_target();
				return;
			}

			if (!sky::render::m_render->IsKeyPushing(var->config.aimbot.key)) {
				reset_target();
				return;
			}

			if(!m_target.is_valid() || m_target.DamageHandler().GetHealth() <= 0.f) {
				reset_target();
				return;
			}

			/*if (!m_target.IsVisible(m_target.Mesh()))
			{
				reset_target();
				return;
			}*/

			auto bone_count = m_target.BoneCount(bone_array);
			if( bone_count <= 0) {
				reset_target();
				return;
			}
			auto it = boneMaps.find(bone_count);

			BoneMap& map = it->second;

			sdk::FVector selected_bone;
			if(var->config.aimbot.mode == 0) {
				switch(var->config.aimbot.bone) {
					case 0: // Head
						selected_bone = m_target.GetBoneWithRotation(m_target.Mesh(), bone_array, map.head);
						break;
					case 1: // Neck
						selected_bone = m_target.GetBoneWithRotation(m_target.Mesh(), bone_array, map.neck);
						break;
					case 2: // Chest
						selected_bone = m_target.GetBoneWithRotation(m_target.Mesh(), bone_array, map.chest);
						break;
					default:
						selected_bone = m_target.GetBoneWithRotation(m_target.Mesh(), bone_array, map.chest); // Default to chest
				}
			} else {
				// Find nearest bone
				float min_distance = std::numeric_limits<float>::max();
				for (const auto& bone_idx : { map.head, map.neck, map.chest, map.lUpperArm, map.lForeArm, map.lHand,
											 map.rUpperArm, map.rForeArm, map.rHand, map.stomach, map.pelvis,
											 map.lThigh, map.lKnee, map.lFoot, map.rThigh, map.rKnee, map.rFoot }) {
					sdk::FVector bone_pos = m_target.GetBoneWithRotation(m_target.Mesh(), bone_array, bone_idx);
					float dist = DistanceBetweenCross(bone_pos.X, bone_pos.Y);
					if (dist < min_distance) {
						min_distance = dist;
						selected_bone = bone_pos;
					}
				}
			}

			if (selected_bone.X == 0. && selected_bone.Y == 0. && selected_bone.Z == 0.)
			{
				reset_target();
				return;
			}

			auto w2s = m_game_engine->ProjectWorldToScreen(selected_bone);
			if(!w2s.isValid()) {
				reset_target();
				return;
			}

			float distToCross = DistanceBetweenCross((float)w2s.X, (float)w2s.Y);

			// Reset if left FOV
			if (distToCross > var->config.aimbot.fov) {
				reset_target();
				return;
			}

			// Deadzone = half of configured FOV
			float deadzone = var->config.aimbot.fov * 0.2f;

			// If already inside deadzone → do not move aim
			if (distToCross <= deadzone) {
				return;
			}

			if (!sky::game::engine::m_cameraCache.HasValidCache)
			{
				reset_target();
				return;
			}

			auto CurrentRotation = m_game_engine->get_world_data().player_controller.ControlRotation();


			auto AimRotation = CalculateAngles(sky::game::engine::m_cameraCache.LastValidLocation, selected_bone);
			sdk::FRotator SmoothedRotation;
			if (var->config.aimbot.recoil > 0) {
				sdk::FRotator ControlRotation = CurrentRotation;
				FRotator DeltaRotation = sky::game::engine::m_cameraCache.LastValidRotation - ControlRotation;
				DeltaRotation.Normalize();

				FRotator ConvertRotation = AimRotation - (DeltaRotation * (double)var->config.aimbot.recoil);
				ConvertRotation.Normalize();

				DeltaRotation = ConvertRotation - sky::game::engine::m_cameraCache.LastValidRotation;
				DeltaRotation.Normalize();

				SmoothedRotation = CurrentRotation + DeltaRotation / (double)var->config.aimbot.smooth;
			}
			else {
				FRotator DeltaRotation = AimRotation - CurrentRotation;
				DeltaRotation.Normalize();

				SmoothedRotation = CurrentRotation + DeltaRotation / (double)var->config.aimbot.smooth;
			}

			m_game_engine->get_world_data().player_controller.SetControlRotation(SmoothedRotation);
		}
	};

	std::shared_ptr<IAimbot> m_aimbot = std::make_shared<IAimbot>();
} // namespace sky::game::aimbot