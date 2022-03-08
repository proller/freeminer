/*
Minetest
Copyright (C) 2010-2014 celeron55, Perttu Ahola <celeron55@gmail.com>

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

#include "wieldmesh.h"
#include "settings.h"
#include "shader.h"
#include "inventory.h"
#include "client.h"
#include "itemdef.h"
#include "nodedef.h"
#include "mesh.h"
#include "content_mapblock.h"
#include "mapblock_mesh.h"
#include "client/meshgen/collector.h"
#include "client/tile.h"
#include "log.h"
#include "util/numeric.h"
#include <map>
#include <IMeshManipulator.h>
#include "client/renderingengine.h"

#include "map.h"
#include "clientmap.h"

#define WIELD_SCALE_FACTOR 30.0
#define WIELD_SCALE_FACTOR_EXTRUDED 40.0

#define MIN_EXTRUSION_MESH_RESOLUTION 16
#define MAX_EXTRUSION_MESH_RESOLUTION 512

static scene::IMesh *createExtrusionMesh(int resolution_x, int resolution_y)
{
	const f32 r = 0.5;

	scene::IMeshBuffer *buf = new scene::SMeshBuffer();
	video::SColor c(255,255,255,255);
	v3f scale(1.0, 1.0, 0.1);

	// Front and back
	{
		video::S3DVertex vertices[8] = {
			// z-
			video::S3DVertex(-r,+r,-r, 0,0,-1, c, 0,0),
			video::S3DVertex(+r,+r,-r, 0,0,-1, c, 1,0),
			video::S3DVertex(+r,-r,-r, 0,0,-1, c, 1,1),
			video::S3DVertex(-r,-r,-r, 0,0,-1, c, 0,1),
			// z+
			video::S3DVertex(-r,+r,+r, 0,0,+1, c, 0,0),
			video::S3DVertex(-r,-r,+r, 0,0,+1, c, 0,1),
			video::S3DVertex(+r,-r,+r, 0,0,+1, c, 1,1),
			video::S3DVertex(+r,+r,+r, 0,0,+1, c, 1,0),
		};
		u16 indices[12] = {0,1,2,2,3,0,4,5,6,6,7,4};
		buf->append(vertices, 8, indices, 12);
	}

	f32 pixelsize_x = 1 / (f32) resolution_x;
	f32 pixelsize_y = 1 / (f32) resolution_y;

	for (int i = 0; i < resolution_x; ++i) {
		f32 pixelpos_x = i * pixelsize_x - 0.5;
		f32 x0 = pixelpos_x;
		f32 x1 = pixelpos_x + pixelsize_x;
		f32 tex0 = (i + 0.1) * pixelsize_x;
		f32 tex1 = (i + 0.9) * pixelsize_x;
		video::S3DVertex vertices[8] = {
			// x-
			video::S3DVertex(x0,-r,-r, -1,0,0, c, tex0,1),
			video::S3DVertex(x0,-r,+r, -1,0,0, c, tex1,1),
			video::S3DVertex(x0,+r,+r, -1,0,0, c, tex1,0),
			video::S3DVertex(x0,+r,-r, -1,0,0, c, tex0,0),
			// x+
			video::S3DVertex(x1,-r,-r, +1,0,0, c, tex0,1),
			video::S3DVertex(x1,+r,-r, +1,0,0, c, tex0,0),
			video::S3DVertex(x1,+r,+r, +1,0,0, c, tex1,0),
			video::S3DVertex(x1,-r,+r, +1,0,0, c, tex1,1),
		};
		u16 indices[12] = {0,1,2,2,3,0,4,5,6,6,7,4};
		buf->append(vertices, 8, indices, 12);
	}
	for (int i = 0; i < resolution_y; ++i) {
		f32 pixelpos_y = i * pixelsize_y - 0.5;
		f32 y0 = -pixelpos_y - pixelsize_y;
		f32 y1 = -pixelpos_y;
		f32 tex0 = (i + 0.1) * pixelsize_y;
		f32 tex1 = (i + 0.9) * pixelsize_y;
		video::S3DVertex vertices[8] = {
			// y-
			video::S3DVertex(-r,y0,-r, 0,-1,0, c, 0,tex0),
			video::S3DVertex(+r,y0,-r, 0,-1,0, c, 1,tex0),
			video::S3DVertex(+r,y0,+r, 0,-1,0, c, 1,tex1),
			video::S3DVertex(-r,y0,+r, 0,-1,0, c, 0,tex1),
			// y+
			video::S3DVertex(-r,y1,-r, 0,+1,0, c, 0,tex0),
			video::S3DVertex(-r,y1,+r, 0,+1,0, c, 0,tex1),
			video::S3DVertex(+r,y1,+r, 0,+1,0, c, 1,tex1),
			video::S3DVertex(+r,y1,-r, 0,+1,0, c, 1,tex0),
		};
		u16 indices[12] = {0,1,2,2,3,0,4,5,6,6,7,4};
		buf->append(vertices, 8, indices, 12);
	}

	// Create mesh object
	scene::SMesh *mesh = new scene::SMesh();
	mesh->addMeshBuffer(buf);
	buf->drop();
	scaleMesh(mesh, scale);  // also recalculates bounding box
	return mesh;
}

/*
	Caches extrusion meshes so that only one of them per resolution
	is needed. Also caches one cube (for convenience).

	E.g. there is a single extrusion mesh that is used for all
	16x16 px images, another for all 256x256 px images, and so on.

	WARNING: Not thread safe. This should not be a problem since
	rendering related classes (such as WieldMeshSceneNode) will be
	used from the rendering thread only.
*/
class ExtrusionMeshCache: public IReferenceCounted
{
public:
	// Constructor
	ExtrusionMeshCache()
	{
		for (int resolution = MIN_EXTRUSION_MESH_RESOLUTION;
				resolution <= MAX_EXTRUSION_MESH_RESOLUTION;
				resolution *= 2) {
			m_extrusion_meshes[resolution] =
				createExtrusionMesh(resolution, resolution);
		}
		m_cube = createCubeMesh(v3f(1.0, 1.0, 1.0));
	}
	// Destructor
	virtual ~ExtrusionMeshCache()
	{
		for (auto &extrusion_meshe : m_extrusion_meshes) {
			extrusion_meshe.second->drop();
		}
		m_cube->drop();
	}
	// Get closest extrusion mesh for given image dimensions
	// Caller must drop the returned pointer
	scene::IMesh* create(core::dimension2d<u32> dim)
	{
		// handle non-power of two textures inefficiently without cache
		if (!is_power_of_two(dim.Width) || !is_power_of_two(dim.Height)) {
			return createExtrusionMesh(dim.Width, dim.Height);
		}

		int maxdim = MYMAX(dim.Width, dim.Height);

		std::map<int, scene::IMesh*>::iterator
			it = m_extrusion_meshes.lower_bound(maxdim);

		if (it == m_extrusion_meshes.end()) {
			// no viable resolution found; use largest one
			it = m_extrusion_meshes.find(MAX_EXTRUSION_MESH_RESOLUTION);
			sanity_check(it != m_extrusion_meshes.end());
		}

		scene::IMesh *mesh = it->second;
		mesh->grab();
		return mesh;
	}
	// Returns a 1x1x1 cube mesh with one meshbuffer (material) per face
	// Caller must drop the returned pointer
	scene::IMesh* createCube()
	{
		m_cube->grab();
		return m_cube;
	}

private:
	std::map<int, scene::IMesh*> m_extrusion_meshes;
	scene::IMesh *m_cube;
};

