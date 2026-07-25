#pragma once
#include <cstdint>
#include <string>
#include <iostream>

// Valorant game offsets
// These are populated at runtime by offset_loader.cpp
// which fetches them from a JSON URL (see CurlSetup.hpp)
//
// If the URL is unreachable, the defaults here are used.

namespace offsets {

    // ---- Game Engine / SDK (UE5 specific) ----
    inline uintptr_t GWorld           = 0xB490930;
    inline uintptr_t FNamePool        = 0xB600040;
    inline uintptr_t FNameState       = 0xB7DC100;
    inline uintptr_t OwningWord       = 0x00C0;
    inline uintptr_t PersistentLevel  = 0x0038;
    inline uintptr_t OwningGameInstance = 0x01D8;
    inline uintptr_t GameState        = 0x0198;
    inline uintptr_t Levels           = 0x01A0;
    inline uintptr_t LocalPlayers     = 0x0040;
    inline uintptr_t ActorArray       = 0x0098;
    inline uintptr_t ViewportClient   = 0x0050;

    // ---- Player / Controller ----
    inline uintptr_t PlayerController  = 0x0038;
    inline uintptr_t AcknowledgedPawn  = 0x0510;
    inline uintptr_t PlayerState       = 0x0480;
    inline uintptr_t PlayerCameraManager = 0x0520;
    inline uintptr_t ControlRotation   = 0x03B0;
    inline uintptr_t CurrentMesh       = 0x0518;
    inline uintptr_t Inventory         = 0x08B0;
    inline uintptr_t CurrentEquippable = 0x08B8;
    inline uintptr_t LocalObserver     = 0x04F8;
    inline uintptr_t MyHUD             = 0x04F0;
    inline uintptr_t CameraCache       = 0x17B0;
    inline uintptr_t DefaultFOV        = 0x01F0;
    inline uintptr_t CurrentFOV        = 0x0528;
    inline uintptr_t POV               = 0x0580;

    // ---- Actor / Entity ----
    inline uintptr_t RootComponent       = 0x0238;
    inline uintptr_t ComponentToWorld    = 0x01E0;
    inline uintptr_t RelativeLocation    = 0x0120;
    inline uintptr_t RelativeRotation    = 0x012C;
    inline uintptr_t ActorID            = 0x00C8;
    inline uintptr_t FNameID            = 0x0018;
    inline uintptr_t Dormant            = 0x00F0;
    inline uintptr_t UniqueID           = 0x0030;
    inline uintptr_t Mesh               = 0x04E8;

    // ---- Skeleton / Bones ----
    inline uintptr_t BoneArray          = 0x05D0;
    inline uintptr_t BoneCount          = 0x05D8;
    inline uintptr_t BoneArrayCache     = 0x05C8;

    // ---- Render / Visibility ----
    inline uintptr_t LastRenderTime     = 0x02E8;
    inline uintptr_t LastSubmitTime     = 0x02E0;
    inline uintptr_t IsVisible          = 0x0690;
    inline uintptr_t bWasAlly           = 0x0F09;

    // ---- Teams ----
    inline uintptr_t TeamComponent      = 0x04A0;
    inline uintptr_t TeamID             = 0x00E8;

    // ---- Health / Damage ----
    inline uintptr_t DamageHandler      = 0x09C0;
    inline uintptr_t Health             = 0x00E0;
    inline uintptr_t MaxHealth          = 0x00E4;
    inline uintptr_t Shield             = 0x0124;
    inline uintptr_t MaxShield          = 0x0128;
    inline uintptr_t ShieldType         = 0x0118;
    inline uintptr_t DamageType         = 0x09C8;
    inline uintptr_t DamageSections     = 0x09D0;

    // ---- Weapons / Equipment ----
    inline uintptr_t MagazineAmmo       = 0x08C0;
    inline uintptr_t MaxAmmo            = 0x08C4;
    inline uintptr_t AuthResourceAmount = 0x08C8;
    inline uintptr_t CurrentEquippableVFXState = 0x08D0;

    // ---- Spike ----
    inline uintptr_t SpikeTimer         = 0x0A98;
    inline uintptr_t SpikeDefuseProgress = 0x05D0;
    inline uintptr_t CurrentDefuseSection = 0x0A90;

    // ---- Outline / Chams ----
    inline uintptr_t OutlineComponent   = 0x0B20;
    inline uintptr_t OutlineMode        = 0x0B28;
    inline uintptr_t EnemyOutline       = 0x0B30;
    inline uintptr_t FresnelIntensity   = 0x0B38;
    inline uintptr_t AttachChildren     = 0x0A80;
    inline uintptr_t AttachChildrenCount = 0x0A88;

