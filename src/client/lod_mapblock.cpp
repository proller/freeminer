// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "lod_mapblock.h"
#include "irr_v3d.h"
#include "util/basic_macros.h"
#include "util/numeric.h"
#include "util/tracy_wrapper.h"
#include "mapblock_mesh.h"
#include "settings.h"
#include "nodedef.h"
#include "client/tile.h"
#include "client/meshgen/collector.h"
#include "client/renderingengine.h"
#include "client.h"
#include "porting.h"
#include "mesh.h"
#include "node_visuals.h"

#include "profiler.h"
#include "SMesh.h"
#include "util/directiontables.h"

static constexpr u16 quad_indices_02[] = {0, 1, 2, 2, 3, 0};
static const auto &quad_indices = quad_indices_02;

LodMeshGenerator::LodMeshGenerator(MeshMakeData *input, MeshCollector *output, const bool is_textureless, const u32 solid_shader_id):
	m_data(input),
	m_collector(output),
	m_nodedef(m_data->m_nodedef),
	m_blockpos_nodes(m_data->m_blockpos * MAP_BLOCKSIZE),
	m_is_textureless(is_textureless)
{
	m_solid_tile.layers[0].shader_id = solid_shader_id;
}

void LodMeshGenerator::drawMeshNode()
{
	u8 facedir = 0;
	scene::IMesh* mesh;
	int degrotate = 0;
	video::SColor base_color = encode_light(LightPair(getInteriorLight(m_cur_node.n, 0, m_nodedef)), m_cur_node.f->light_source);

	if (m_cur_node.f->param_type_2 == CPT2_FACEDIR ||
			m_cur_node.f->param_type_2 == CPT2_COLORED_FACEDIR ||
			m_cur_node.f->param_type_2 == CPT2_4DIR ||
			m_cur_node.f->param_type_2 == CPT2_COLORED_4DIR) {
		facedir = m_cur_node.n.getFaceDir(m_nodedef);
	} else if (m_cur_node.f->param_type_2 == CPT2_WALLMOUNTED ||
			m_cur_node.f->param_type_2 == CPT2_COLORED_WALLMOUNTED) {
		// Convert wallmounted to 6dfacedir.
		facedir = m_cur_node.n.getWallMounted(m_nodedef);
		facedir = wallmounted_to_facedir[facedir];
	} else if (m_cur_node.f->param_type_2 == CPT2_DEGROTATE ||
			m_cur_node.f->param_type_2 == CPT2_COLORED_DEGROTATE) {
		degrotate = m_cur_node.n.getDegRotate(m_nodedef);
	}

	if (m_cur_node.f->visuals->mesh_ptr) {
		// clone and rotate mesh
		mesh = cloneStaticMesh(m_cur_node.f->visuals->mesh_ptr);
		bool modified = true;
		if (facedir)
			rotateMeshBy6dFacedir(mesh, facedir);
		else if (degrotate)
			rotateMeshXZby(mesh, 1.5f * degrotate);
		else
			modified = false;
		if (modified) {
			recalculateBoundingBox(mesh);
		}
	} else {
		warningstream << "drawMeshNode(): missing mesh" << std::endl;
		return;
	}

	for (u32 j = 0; j < mesh->getMeshBufferCount(); j++) {
		// Only up to 6 tiles are supported
		const u32 tile_idx = mesh->getTextureSlot(j);
		TileSpec tile;
		getNodeTileN(m_cur_node.n, m_cur_node.p, MYMIN(tile_idx, 5), m_data, tile);

		scene::IMeshBuffer *buf = mesh->getMeshBuffer(j);
		video::S3DVertex *vertices = static_cast<video::S3DVertex*>(buf->getVertices());
		const u32 vertex_count = buf->getVertexCount();

		// Mesh is always private here. So the lighting is applied to each
		// vertex right here.
		const bool is_light_source = m_cur_node.f->light_source != 0;
		for (u32 k = 0; k < vertex_count; k++) {
			video::S3DVertex &vertex = vertices[k];
			video::SColor color = base_color;
			if (!is_light_source)
				applyFacesShading(color, vertex.Normal);
			vertex.Color = color;
			vertex.Pos.X += m_cur_node.p.X * BS;
			vertex.Pos.Y += m_cur_node.p.Y * BS;
			vertex.Pos.Z += m_cur_node.p.Z * BS;
		}
		m_collector->append(tile, vertices, vertex_count,
			buf->getIndices(), buf->getIndexCount());
	}
	std::ignore = mesh->drop();
}

