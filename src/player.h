/*
player.h
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "irrlichttypes_bloated.h"
#include "inventory.h"
#include "constants.h"
#include "network/networkprotocol.h"
#include "util/basic_macros.h"
#include <atomic>
#include <list>
#include "threading/lock.h"
#include "json/json.h"
#include <mutex>

#define PLAYERNAME_SIZE 20

#define PLAYERNAME_ALLOWED_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
#define PLAYERNAME_ALLOWED_CHARS_USER_EXPL "'a' to 'z', 'A' to 'Z', '0' to '9', '-', '_'"

struct PlayerFovSpec
{
	f32 fov;

	// Whether to multiply the client's FOV or to override it
	bool is_multiplier;

	// The time to be take to trasition to the new FOV value.
	// Transition is instantaneous if omitted. Omitted by default.
	f32 transition_time;
};

struct PlayerControl
{
	PlayerControl() = default;

	PlayerControl(
		bool a_up, bool a_down, bool a_left, bool a_right,
		bool a_jump, bool a_aux1, bool a_sneak,
		bool a_zoom,
		bool a_dig, bool a_place,
		float a_pitch, float a_yaw,
		float a_movement_speed, float a_movement_direction
	)
	{
		// Encode direction keys into a single value so nobody uses it accidentally
		// as movement_{speed,direction} is supposed to be the source of truth.
		direction_keys = (a_up&1) | ((a_down&1) << 1) |
			((a_left&1) << 2) | ((a_right&1) << 3);
		jump = a_jump;
		aux1 = a_aux1;
		sneak = a_sneak;
		zoom = a_zoom;
		dig = a_dig;
		place = a_place;
		pitch = a_pitch;
		yaw = a_yaw;
		movement_speed = a_movement_speed;
		movement_direction = a_movement_direction;
	}

#ifndef SERVER
	// For client use
	u32 getKeysPressed() const;
	inline bool isMoving() const { return movement_speed > 0.001f; }
#endif

	// For server use
	void unpackKeysPressed(u32 keypress_bits);

	u8 direction_keys = 0;
	bool jump = false;
	bool aux1 = false;
	bool sneak = false;
	bool zoom = false;
	bool dig = false;
	bool place = false;
	// Note: These four are NOT available on the server
	float pitch = 0.0f;
	float yaw = 0.0f;
	float movement_speed = 0.0f;
	float movement_direction = 0.0f;
};

struct PlayerPhysicsOverride
{
	float speed = 1.f;
	float jump = 1.f;
	float gravity = 1.f;

	bool sneak = true;
	bool sneak_glitch = false;
	// "Temporary" option for old move code
	bool new_move = true;

	float speed_climb = 1.f;
	float speed_crouch = 1.f;
	float liquid_fluidity = 1.f;
	float liquid_fluidity_smooth = 1.f;
	float liquid_sink = 1.f;
	float acceleration_default = 1.f;
	float acceleration_air = 1.f;
};

struct PlayerSettings
{
	bool free_move = false;
	bool pitch_move = false;
	bool fast_move = false;
	bool continuous_forward = false;
	bool always_fly_fast = false;
	bool aux1_descends = false;
	bool noclip = false;
	bool autojump = false;

	const std::string setting_names[8] = {
		"free_move", "pitch_move", "fast_move", "continuous_forward", "always_fly_fast",
		"aux1_descends", "noclip", "autojump"
	};
	void readGlobalSettings();
};

class Map;
struct CollisionInfo;
struct HudElement;
class Environment;

class Player
: public shared_locker
{
public:

	Player(const std::string & name, IItemDefManager *idef);
	virtual ~Player() = 0;

	DISABLE_CLASS_COPY(Player);

	virtual void move(f32 dtime, Environment *env, f32 pos_max_d)
	{}
	virtual void move(f32 dtime, Environment *env, f32 pos_max_d,
			std::vector<CollisionInfo> *collision_info)
	{}

	// in BS-space
	v3f getSpeed() const
	{
		auto lock = lock_shared();
		return m_speed;
	}

	// in BS-space
	void setSpeed(v3f speed)
	{
		auto lock = lock_unique();
		m_speed = speed;
	}

	void addSpeed(v3f speed);

/*
	const char *getName() const { return m_name; }
*/
	const std::string &getName() const { return m_name; }

	u32 getFreeHudID()
	{
		size_t size = hud.size();
		for (size_t i = 0; i != size; i++) {
			if (!hud[i])
				return i;
		}
		return size;
	}

	v3f eye_offset_first;
	v3f eye_offset_third;
	v3f eye_offset_third_front;

	Inventory inventory;

	f32 movement_acceleration_default;
	f32 movement_acceleration_air;
	f32 movement_acceleration_fast;
	f32 movement_speed_walk;
	f32 movement_speed_crouch;
	f32 movement_speed_fast;
	f32 movement_speed_climb;
	f32 movement_speed_jump;
	f32 movement_liquid_fluidity;
	f32 movement_liquid_fluidity_smooth;
	f32 movement_liquid_sink;
	f32 movement_gravity;
	f32 movement_fall_aerodynamics;

	v2s32 local_animations[4];
	float local_animation_speed;

	std::string inventory_formspec;
	std::string formspec_prepend;

	PlayerControl control;
	std::mutex control_mutex;
	const PlayerControl& getPlayerControl() {
   		 std::lock_guard<std::mutex> lock(control_mutex);
		 return control; }
	PlayerPhysicsOverride physics_override;
	PlayerSettings &getPlayerSettings() { return m_player_settings; }
	static void settingsChangedCallback(const std::string &name, void *data);

	// Returns non-empty `selected` ItemStack. `hand` is a fallback, if specified
	ItemStack &getWieldedItem(ItemStack *selected, ItemStack *hand) const;
	void setWieldIndex(u16 index);
	u16 getWieldIndex() const { return m_wield_index; }

	void setFov(const PlayerFovSpec &spec)
	{
		m_fov_override_spec = spec;
	}

	const PlayerFovSpec &getFov() const
	{
		return m_fov_override_spec;
	}

	HudElement* getHud(u32 id);
	u32         addHud(HudElement* hud);
	HudElement* removeHud(u32 id);
	void        clearHud();

	u32 hud_flags;
	s32 hud_hotbar_itemcount;

    // fm:
	std::string hotbar_image;
	int hotbar_image_items = 0;
	std::string hotbar_selected_image;

	std::string m_name;

protected:
	//char m_name[PLAYERNAME_SIZE];
	v3f m_speed; // velocity; in BS-space
	std::atomic_uint16_t m_wield_index {0};
	PlayerFovSpec m_fov_override_spec = { 0.0f, false, 0.0f };

	std::vector<HudElement *> hud;
private:
	// Protect some critical areas
	// hud for example can be modified by EmergeThread
	// and ServerThread
	std::mutex m_mutex;
	PlayerSettings m_player_settings;
};