    // ---- Minimap ----
    inline uintptr_t CharacterMinimap   = 0x14F0;
    inline uintptr_t PortraitMap        = 0x0B80;
    inline uintptr_t CharacterMap       = 0x0B88;

    // ---- VGK (Vanguard anti-cheat) ----
    namespace vgk {
        inline uintptr_t ShadowRegions  = 0x838F8;
        inline uintptr_t ShadowRegionQ  = 0x83910;
        inline uintptr_t ShadowRegionB  = 0x839C0;
    }

    namespace old_vgk {
        inline uintptr_t ShadowRegions  = 0x82708;
        inline uintptr_t ShadowRegionQ  = 0x82720;
        inline uintptr_t ShadowRegionB  = 0x827D0;
    }

    // ---- ShadowRegion aliases (used by vgk_system) ----
    inline uintptr_t ShadowRegionQ  = vgk::ShadowRegionQ;
    inline uintptr_t ShadowRegionB  = vgk::ShadowRegionB;

    // ---- Derived ----
    inline uintptr_t& fname_pool          = FNamePool;
    inline uintptr_t& persistent_level    = PersistentLevel;
    inline uintptr_t& owning_game_instance = OwningGameInstance;
    inline uintptr_t& game_state          = GameState;
    inline uintptr_t& levels              = Levels;
    inline uintptr_t& local_players       = LocalPlayers;
    inline uintptr_t& actor_array         = ActorArray;
    inline uintptr_t& viewport_client     = ViewportClient;
    inline uintptr_t& player_controller   = PlayerController;
    inline uintptr_t& acknowledged_pawn   = AcknowledgedPawn;
    inline uintptr_t& player_camera       = PlayerCameraManager;
    inline uintptr_t& control_rotation    = ControlRotation;
    inline uintptr_t& root_component      = RootComponent;
    inline uintptr_t& damage_handler      = DamageHandler;
    inline uintptr_t& actor_id            = ActorID;
    inline uintptr_t& fname_id            = FNameID;
    inline uintptr_t& dormant             = Dormant;
    inline uintptr_t& player_state        = PlayerState;
    inline uintptr_t& current_mesh        = CurrentMesh;
    inline uintptr_t& inventory           = Inventory;
    inline uintptr_t& current_equippable  = CurrentEquippable;
    inline uintptr_t& local_observer      = LocalObserver;
    inline uintptr_t& is_visible          = IsVisible;
    inline uintptr_t& component_to_world  = ComponentToWorld;
    inline uintptr_t& bone_array          = BoneArray;
    inline uintptr_t& bone_count          = BoneCount;
    inline uintptr_t& last_submit_time    = LastSubmitTime;
    inline uintptr_t& last_render_time    = LastRenderTime;
    inline uintptr_t& outline_mode        = OutlineMode;
    inline uintptr_t& attach_children     = AttachChildren;
    inline uintptr_t& attach_children_count = AttachChildrenCount;
    inline uintptr_t& outline_component   = OutlineComponent;
    inline uintptr_t& portrait_map        = PortraitMap;
    inline uintptr_t& character_map       = CharacterMap;
    inline uintptr_t& pov                 = POV;
    inline uintptr_t& team_component      = TeamComponent;
    inline uintptr_t& team_id             = TeamID;
    inline uintptr_t& current_health      = Health;
    inline uintptr_t& max_life            = MaxHealth;
    inline uintptr_t& relative_location   = RelativeLocation;
    inline uintptr_t& relative_rotation   = RelativeRotation;
    inline uintptr_t& default_fov         = DefaultFOV;
    inline uintptr_t& camera_cache        = CameraCache;
    inline uintptr_t& location            = RelativeLocation;
    inline uintptr_t& rotation            = RelativeRotation;
    inline uintptr_t& current_fov         = CurrentFOV;
    inline uintptr_t& enemy_outline       = EnemyOutline;
    inline uintptr_t& bone_array_cache    = BoneArrayCache;
    inline uintptr_t& current_defuse_section = CurrentDefuseSection;
    inline uintptr_t& DefuseSection = CurrentDefuseSection;
    inline uintptr_t& magazine_ammo       = MagazineAmmo;
    inline uintptr_t& auth_resource_amount = AuthResourceAmount;
    inline uintptr_t& max_ammo            = MaxAmmo;
    inline uintptr_t& spike_timer         = SpikeTimer;
    inline uintptr_t& my_hud              = MyHUD;
    inline uintptr_t& HP                  = Health;
    inline uintptr_t& MaxHP               = MaxHealth;
    // DamageType, DamageSections, CurrentEquippableVFXState, and
    // FresnelIntensity are declared directly above — no aliases needed.

    // Fetches offsets from URL (live streaming)
    void initialize();

} // namespace offsets