void LodMeshGenerator::drawSolidNode()
{
	aabb3f box(v3f(-0.5 * BS), v3f(-0.5 * BS));

	box.MinEdge += oposToV3f(intToFloat(v3pos_t(
		m_cur_node.surface_p.X + 1 - m_node_width,
		m_cur_node.p.Y + 1 - m_node_width,
		m_cur_node.surface_p.Z + 1 - m_node_width
	), BS));
	box.MaxEdge += oposToV3f(intToFloat(m_cur_node.surface_p + 1, BS));


	const bool is_liquid = m_cur_node.f->drawtype == NDT_LIQUID || m_cur_node.f->drawtype == NDT_FLOWINGLIQUID;
	const bool uses_textureless_tile = m_is_textureless && !(is_liquid && g_settings->getBool("enable_waving_water"));

	core::vector2d<f32> uvs[4];
	core::vector3df vertices[4];
	video::S3DVertex irr_vertices[4];

	for (int face = 0; face < Direction_END; face++) {
		v3pos_t p_neigh_p = m_cur_node.p + tile_dirs[face] * m_node_width + m_blockpos_nodes;
		MapNode p_neigh_n = m_data->m_vmanip.getNodeNoExNoEmerge(p_neigh_p);

		auto surf_neigh_p = seekDownwards(m_cur_node.p + tile_dirs[face] * m_node_width);
		MapNode surf_neigh_n = m_data->m_vmanip.getNodeNoExNoEmerge(surf_neigh_p + m_blockpos_nodes);
		const content_t surf_neigh_t = surf_neigh_n.getContent();
		if (surf_neigh_t == CONTENT_IGNORE)
			continue;
		if (surf_neigh_p.Y == m_cur_node.surface_p.Y) {
			if (surf_neigh_t == m_cur_node.n.getContent() || surf_neigh_t == CONTENT_IGNORE)
				continue;
			if (surf_neigh_t != CONTENT_AIR) {
				const ContentFeatures &f2 = m_nodedef->get(surf_neigh_t);
				if (f2.visuals->solidness == 2 || (is_liquid && m_cur_node.f->sameLiquidRender(f2)))
					continue;
			}
		}

		// Farmesh / high-LOD neighbors can legitimately be unavailable here.
		// Falling back to face light against CONTENT_IGNORE makes large distant
		// surfaces render almost black, so use the node's own light instead.
		const u16 light = p_neigh_n.getContent() == CONTENT_IGNORE ?
				getInteriorLight(m_cur_node.n, 0, m_nodedef) :
				getFaceLight(m_cur_node.n, p_neigh_n, m_nodedef);
		video::SColor color = encode_light(light, m_cur_node.f->light_source);
		TileSpec tile;
		getNodeTileN(m_cur_node.n, m_blockpos_nodes, face, m_data, tile);

		if (uses_textureless_tile) {
			// When generating a mesh with no texture, we have to color the vertices instead of relying on the texture.
			video::SColor c2 = m_cur_node.f->visuals->average_colors[face];
			video::SColor c3 = tile.layers[0].color;
			color = video::SColor(
				color.getAlpha(),
				color.getRed() * c2.getRed() * c3.getRed() / 65025U,
				color.getGreen() * c2.getGreen() * c3.getGreen() / 65025U,
				color.getBlue() * c2.getBlue() * c3.getBlue() / 65025U);
		}

		switch (face) {
		case LEFT:
		case RIGHT:
		case DOWN:
		case BACK:
			uvs[0] = core::vector2d<f32>{0, static_cast<f32>(m_node_width)};
			uvs[1] = core::vector2d<f32>{0, 0};
			uvs[2] = core::vector2d<f32>{static_cast<f32>(m_node_width), 0};
			uvs[3] = core::vector2d<f32>{static_cast<f32>(m_node_width), static_cast<f32>(m_node_width)};
			break;
		default:
			uvs[0] = core::vector2d<f32>{0, static_cast<f32>(m_node_width)};
			uvs[1] = core::vector2d<f32>{0, 0};
			uvs[2] = core::vector2d<f32>{static_cast<f32>(m_node_width), 0};
			uvs[3] = core::vector2d<f32>{static_cast<f32>(m_node_width), static_cast<f32>(m_node_width)};
		}

		switch (face) {
		case UP:
			vertices[0] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			vertices[1] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[2] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[3] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			break;
		case DOWN:
			vertices[0] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			vertices[1] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			vertices[2] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			vertices[3] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			break;
		case LEFT:
			vertices[0] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			vertices[1] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			vertices[2] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[3] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			break;
		case RIGHT:
			vertices[0] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			vertices[1] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[2] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			vertices[3] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			break;
		case BACK:
			vertices[0] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			vertices[1] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[2] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MaxEdge.Z);
			vertices[3] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MaxEdge.Z);
			break;
		default:
			vertices[0] = core::vector3df(box.MinEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			vertices[1] = core::vector3df(box.MinEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			vertices[2] = core::vector3df(box.MaxEdge.X, box.MaxEdge.Y, box.MinEdge.Z);
			vertices[3] = core::vector3df(box.MaxEdge.X, box.MinEdge.Y, box.MinEdge.Z);
			break;
		}
		irr_vertices[0] = video::S3DVertex(vertices[0], s_normals[face], color, uvs[0]);
		irr_vertices[1] = video::S3DVertex(vertices[1], s_normals[face], color, uvs[1]);
		irr_vertices[2] = video::S3DVertex(vertices[2], s_normals[face], color, uvs[2]);
		irr_vertices[3] = video::S3DVertex(vertices[3], s_normals[face], color, uvs[3]);
		m_collector->append(uses_textureless_tile ? m_solid_tile : tile, irr_vertices, 4, quad_indices, 6);
	}
}

