/*
collision.h
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>
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

#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "irrlichttypes_bloated.h"
#include <vector>

class Map;
class IGameDef;
class Environment;
class ActiveObject;

enum CollisionType
{
	COLLISION_NODE,
	COLLISION_OBJECT,
};

enum CollisionAxis
{
	COLLISION_AXIS_NONE = -1,
	COLLISION_AXIS_X,
	COLLISION_AXIS_Y,
	COLLISION_AXIS_Z,
};

struct CollisionInfo
{
	CollisionInfo() = default;

	CollisionType type = COLLISION_NODE;
	CollisionAxis axis = COLLISION_AXIS_NONE;
	v3pos_t node_p = v3pos_t(-32768,-32768,-32768); // COLLISION_NODE
	ActiveObject *object = nullptr; // COLLISION_OBJECT
	v3f old_speed;
	v3f new_speed;
	int plane = -1;
};

struct collisionMoveResult
{
	collisionMoveResult() = default;

	bool touching_ground = false;
	bool collides = false;
	bool standing_on_object = false;
	std::vector<CollisionInfo> collisions;
};

// Moves using a single iteration; speed should not exceed pos_max_d/dtime
collisionMoveResult collisionMoveSimple(Environment *env,IGameDef *gamedef,
		opos_t pos_max_d, const aabb3f &box_0,
		f32 stepheight, f32 dtime,
		v3opos_t *pos_f, v3f *speed_f,
		v3f accel_f, ActiveObject *self=NULL,
		bool collideWithObjects=true);

// Helper function:
// Checks for collision of a moving aabbox with a static aabbox
// Returns -1 if no collision, 0 if X collision, 1 if Y collision, 2 if Z collision
// dtime receives time until first collision, invalid if -1 is returned
CollisionAxis axisAlignedCollision(
		const aabb3o &staticbox, const aabb3o &movingbox,
		const v3f &speed, f32 *dtime);

// Helper function:
// Checks if moving the movingbox up by the given distance would hit a ceiling.
bool wouldCollideWithCeiling(
		const std::vector<aabb3o> &staticboxes,
		const aabb3o &movingbox,
		f32 y_increase, f32 d);