ExtrusionMeshCache *g_extrusion_mesh_cache = NULL;


WieldMeshSceneNode::WieldMeshSceneNode(scene::ISceneManager *mgr, s32 id, bool lighting):
	scene::ISceneNode(mgr->getRootSceneNode(), mgr, id),
	m_material_type(video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF),
	m_lighting(lighting)
{
	m_enable_shaders = g_settings->getBool("enable_shaders");
	m_anisotropic_filter = g_settings->getBool("anisotropic_filter");
	m_bilinear_filter = g_settings->getBool("bilinear_filter");
	m_trilinear_filter = g_settings->getBool("trilinear_filter");

	// If this is the first wield mesh scene node, create a cache
	// for extrusion meshes (and a cube mesh), otherwise reuse it
	if (!g_extrusion_mesh_cache)
		g_extrusion_mesh_cache = new ExtrusionMeshCache();
	else
		g_extrusion_mesh_cache->grab();

	// Disable bounding box culling for this scene node
	// since we won't calculate the bounding box.
	setAutomaticCulling(scene::EAC_OFF);

	// Create the child scene node
	scene::IMesh *dummymesh = g_extrusion_mesh_cache->createCube();
	m_meshnode = SceneManager->addMeshSceneNode(dummymesh, this, -1);
	m_meshnode->setReadOnlyMaterials(false);
	m_meshnode->setVisible(false);
	dummymesh->drop(); // m_meshnode grabbed it

	m_shadow = RenderingEngine::get_shadow_renderer();
}

