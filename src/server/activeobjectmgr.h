/*
Minetest
Copyright (C) 2010-2018 nerzhul, Loic BLOT <loic.blot@unix-experience.fr>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <functional>
#include <vector>
#include "../activeobjectmgr.h"
#include "serveractiveobject.h"

namespace server
{
class ActiveObjectMgr : public ::ActiveObjectMgr<ServerActiveObject>
{

    std::vector<ServerActiveObjectPtr> m_objects_to_delete, m_objects_to_delete_2;
	std::vector<u16> objects_to_remove;
public:
	~ActiveObjectMgr();


public:
	void deferDelete(ServerActiveObjectPtr obj);

	void clear(const std::function<bool(const ServerActiveObjectPtr&, u16)> &cb);
	void step(float dtime,
			const std::function<void(const ServerActiveObjectPtr&)> &f) override;
	bool registerObject(ServerActiveObject *obj) override;
	void removeObject(u16 id) override;

	void getObjectsInsideRadius(const v3opos_t &pos, float radius,
			std::vector<ServerActiveObjectPtr> &result,
			const std::function<bool(ServerActiveObjectPtr &obj)> &include_obj_cb);
	void getObjectsInArea(const aabb3o &box,
			std::vector<ServerActiveObjectPtr> &result,
			const std::function<bool(ServerActiveObjectPtr &obj)> &include_obj_cb);

	void getAddedActiveObjectsAroundPos(const v3opos_t &player_pos, f32 radius,
			f32 player_radius, std::set<u16> &current_objects,
			std::queue<u16> &added_objects);
};
} // namespace server
