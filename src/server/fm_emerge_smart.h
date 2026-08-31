// Freeminer
// Native mapblock scheduler for /emerge_radius.

#pragma once

struct lua_State;

void fm_register_emerge_smart_api(lua_State *L, int top);