WieldMeshSceneNode::~WieldMeshSceneNode()
{
	sanity_check(g_extrusion_mesh_cache);

	// Remove node from shadow casters. m_shadow might be an invalid pointer!
	if (auto shadow = RenderingEngine::get_shadow_renderer())
		shadow->removeNodeFromShadowList(m_meshnode);

	if (g_extrusion_mesh_cache->drop())
		g_extrusion_mesh_cache = nullptr;
}

void WieldMeshSceneNode::setCube(const ContentFeatures &f,
			v3f wield_scale)
{
	scene::IMesh *cubemesh = g_extrusion_mesh_cache->createCube();
	scene::SMesh *copy = cloneMesh(cubemesh);
	cubemesh->drop();
	postProcessNodeMesh(copy, f, false, true, &m_material_type, &m_colors, true);
	changeToMesh(copy);
	copy->drop();
	m_meshnode->setScale(wield_scale * WIELD_SCALE_FACTOR);
}

void WieldMeshSceneNode::setExtruded(const std::string &imagename,
	const std::string &overlay_name, v3f wield_scale, ITextureSource *tsrc,
	u8 num_frames)
{
	video::ITexture *texture = tsrc->getTexture(imagename);
	if (!texture) {
		changeToMesh(nullptr);
		return;
	}
	video::ITexture *overlay_texture =
		overlay_name.empty() ? NULL : tsrc->getTexture(overlay_name);

	core::dimension2d<u32> dim = texture->getSize();
	// Detect animation texture and pull off top frame instead of using entire thing
	if (num_frames > 1) {
		u32 frame_height = dim.Height / num_frames;
		dim = core::dimension2d<u32>(dim.Width, frame_height);
	}
	scene::IMesh *original = g_extrusion_mesh_cache->create(dim);
	scene::SMesh *mesh = cloneMesh(original);
	original->drop();
	//set texture
	mesh->getMeshBuffer(0)->getMaterial().setTexture(0,
		tsrc->getTexture(imagename));
	if (overlay_texture) {
		scene::IMeshBuffer *copy = cloneMeshBuffer(mesh->getMeshBuffer(0));
		copy->getMaterial().setTexture(0, overlay_texture);
		mesh->addMeshBuffer(copy);
		copy->drop();
	}
	changeToMesh(mesh);
	mesh->drop();

	m_meshnode->setScale(wield_scale * WIELD_SCALE_FACTOR_EXTRUDED);

	// Customize materials
	for (u32 layer = 0; layer < m_meshnode->getMaterialCount(); layer++) {
		video::SMaterial &material = m_meshnode->getMaterial(layer);
		material.TextureLayer[0].TextureWrapU = video::ETC_CLAMP_TO_EDGE;
		material.TextureLayer[0].TextureWrapV = video::ETC_CLAMP_TO_EDGE;
		material.MaterialType = m_material_type;
		material.MaterialTypeParam = 0.5f;
		material.setFlag(video::EMF_BACK_FACE_CULLING, true);
		// Enable bi/trilinear filtering only for high resolution textures
		if (dim.Width > 32) {
			material.setFlag(video::EMF_BILINEAR_FILTER, m_bilinear_filter);
			material.setFlag(video::EMF_TRILINEAR_FILTER, m_trilinear_filter);
		} else {
			material.setFlag(video::EMF_BILINEAR_FILTER, false);
			material.setFlag(video::EMF_TRILINEAR_FILTER, false);
		}
		material.setFlag(video::EMF_ANISOTROPIC_FILTER, m_anisotropic_filter);
		// mipmaps cause "thin black line" artifacts
		material.setFlag(video::EMF_USE_MIP_MAPS, false);
		if (m_enable_shaders) {
			material.setTexture(2, tsrc->getShaderFlagsTexture(false));
		}
	}
}

