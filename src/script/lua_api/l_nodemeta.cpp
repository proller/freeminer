/*
script/lua_api/l_nodemeta.cpp
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

#include "lua_api/l_nodemeta.h"
#include "lua_api/l_internal.h"
#include "lua_api/l_inventory.h"
#include "common/c_content.h"
#include "serverenvironment.h"
#include "map.h"
#include "mapblock.h"
#include "server.h"

/*
	NodeMetaRef
*/

IMetadata* NodeMetaRef::getmeta(bool auto_create)
{
	if (m_is_local)
		return m_local_meta;

	NodeMetadata *meta = m_env->getMap().getNodeMetadata(m_p);
	if (meta == NULL && auto_create) {
		meta = new NodeMetadata(m_env->getGameDef()->idef());
		if (!m_env->getMap().setNodeMetadata(m_p, meta)) {
			delete meta;
			return NULL;
		}
	}
	return meta;
}

void NodeMetaRef::clearMeta()
{
	SANITY_CHECK(!m_is_local);
	m_env->getMap().removeNodeMetadata(m_p);
}

void NodeMetaRef::reportMetadataChange(const std::string *name)
{
	SANITY_CHECK(!m_is_local);
	// Inform other things that the metadata has changed
	NodeMetadata *meta = dynamic_cast<NodeMetadata*>(getmeta(false));

	// If the metadata is now empty, get rid of it
	if (meta && meta->empty()) {
		clearMeta();
		meta = nullptr;
	}

	MapEditEvent event;
	event.type = MEET_BLOCK_NODE_METADATA_CHANGED;
	event.setPositionModified(m_p);
	event.is_private_change = name && meta && meta->isPrivate(*name);
	m_env->getMap().dispatchEvent(event);
}

// Exported functions

// get_inventory(self)
int NodeMetaRef::l_get_inventory(lua_State *L)
{
	MAP_LOCK_REQUIRED;

	NodeMetaRef *ref = checkObject<NodeMetaRef>(L, 1);
	ref->getmeta(true);  // try to ensure the metadata exists

	InventoryLocation loc;
	loc.setNodeMeta(ref->m_p);
	InvRef::create(L, loc);
	return 1;
}

// mark_as_private(self, <string> or {<string>, <string>, ...})
int NodeMetaRef::l_mark_as_private(lua_State *L)
{
	MAP_LOCK_REQUIRED;

	NodeMetaRef *ref = checkObject<NodeMetaRef>(L, 1);
	NodeMetadata *meta = dynamic_cast<NodeMetadata*>(ref->getmeta(true));
	assert(meta);

	if (lua_istable(L, 2)) {
		lua_pushnil(L);
		while (lua_next(L, 2) != 0) {
			// key at index -2 and value at index -1
			luaL_checktype(L, -1, LUA_TSTRING);
			meta->markPrivate(readParam<std::string>(L, -1), true);
			// removes value, keeps key for next iteration
			lua_pop(L, 1);
		}
	} else if (lua_isstring(L, 2)) {
		meta->markPrivate(readParam<std::string>(L, 2), true);
	}
	ref->reportMetadataChange();

	return 0;
}

void NodeMetaRef::handleToTable(lua_State *L, IMetadata *_meta)
{
	// fields
	MetaDataRef::handleToTable(L, _meta);

	NodeMetadata *meta = dynamic_cast<NodeMetadata*>(_meta);
	assert(meta);

	// inventory
	Inventory *inv = meta->getInventory();
	if (inv) {
		push_inventory_lists(L, *inv);
	} else {
		lua_newtable(L);
	}
	lua_setfield(L, -2, "inventory");
}

// from_table(self, table)
bool NodeMetaRef::handleFromTable(lua_State *L, int table, IMetadata *_meta)
{
	// fields
	if (!MetaDataRef::handleFromTable(L, table, _meta))
		return false;

	NodeMetadata *meta = dynamic_cast<NodeMetadata*>(_meta);
	assert(meta);

	// inventory
	Inventory *inv = meta->getInventory();
	lua_getfield(L, table, "inventory");
	if (lua_istable(L, -1)) {
		int inventorytable = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, inventorytable) != 0) {
			// key at index -2 and value at index -1
			std::string name = luaL_checkstring(L, -2);
			read_inventory_list(L, -1, inv, name.c_str(), getServer(L));
			lua_pop(L, 1); // Remove value, keep key for next iteration
		}
		lua_pop(L, 1);
	}

	return true;
}


NodeMetaRef::NodeMetaRef(v3pos_t p, ServerEnvironment *env):
	m_p(p),
	m_env(env)
{
}

NodeMetaRef::NodeMetaRef(IMetadata *meta):
	m_is_local(true),
	m_local_meta(meta)
{
}

// Creates an NodeMetaRef and leaves it on top of stack
// Not callable from Lua; all references are created on the C side.
void NodeMetaRef::create(lua_State *L, v3pos_t p, ServerEnvironment *env)
{
	NodeMetaRef *o = new NodeMetaRef(p, env);
	//infostream<<"NodeMetaRef::create: o="<<o<<std::endl;
	*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
	luaL_getmetatable(L, className);
	lua_setmetatable(L, -2);
}

// Client-sided version of the above
void NodeMetaRef::createClient(lua_State *L, IMetadata *meta)
{
	NodeMetaRef *o = new NodeMetaRef(meta);
	*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
	luaL_getmetatable(L, className);
	lua_setmetatable(L, -2);
}

const char NodeMetaRef::className[] = "NodeMetaRef";

void NodeMetaRef::Register(lua_State *L)
{
	registerMetadataClass(L, className, methodsServer);
}


const luaL_Reg NodeMetaRef::methodsServer[] = {
	luamethod(MetaDataRef, contains),
	luamethod(MetaDataRef, get),
	luamethod(MetaDataRef, get_string),
	luamethod(MetaDataRef, set_string),
	luamethod(MetaDataRef, get_int),
	luamethod(MetaDataRef, set_int),
	luamethod(MetaDataRef, get_float),
	luamethod(MetaDataRef, set_float),
	luamethod(MetaDataRef, get_keys),
	luamethod(MetaDataRef, to_table),
	luamethod(MetaDataRef, from_table),
	luamethod(NodeMetaRef, get_inventory),
	luamethod(NodeMetaRef, mark_as_private),
	luamethod(MetaDataRef, equals),
	{0,0}
};


void NodeMetaRef::RegisterClient(lua_State *L)
{
	registerMetadataClass(L, className, methodsClient);
}


const luaL_Reg NodeMetaRef::methodsClient[] = {
	luamethod(MetaDataRef, contains),
	luamethod(MetaDataRef, get),
	luamethod(MetaDataRef, get_string),
	luamethod(MetaDataRef, get_int),
	luamethod(MetaDataRef, get_float),
	luamethod(MetaDataRef, get_keys),
	luamethod(MetaDataRef, to_table),
	{0,0}
};
