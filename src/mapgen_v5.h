/*
Minetest
Copyright (C) 2010-2015 kwolekr, Ryan Kwolek <kwolekr@minetest.net>
Copyright (C) 2010-2015 paramat, Matt Gregory

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

#ifndef MAPGEN_V5_HEADER
#define MAPGEN_V5_HEADER

#include "mapgen.h"
#include "mapgen_indev.h"

#define MGV5_LARGE_CAVE_DEPTH -256

class BiomeManager;

extern FlagDesc flagdesc_mapgen_v5[];


struct MapgenV5Params : public MapgenSpecificParams {
	u32 spflags;
	float cave_width;
	NoiseParams np_filler_depth;
	NoiseParams np_factor;
	NoiseParams np_height;
	NoiseParams np_cave1;
	NoiseParams np_cave2;
	NoiseParams np_ground;

	s16 float_islands;
	NoiseParams np_float_islands1;
	NoiseParams np_float_islands2;
	NoiseParams np_float_islands3;
	NoiseParams np_layers;
	//NoiseParams np_cave_indev;
	Json::Value paramsj;

	MapgenV5Params();
	~MapgenV5Params() {}

	void readParams(Settings *settings);
	void writeParams(Settings *settings) const;
};


class MapgenV5 : public MapgenBasic, public Mapgen_features {
public:
	MapgenV5(int mapgenid, MapgenParams *params, EmergeManager *emerge);
	~MapgenV5();

	virtual MapgenType getType() const { return MAPGEN_V5; }

	virtual void makeChunk(BlockMakeData *data);
	int getSpawnLevelAtPoint(v2s16 p);
	int generateBaseTerrain();

private:
	Noise *noise_factor;
	Noise *noise_height;
	Noise *noise_ground;

	//freeminer:
	s16 float_islands;

};

#endif