static scene::SMesh *createSpecialNodeMesh(Client *client, MapNode n,
	std::vector<ItemPartColor> *colors, const ContentFeatures &f)
{
	MeshMakeData mesh_make_data(client, false);
	MeshCollector collector;
	mesh_make_data.setSmoothLighting(false);
	MapblockMeshGenerator gen(&mesh_make_data, &collector,
		client->getSceneManager()->getMeshManipulator());

	if (n.getParam2()) {
		// keep it
	} else if (f.param_type_2 == CPT2_WALLMOUNTED ||
			f.param_type_2 == CPT2_COLORED_WALLMOUNTED) {
		if (f.drawtype == NDT_TORCHLIKE ||
				f.drawtype == NDT_SIGNLIKE ||
				f.drawtype == NDT_NODEBOX ||
				f.drawtype == NDT_MESH) {
			n.setParam2(4);
		}
	} else if (f.drawtype == NDT_SIGNLIKE || f.drawtype == NDT_TORCHLIKE) {
		n.setParam2(1);
	}
	gen.renderSingle(n.getContent(), n.getParam2());

	colors->clear();
	scene::SMesh *mesh = new scene::SMesh();
	for (auto &prebuffers : collector.prebuffers)
		for (PreMeshBuffer &p : prebuffers) {
			if (p.layer.material_flags & MATERIAL_FLAG_ANIMATION) {
				const FrameSpec &frame = (*p.layer.frames)[0];
				p.layer.texture = frame.texture;
				p.layer.normal_texture = frame.normal_texture;
			}
			for (video::S3DVertex &v : p.vertices) {
				v.Color.setAlpha(255);
			}
			scene::SMeshBuffer *buf = new scene::SMeshBuffer();
			buf->Material.setTexture(0, p.layer.texture);
			p.layer.applyMaterialOptions(buf->Material);
			mesh->addMeshBuffer(buf);
			buf->append(&p.vertices[0], p.vertices.size(),
					&p.indices[0], p.indices.size());
			buf->drop();
			colors->push_back(
				ItemPartColor(p.layer.has_color, p.layer.color));
		}
	return mesh;
}

