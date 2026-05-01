// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "nodedef.h"
#include <bitset>

#include "tile.h"

struct MeshMakeData;
struct MeshCollector;

class LodMeshGenerator
{
public:
	LodMeshGenerator(MeshMakeData *input, MeshCollector *output, bool is_textureless, u32 solid_shader_id);
	void generate(u8 lod);

private:
	static constexpr core::vector3df s_normals[6] = {
		core::vector3df(0, 1, 0), core::vector3df(0, -1, 0),
		core::vector3df(1, 0, 0), core::vector3df(-1, 0, 0),
		core::vector3df(0, 0, 1), core::vector3df(0, 0, -1)
	};

	static constexpr v3pos_t tile_dirs[6] = {
		{0, 1, 0}, {0, -1, 0},
		{1, 0, 0}, {-1, 0, 0},
		{0, 0, 1}, {0, 0, -1}
	};

	MeshMakeData *const m_data;
	MeshCollector *const m_collector;
	const NodeDefManager *const m_nodedef;
	const v3pos_t m_blockpos_nodes;
	const bool m_is_textureless;

	pos_t m_node_width;

	TileSpec m_solid_tile = [] {
		TileSpec tile;
		TileLayer layer;
		layer.shader_id = -1;
		layer.material_type = TILE_MATERIAL_PLAIN;
		tile.layers[0] = layer;
		tile.layers[0].color = {255, 255, 255, 255};
		return tile;
	}();

	struct {
		v3pos_t pf;
		v3pos_t pr;

		v3pos_t p; // relative to blockpos_nodes
		v3pos_t surface_p;
		MapNode n;
		const ContentFeatures *f;
	} m_cur_node;

	v3pos_t seekDownwards(v3pos_t);
	void drawMeshNode();
	void drawSolidNode();
	void drawNode();
};