void LodMeshGenerator::drawNode()
{
	switch (m_cur_node.f->drawtype) {
	case NDT_MESH:
		if (!m_is_textureless)
			drawMeshNode();
		else
			drawSolidNode();
		break;
	case NDT_GLASSLIKE:
	case NDT_ALLFACES:
	case NDT_FLOWINGLIQUID:
	case NDT_LIQUID:
	case NDT_NODEBOX:
	case NDT_NORMAL:
		drawSolidNode();
		break;
	default:
		break;
	}
}

v3pos_t LodMeshGenerator::seekDownwards(v3pos_t from)
{
	for (u8 subtr = 0; subtr < m_node_width; subtr++) {
		// this assumes that we always have entire blocks emerged for meshgen
		auto p = from - v3pos_t(0, subtr, 0);
		MapNode n = m_data->m_vmanip.getNodeNoExNoEmerge(p + m_blockpos_nodes);
		const ContentFeatures *f = &m_nodedef->get(n);
		if (f->drawtype != NDT_AIRLIKE && f->drawtype != NDT_PLANTLIKE)
			return p;
	}
	return from;
}

void LodMeshGenerator::generate(const u8 lod)
{
	ZoneScoped;
	ScopeProfiler sp(g_profiler, "Client: Mesh Making LOD", SPT_AVG);

	m_node_width = m_data->fscale;

	const auto &data = m_data;
	auto &cur_node = m_cur_node;
	auto &blockpos_nodes = m_blockpos_nodes;
	const pos_t cell_count = data->side_length_data >> data->lod_step;
	for (pos_t z = 0; z < cell_count; ++z) {
		cur_node.p.Z = z * m_node_width + (m_node_width - 1);
		for (pos_t x = 0; x < cell_count; ++x) {
			cur_node.p.X = x * m_node_width + (m_node_width - 1);
			for (pos_t y = 0; y < cell_count; ++y) {
				cur_node.p.Y = y * m_node_width + (m_node_width - 1);
				cur_node.n = data->m_vmanip.getNodeRefAndVisible(
						blockpos_nodes + cur_node.p).first;
				cur_node.surface_p = seekDownwards(cur_node.p);
				cur_node.f = &m_nodedef->get(cur_node.n);
				drawNode();
			}
		}
	}
}