void WieldMeshSceneNode::setItem(const ItemStack &item, Client *client, bool check_wield_image)
{
	ITextureSource *tsrc = client->getTextureSource();
	IItemDefManager *idef = client->getItemDefManager();
	IShaderSource *shdrsrc = client->getShaderSource();
	const NodeDefManager *ndef = client->getNodeDefManager();
	const ItemDefinition &def = item.getDefinition(idef);
	const ContentFeatures &f = ndef->get(def.name);
	content_t id = ndef->getId(def.name);

	scene::SMesh *mesh = nullptr;

	if (m_enable_shaders) {
		u32 shader_id = shdrsrc->getShader("object_shader", TILE_MATERIAL_BASIC, NDT_NORMAL);
		m_material_type = shdrsrc->getShaderInfo(shader_id).material;
	}

	// Color-related
	m_colors.clear();
	m_base_color = idef->getItemstackColor(item, client);

	// If wield_image needs to be checked and is defined, it overrides everything else
	if (!def.wield_image.empty() && check_wield_image) {
		setExtruded(def.wield_image, def.wield_overlay, def.wield_scale, tsrc,
			1);
		m_colors.emplace_back();
		// overlay is white, if present
		m_colors.emplace_back(true, video::SColor(0xFFFFFFFF));
		return;
	}

	// Handle nodes
	// See also CItemDefManager::createClientCached()
<<<<<<< HEAD:src/wieldmesh.cpp
	else if (def.type == ITEM_NODE) {
		if (f.mesh_ptr[0]) {
			// e.g. mesh nodes and nodeboxes
			changeToMesh(f.mesh_ptr[0]);
			// mesh_ptr[0] is pre-scaled by BS * f->visual_scale
			m_meshnode->setScale(
					def.wield_scale * WIELD_SCALE_FACTOR
					/ (BS * f.visual_scale));
		} else if (f.drawtype == NDT_AIRLIKE) {
			changeToMesh(NULL);
		} else if (f.drawtype == NDT_PLANTLIKE) {
			setExtruded(tsrc->getTextureName(f.tiles[0].texture_id), def.wield_scale, tsrc, f.tiles[0].animation_frame_count);
		} else if (f.drawtype == NDT_NORMAL || f.drawtype == NDT_ALLFACES) {
			setCube(f.tiles, def.wield_scale, tsrc);
		} else {
			Map map(gamedef);
			MapDrawControl map_draw_control;
			//// TODO: Change false in the following constructor args to
			//// appropriate value when shader is added for wield items (if applicable)
			MeshMakeData mesh_make_data(gamedef, false, false, map, map_draw_control);
			MapNode mesh_make_node(id, 255, 0);
			mesh_make_data.fillSingleNode(&mesh_make_node);
			MapBlockMesh mapblock_mesh(&mesh_make_data, v3s16(0, 0, 0));
			changeToMesh(mapblock_mesh.getMesh());
			translateMesh(m_meshnode->getMesh(), v3f(-BS, -BS, -BS));
			m_meshnode->setScale(
					def.wield_scale * WIELD_SCALE_FACTOR
					/ (BS * f.visual_scale));
=======
	if (def.type == ITEM_NODE) {
		bool cull_backface = f.needsBackfaceCulling();

		// Select rendering method
		switch (f.drawtype) {
		case NDT_AIRLIKE:
			setExtruded("no_texture_airlike.png", "",
				v3f(1.0, 1.0, 1.0), tsrc, 1);
			break;
		case NDT_SIGNLIKE:
		case NDT_TORCHLIKE:
		case NDT_RAILLIKE:
		case NDT_PLANTLIKE:
		case NDT_FLOWINGLIQUID: {
			v3f wscale = def.wield_scale;
			if (f.drawtype == NDT_FLOWINGLIQUID)
				wscale.Z *= 0.1f;
			setExtruded(tsrc->getTextureName(f.tiles[0].layers[0].texture_id),
				tsrc->getTextureName(f.tiles[0].layers[1].texture_id),
				wscale, tsrc,
				f.tiles[0].layers[0].animation_frame_count);
			// Add color
			const TileLayer &l0 = f.tiles[0].layers[0];
			m_colors.emplace_back(l0.has_color, l0.color);
			const TileLayer &l1 = f.tiles[0].layers[1];
			m_colors.emplace_back(l1.has_color, l1.color);
			break;
>>>>>>> 5.5.0:src/client/wieldmesh.cpp
		}
		case NDT_PLANTLIKE_ROOTED: {
			setExtruded(tsrc->getTextureName(f.special_tiles[0].layers[0].texture_id),
				"", def.wield_scale, tsrc,
				f.special_tiles[0].layers[0].animation_frame_count);
			// Add color
			const TileLayer &l0 = f.special_tiles[0].layers[0];
			m_colors.emplace_back(l0.has_color, l0.color);
			break;
		}
		case NDT_NORMAL:
		case NDT_ALLFACES:
		case NDT_LIQUID:
			setCube(f, def.wield_scale);
			break;
		default: {
			// Render non-trivial drawtypes like the actual node
			MapNode n(id);
			n.setParam2(def.place_param2);

			mesh = createSpecialNodeMesh(client, n, &m_colors, f);
			changeToMesh(mesh);
			mesh->drop();
			m_meshnode->setScale(
				def.wield_scale * WIELD_SCALE_FACTOR
				/ (BS * f.visual_scale));
			break;
		}
		}

		u32 material_count = m_meshnode->getMaterialCount();
		for (u32 i = 0; i < material_count; ++i) {
			video::SMaterial &material = m_meshnode->getMaterial(i);
			material.MaterialType = m_material_type;
			material.MaterialTypeParam = 0.5f;
			material.setFlag(video::EMF_BACK_FACE_CULLING, cull_backface);
			material.setFlag(video::EMF_BILINEAR_FILTER, m_bilinear_filter);
			material.setFlag(video::EMF_TRILINEAR_FILTER, m_trilinear_filter);
		}
		return;
	} else {
		if (!def.inventory_image.empty()) {
			setExtruded(def.inventory_image, def.inventory_overlay, def.wield_scale,
				tsrc, 1);
		} else {
			setExtruded("no_texture.png", "", def.wield_scale, tsrc, 1);
		}

		m_colors.emplace_back();
		// overlay is white, if present
		m_colors.emplace_back(true, video::SColor(0xFFFFFFFF));
		return;
	}

	// no wield mesh found
	changeToMesh(nullptr);
}

void WieldMeshSceneNode::setColor(video::SColor c)
{
	assert(!m_lighting);
	scene::IMesh *mesh = m_meshnode->getMesh();
	if (!mesh)
		return;

	u8 red = c.getRed();
	u8 green = c.getGreen();
	u8 blue = c.getBlue();
	u32 mc = mesh->getMeshBufferCount();
	for (u32 j = 0; j < mc; j++) {
		video::SColor bc(m_base_color);
		if ((m_colors.size() > j) && (m_colors[j].override_base))
			bc = m_colors[j].color;
		video::SColor buffercolor(255,
			bc.getRed() * red / 255,
			bc.getGreen() * green / 255,
			bc.getBlue() * blue / 255);
		scene::IMeshBuffer *buf = mesh->getMeshBuffer(j);

		if (m_enable_shaders)
			setMeshBufferColor(buf, buffercolor);
		else
			colorizeMeshBuffer(buf, &buffercolor);
	}
}

void WieldMeshSceneNode::setNodeLightColor(video::SColor color)
{
	if (!m_meshnode)
		return;

	if (m_enable_shaders) {
		for (u32 i = 0; i < m_meshnode->getMaterialCount(); ++i) {
			video::SMaterial &material = m_meshnode->getMaterial(i);
			material.EmissiveColor = color;
		}
	}

	setColor(color);
}

void WieldMeshSceneNode::render()
{
	// note: if this method is changed to actually do something,
	// you probably should implement OnRegisterSceneNode as well
}

void WieldMeshSceneNode::changeToMesh(scene::IMesh *mesh)
{
	if (!mesh) {
		scene::IMesh *dummymesh = g_extrusion_mesh_cache->createCube();
		m_meshnode->setVisible(false);
		m_meshnode->setMesh(dummymesh);
		dummymesh->drop();  // m_meshnode grabbed it
	} else {
		m_meshnode->setMesh(mesh);
	}

	m_meshnode->setMaterialFlag(video::EMF_LIGHTING, m_lighting);
	// need to normalize normals when lighting is enabled (because of setScale())
	m_meshnode->setMaterialFlag(video::EMF_NORMALIZE_NORMALS, m_lighting);
	m_meshnode->setVisible(true);

	// Add mesh to shadow caster
	if (m_shadow)
		m_shadow->addNodeToShadowList(m_meshnode);
}

void getItemMesh(Client *client, const ItemStack &item, ItemMesh *result)
{
	ITextureSource *tsrc = client->getTextureSource();
	IItemDefManager *idef = client->getItemDefManager();
	const NodeDefManager *ndef = client->getNodeDefManager();
	const ItemDefinition &def = item.getDefinition(idef);
	const ContentFeatures &f = ndef->get(def.name);
	content_t id = ndef->getId(def.name);

	FATAL_ERROR_IF(!g_extrusion_mesh_cache, "Extrusion mesh cache is not yet initialized");

	scene::SMesh *mesh = nullptr;

	// Shading is on by default
	result->needs_shading = true;

	bool cull_backface = f.needsBackfaceCulling();

	// If inventory_image is defined, it overrides everything else
	if (!def.inventory_image.empty()) {
		mesh = getExtrudedMesh(tsrc, def.inventory_image,
			def.inventory_overlay);
		result->buffer_colors.emplace_back();
		// overlay is white, if present
		result->buffer_colors.emplace_back(true, video::SColor(0xFFFFFFFF));
		// Items with inventory images do not need shading
		result->needs_shading = false;
	} else if (def.type == ITEM_NODE && f.drawtype == NDT_AIRLIKE) {
		// Fallback image for airlike node
		mesh = getExtrudedMesh(tsrc, "no_texture_airlike.png",
			def.inventory_overlay);
		result->needs_shading = false;
	} else if (def.type == ITEM_NODE) {
<<<<<<< HEAD:src/wieldmesh.cpp
		if (f.mesh_ptr[0]) {
			mesh = cloneMesh(f.mesh_ptr[0]);
			scaleMesh(mesh, v3f(0.12, 0.12, 0.12));
			setMeshColor(mesh, video::SColor (255, 255, 255, 255));
		} else if (f.drawtype == NDT_PLANTLIKE) {
			mesh = getExtrudedMesh(tsrc,
				tsrc->getTextureName(f.tiles[0].texture_id));
			return mesh;
		} else if (f.drawtype == NDT_NORMAL || f.drawtype == NDT_ALLFACES
			|| f.drawtype == NDT_LIQUID || f.drawtype == NDT_FLOWINGLIQUID) {
			mesh = cloneMesh(g_extrusion_mesh_cache->createCube());
			scaleMesh(mesh, v3f(1.2, 1.2, 1.2));
		} else {

//fm:
// /*
				Map map(gamedef);
				MapDrawControl map_draw_control;
				MeshMakeData mesh_make_data(gamedef, false, false, map, map_draw_control);
				v3POS bp = v3POS(32000, 32000, 32000-id);
				auto block = map.createBlankBlockNoInsert(bp);
				auto air_node = MapNode(CONTENT_AIR, LIGHT_MAX);
				for(s16 z0=0; z0<=2; ++z0)
				for(s16 y0=0; y0<=2; ++y0)
				for(s16 x0=0; x0<=2; ++x0) {
					v3s16 p(x0,y0,z0);
					block->setNode(p, air_node);
				}
				u8 param1 = 0;
				u8 param2 = 0;
				if (f.param_type_2 == CPT2_WALLMOUNTED)
					param2 = 1;
				MapNode mesh_make_node(id, param1, param2);
				mesh_make_data.fillSingleNode(&mesh_make_node, bp);
				block->setNode(v3s16(1,1,1), mesh_make_node);
				map.insertBlock(block);
				MapBlockMesh mapblock_mesh(&mesh_make_data, bp*MAP_BLOCKSIZE);
// */
//==

/*MT
			MeshMakeData mesh_make_data(gamedef, false);
			MapNode mesh_make_node(id, 255, 0);
			mesh_make_data.fillSingleNode(&mesh_make_node);
			MapBlockMesh mapblock_mesh(&mesh_make_data, v3s16(0, 0, 0));
*/
			mesh = cloneMesh(mapblock_mesh.getMesh());
			translateMesh(mesh, v3f(-BS, -BS, -BS));
			scaleMesh(mesh, v3f(0.12, 0.12, 0.12));
			rotateMeshXZby(mesh, -45);
			rotateMeshYZby(mesh, -30);

			u32 mc = mesh->getMeshBufferCount();
			for (u32 i = 0; i < mc; ++i) {
				video::SMaterial &material1 =
					mesh->getMeshBuffer(i)->getMaterial();
				video::SMaterial &material2 =
					mapblock_mesh.getMesh()->getMeshBuffer(i)->getMaterial();
				material1.setTexture(0, material2.getTexture(0));
				material1.setTexture(1, material2.getTexture(1));
				material1.setTexture(2, material2.getTexture(2));
				material1.setTexture(3, material2.getTexture(3));
				material1.MaterialType = material2.MaterialType;
			}
			return mesh;
=======
		switch (f.drawtype) {
		case NDT_NORMAL:
		case NDT_ALLFACES:
		case NDT_LIQUID:
		case NDT_FLOWINGLIQUID: {
			scene::IMesh *cube = g_extrusion_mesh_cache->createCube();
			mesh = cloneMesh(cube);
			cube->drop();
			if (f.drawtype == NDT_FLOWINGLIQUID) {
				scaleMesh(mesh, v3f(1.2, 0.03, 1.2));
				translateMesh(mesh, v3f(0, -0.57, 0));
			} else
				scaleMesh(mesh, v3f(1.2, 1.2, 1.2));
			// add overlays
			postProcessNodeMesh(mesh, f, false, false, nullptr,
				&result->buffer_colors, true);
			if (f.drawtype == NDT_ALLFACES)
				scaleMesh(mesh, v3f(f.visual_scale));
			break;
>>>>>>> 5.5.0:src/client/wieldmesh.cpp
		}
		case NDT_PLANTLIKE: {
			mesh = getExtrudedMesh(tsrc,
				tsrc->getTextureName(f.tiles[0].layers[0].texture_id),
				tsrc->getTextureName(f.tiles[0].layers[1].texture_id));
			// Add color
			const TileLayer &l0 = f.tiles[0].layers[0];
			result->buffer_colors.emplace_back(l0.has_color, l0.color);
			const TileLayer &l1 = f.tiles[0].layers[1];
			result->buffer_colors.emplace_back(l1.has_color, l1.color);
			break;
		}
		case NDT_PLANTLIKE_ROOTED: {
			mesh = getExtrudedMesh(tsrc,
				tsrc->getTextureName(f.special_tiles[0].layers[0].texture_id), "");
			// Add color
			const TileLayer &l0 = f.special_tiles[0].layers[0];
			result->buffer_colors.emplace_back(l0.has_color, l0.color);
			break;
		}
		default: {
			// Render non-trivial drawtypes like the actual node
			MapNode n(id);
			n.setParam2(def.place_param2);

			mesh = createSpecialNodeMesh(client, n, &result->buffer_colors, f);
			scaleMesh(mesh, v3f(0.12, 0.12, 0.12));
			break;
		}
		}

		u32 mc = mesh->getMeshBufferCount();
		for (u32 i = 0; i < mc; ++i) {
			scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);
			video::SMaterial &material = buf->getMaterial();
			material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
			material.MaterialTypeParam = 0.5f;
			material.setFlag(video::EMF_BILINEAR_FILTER, false);
			material.setFlag(video::EMF_TRILINEAR_FILTER, false);
			material.setFlag(video::EMF_BACK_FACE_CULLING, cull_backface);
			material.setFlag(video::EMF_LIGHTING, false);
		}

		rotateMeshXZby(mesh, -45);
		rotateMeshYZby(mesh, -30);
	}
	result->mesh = mesh;
}



scene::SMesh *getExtrudedMesh(ITextureSource *tsrc,
	const std::string &imagename, const std::string &overlay_name)
{
	// check textures
	video::ITexture *texture = tsrc->getTextureForMesh(imagename);
	if (!texture) {
		return NULL;
	}
	video::ITexture *overlay_texture =
		(overlay_name.empty()) ? NULL : tsrc->getTexture(overlay_name);

	// get mesh
	core::dimension2d<u32> dim = texture->getSize();
	scene::IMesh *original = g_extrusion_mesh_cache->create(dim);
	scene::SMesh *mesh = cloneMesh(original);
	original->drop();

	//set texture
	mesh->getMeshBuffer(0)->getMaterial().setTexture(0,
		tsrc->getTexture(imagename));
	if (overlay_texture) {
		scene::IMeshBuffer *copy = cloneMeshBuffer(mesh->getMeshBuffer(0));
		copy->getMaterial().setTexture(0, overlay_texture);
		mesh->addMeshBuffer(copy);
		copy->drop();
	}
	// Customize materials
	for (u32 layer = 0; layer < mesh->getMeshBufferCount(); layer++) {
		video::SMaterial &material = mesh->getMeshBuffer(layer)->getMaterial();
		material.TextureLayer[0].TextureWrapU = video::ETC_CLAMP_TO_EDGE;
		material.TextureLayer[0].TextureWrapV = video::ETC_CLAMP_TO_EDGE;
		material.setFlag(video::EMF_BILINEAR_FILTER, false);
		material.setFlag(video::EMF_TRILINEAR_FILTER, false);
		material.setFlag(video::EMF_BACK_FACE_CULLING, true);
		material.setFlag(video::EMF_LIGHTING, false);
		material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
		material.MaterialTypeParam = 0.5f;
	}
	scaleMesh(mesh, v3f(2.0, 2.0, 2.0));

	return mesh;
}

void postProcessNodeMesh(scene::SMesh *mesh, const ContentFeatures &f,
	bool use_shaders, bool set_material, const video::E_MATERIAL_TYPE *mattype,
	std::vector<ItemPartColor> *colors, bool apply_scale)
{
	u32 mc = mesh->getMeshBufferCount();
	// Allocate colors for existing buffers
	colors->clear();
	for (u32 i = 0; i < mc; ++i)
		colors->push_back(ItemPartColor());

	for (u32 i = 0; i < mc; ++i) {
		const TileSpec *tile = &(f.tiles[i]);
		scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);
		for (int layernum = 0; layernum < MAX_TILE_LAYERS; layernum++) {
			const TileLayer *layer = &tile->layers[layernum];
			if (layer->texture_id == 0)
				continue;
			if (layernum != 0) {
				scene::IMeshBuffer *copy = cloneMeshBuffer(buf);
				copy->getMaterial() = buf->getMaterial();
				mesh->addMeshBuffer(copy);
				copy->drop();
				buf = copy;
				colors->push_back(
					ItemPartColor(layer->has_color, layer->color));
			} else {
				(*colors)[i] = ItemPartColor(layer->has_color, layer->color);
			}
			video::SMaterial &material = buf->getMaterial();
			if (set_material)
				layer->applyMaterialOptions(material);
			if (mattype) {
				material.MaterialType = *mattype;
			}
			if (layer->animation_frame_count > 1) {
				const FrameSpec &animation_frame = (*layer->frames)[0];
				material.setTexture(0, animation_frame.texture);
			} else {
				material.setTexture(0, layer->texture);
			}
			if (use_shaders) {
				if (layer->normal_texture) {
					if (layer->animation_frame_count > 1) {
						const FrameSpec &animation_frame = (*layer->frames)[0];
						material.setTexture(1, animation_frame.normal_texture);
					} else
						material.setTexture(1, layer->normal_texture);
				}
				material.setTexture(2, layer->flags_texture);
			}
			if (apply_scale && tile->world_aligned) {
				u32 n = buf->getVertexCount();
				for (u32 k = 0; k != n; ++k)
					buf->getTCoords(k) /= layer->scale;
			}
		}
	}
}
