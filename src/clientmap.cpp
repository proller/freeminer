/*
clientmap.cpp
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

#include "clientmap.h"
#include "client.h"
#include "mapblock_mesh.h"
#include <IMaterialRenderer.h>
#include <matrix4.h>
#include "log_types.h"
#include "nodedef.h"
#include "mapblock.h"
#include "profiler.h"
#include "settings.h"
#include "camera.h"               // CameraModes
#include "util/mathconstants.h"
#include <algorithm>
#include <unordered_map>
#include <utility>

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

MapDrawControl::MapDrawControl():
		range_all(false),
		wanted_range(500),
		wanted_max_blocks(0),
		wanted_min_range(0),
		blocks_drawn(0),
		blocks_would_have_drawn(0),
		farthest_drawn(0)
		,
		farmesh(0),
		farmesh_step(1),
		fps(30),
		fps_avg(30),
		fps_wanted(30),
		drawtime_avg(30),
		fov_add(0)
		,camera_fov_blocks(0)
		//,block_overflow(false)
	{
		farmesh = g_settings->getS32("farmesh");
		farmesh_step = g_settings->getS32("farmesh_step");
		fov = g_settings->getFloat("fov");
	}

ClientMap::ClientMap(
		Client *client,
		IGameDef *gamedef,
		MapDrawControl &control,
		scene::ISceneNode* parent,
		scene::ISceneManager* mgr,
		s32 id
):
	Map(gamedef),
	scene::ISceneNode(parent, mgr, id),
	m_client(client),
	m_control(control),
	m_camera_position(0,0,0),
	m_camera_direction(0,0,1),
	m_camera_fov(M_PI)
	,m_drawlist(&m_drawlist_1),
	m_drawlist_current(0)
{
	m_drawlist_last = 0;
	m_box = core::aabbox3d<f32>(-BS*1000000,-BS*1000000,-BS*1000000,
			BS*1000000,BS*1000000,BS*1000000);

	/* TODO: Add a callback function so these can be updated when a setting
	 *       changes.  At this point in time it doesn't matter (e.g. /set
	 *       is documented to change server settings only)
	 *
	 * TODO: Local caching of settings is not optimal and should at some stage
	 *       be updated to use a global settings object for getting thse values
	 *       (as opposed to the this local caching). This can be addressed in
	 *       a later release.
	 */
	m_cache_trilinear_filter  = g_settings->getBool("trilinear_filter");
	m_cache_bilinear_filter   = g_settings->getBool("bilinear_filter");
	m_cache_anistropic_filter = g_settings->getBool("anisotropic_filter");

}

ClientMap::~ClientMap()
{
	/*MutexAutoLock lock(mesh_mutex);

	if(mesh != NULL)
	{
		mesh->drop();
		mesh = NULL;
	}*/
}

#if WTF
MapSector * ClientMap::emergeSector(v2s16 p2d)
{
	DSTACK(FUNCTION_NAME);
	// Check that it doesn't exist already
	try{
		return getSectorNoGenerate(p2d);
	}
	catch(InvalidPositionException &e)
	{
	}

	// Create a sector
	ClientMapSector *sector = new ClientMapSector(this, p2d, m_gamedef);

	{
		//MutexAutoLock lock(m_sector_mutex); // Bulk comment-out
		m_sectors[p2d] = sector;
	}

	return sector;
}
#endif

void ClientMap::OnRegisterSceneNode()
{
	if(IsVisible)
	{
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
		SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
	}

	ISceneNode::OnRegisterSceneNode();
}

static bool isOccluded(Map *map, v3s16 p0, v3s16 p1, float step, float stepfac,
		float start_off, float end_off, u32 needed_count, INodeDefManager *nodemgr,
		unordered_map_v3POS<bool> & occlude_cache)
{
	float d0 = (float)BS * p0.getDistanceFrom(p1);
	v3s16 u0 = p1 - p0;
	v3f uf = v3f(u0.X, u0.Y, u0.Z) * BS;
	uf.normalize();
	v3f p0f = v3f(p0.X, p0.Y, p0.Z) * BS;
	u32 count = 0;
	for(float s=start_off; s<d0+end_off; s+=step){
		v3f pf = p0f + uf * s;
		v3s16 p = floatToInt(pf, BS);
		bool is_transparent = false;
		bool cache = true;
		if (occlude_cache.count(p)) {
			cache = false;
			is_transparent = occlude_cache[p];
		} else {
		MapNode n = map->getNodeTry(p);
		if (n.getContent() == CONTENT_IGNORE) {
			cache = false;
		}
		const ContentFeatures &f = nodemgr->get(n);
		if(f.solidness == 0)
			is_transparent = (f.visual_solidness != 2);
		else
			is_transparent = (f.solidness != 2);
		}
		if (cache)
			occlude_cache[p] = is_transparent;
		if(!is_transparent){
			if(count == needed_count)
				return true;
			count++;
		}
		step *= stepfac;
	}
	return false;
}

void ClientMap::updateDrawList(video::IVideoDriver* driver, float dtime, unsigned int max_cycle_ms)
{
	ScopeProfiler sp(g_profiler, "CM::updateDrawList()", SPT_AVG);
	//g_profiler->add("CM::updateDrawList() count", 1);
	TimeTaker timer_step("ClientMap::updateDrawList");

	INodeDefManager *nodemgr = m_gamedef->ndef();

	if (!m_drawlist_last)
		m_drawlist_current = !m_drawlist_current;
	auto & drawlist = m_drawlist_current ? m_drawlist_1 : m_drawlist_0;

	if (!max_cycle_ms)
		max_cycle_ms = 300/getControl().fps_wanted;

	m_camera_mutex.lock();
	v3f camera_position = m_camera_position;
	f32 camera_fov = m_camera_fov;
	//v3s16 camera_offset = m_camera_offset;
	m_camera_mutex.unlock();

	// Use a higher fov to accomodate faster camera movements.
	// Blocks are cropped better when they are drawn.
	// Or maybe they aren't? Well whatever.
	camera_fov *= 1.2;

	//if(m_control.fps_avg > m_control.fps_wanted *0.7)
	//	camera_fov = 0;

	m_control.camera_fov_blocks = camera_fov;

	v3s16 cam_pos_nodes = floatToInt(camera_position, BS);
	v3s16 box_nodes_d = m_control.wanted_range * v3s16(1,1,1);
	v3s16 p_nodes_min = cam_pos_nodes - box_nodes_d;
	v3s16 p_nodes_max = cam_pos_nodes + box_nodes_d;
	// Take a fair amount as we will be dropping more out later
	// Umm... these additions are a bit strange but they are needed.
	v3s16 p_blocks_min(
			p_nodes_min.X / MAP_BLOCKSIZE - 3,
			p_nodes_min.Y / MAP_BLOCKSIZE - 3,
			p_nodes_min.Z / MAP_BLOCKSIZE - 3);
	v3s16 p_blocks_max(
			p_nodes_max.X / MAP_BLOCKSIZE + 1,
			p_nodes_max.Y / MAP_BLOCKSIZE + 1,
			p_nodes_max.Z / MAP_BLOCKSIZE + 1);

	// Number of blocks in rendering range
	u32 blocks_in_range = 0;
	// Number of blocks occlusion culled
	u32 blocks_occlusion_culled = 0;
	// Number of blocks in rendering range but don't have a mesh
	u32 blocks_in_range_without_mesh = 0;
	// Blocks that had mesh that would have been drawn according to
	// rendering range (if max blocks limit didn't kick in)
	u32 blocks_would_have_drawn = 0;
	// Blocks that were drawn and had a mesh
	u32 blocks_drawn = 0;
	// Blocks which had a corresponding meshbuffer for this pass
	//u32 blocks_had_pass_meshbuf = 0;
	// Blocks from which stuff was actually drawn
	//u32 blocks_without_stuff = 0;
	// Distance to farthest drawn block
	float farthest_drawn = 0;
	int m_mesh_queued = 0;

	bool free_move = g_settings->getBool("free_move");

	float range_max = 100000 * BS;
	if(m_control.range_all == false)
		range_max = m_control.wanted_range * BS;

	if (draw_nearest.empty()) {
		//ScopeProfiler sp(g_profiler, "CM::updateDrawList() make list", SPT_AVG);
		TimeTaker timer_step("ClientMap::updateDrawList make list");

		auto lock = m_blocks.try_lock_shared_rec();
		if (!lock->owns_lock())
			return;

		draw_nearest.clear();

		for(auto & ir : m_blocks) {
			auto bp = ir.first;

		if(m_control.range_all == false)
		{
			if(bp.X < p_blocks_min.X
			|| bp.X > p_blocks_max.X
			|| bp.Z > p_blocks_max.Z
			|| bp.Z < p_blocks_min.Z
			|| bp.Y < p_blocks_min.Y
			|| bp.Y > p_blocks_max.Y)
				continue;
		}

			int mesh_step = getFarmeshStep(m_control, getNodeBlockPos(cam_pos_nodes), bp);
			if (!getFarmeshGrid(bp, mesh_step))
				continue;

			v3s16 blockpos_nodes = bp * MAP_BLOCKSIZE;
			// Block center position
			v3f blockpos(
				((float)blockpos_nodes.X + MAP_BLOCKSIZE/2) * BS,
				((float)blockpos_nodes.Y + MAP_BLOCKSIZE/2) * BS,
				((float)blockpos_nodes.Z + MAP_BLOCKSIZE/2) * BS
			);

			f32 d = radius_box(blockpos, camera_position); //blockpos_relative.getLength();
			if (d> range_max)
				continue;
			int range = d / (MAP_BLOCKSIZE * BS);
			draw_nearest.emplace_back(std::make_pair(bp, range));
		}
	}

	const int maxq = 10000;

			// No occlusion culling when free_move is on and camera is
			// inside ground
			bool occlusion_culling_enabled = true;
			if(free_move){
				MapNode n = getNodeNoEx(cam_pos_nodes);
				if(n.getContent() == CONTENT_IGNORE ||
						nodemgr->get(n).solidness == 2)
					occlusion_culling_enabled = false;
			}

	u32 calls = 0, end_ms = porting::getTimeMs() + u32(max_cycle_ms);

	unordered_map_v3POS<bool> occlude_cache;

	while (!draw_nearest.empty()) {
		auto ir = draw_nearest.back();

		auto bp = ir.first;
		int range = ir.second;
		draw_nearest.pop_back();
		++calls;

		//auto block = getBlockNoCreateNoEx(bp);
		auto block = m_blocks.get(bp);
		if (!block)
			continue;

			int mesh_step = getFarmeshStep(m_control, getNodeBlockPos(cam_pos_nodes), bp);
			/*
				Compare block position to camera position, skip
				if not seen on display
			*/

			auto mesh = block->getMesh(mesh_step);
			if (mesh)
				mesh->updateCameraOffset(m_camera_offset);

			blocks_in_range++;

			//auto smesh_size = block->mesh_size;
			auto smesh_size = block->mesh.size();
			/*
				Ignore if mesh doesn't exist
			*/
			{
				if(!mesh) {
					blocks_in_range_without_mesh++;
					if (m_mesh_queued < maxq || range <= 2) {
						m_client->addUpdateMeshTask(bp, false);
						++m_mesh_queued;
					}
					continue;
				}
				if(mesh_step == mesh->step && block->getTimestamp() <= mesh->timestamp && !smesh_size) {
					blocks_in_range_without_mesh++;
					continue;
				}
			}

			/*
				Occlusion culling
			*/

			v3s16 cpn = bp * MAP_BLOCKSIZE;
			cpn += v3s16(MAP_BLOCKSIZE/2, MAP_BLOCKSIZE/2, MAP_BLOCKSIZE/2);

			float step = BS*1;
			float stepfac = 1.2;
			float startoff = BS*1;
			float endoff = -BS*MAP_BLOCKSIZE; //*1.42; //*1.42;
			v3s16 spn = cam_pos_nodes + v3s16(0,0,0);
			s16 bs2 = MAP_BLOCKSIZE/2 + 1;
			bs2 *= mesh->scale;
			u32 needed_count = 1;
			if( range > 1 && smesh_size &&
				occlusion_culling_enabled &&
				isOccluded(this, spn, cpn + v3s16(0,0,0),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(bs2,bs2,bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(bs2,bs2,-bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(bs2,-bs2,bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(bs2,-bs2,-bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(-bs2,bs2,bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(-bs2,bs2,-bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(-bs2,-bs2,bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache) &&
				isOccluded(this, spn, cpn + v3s16(-bs2,-bs2,-bs2),
					step, stepfac, startoff, endoff, needed_count, nodemgr, occlude_cache)
			)
			{
				//infostream<<"culled "<< spn<<" "<<cpn<<"step="<<mesh_step<<" total="<<blocks_occlusion_culled<<std::endl;
				blocks_occlusion_culled++;
				continue;
			}

			// This block is in range. Reset usage timer.
			block->resetUsageTimer();

/*
			// Limit block count in case of a sudden increase
			blocks_would_have_drawn++;

			if(blocks_drawn >= m_control.wanted_max_blocks
					&& m_control.range_all == false
					&& d > m_control.wanted_min_range * BS)
				continue;
*/

/*
			//if (m_control.farmesh && (!block->getMesh(mesh_step) || mesh_step != block->getMesh(mesh_step)->step)) { //&& !block->mesh->transparent
			if (!block->getMesh(mesh_step)) { //&& !block->mesh->transparent
if(mesh_step>=3)
infostream<<" making mesh for new step="<<mesh_step<<" bp="<<bp<<std::endl;
				blocks_in_range_without_mesh++;
				m_client->addUpdateMeshTask(block->getPos(), false, true, true); //,mesh_step == 1
				continue;
*/
/*
			if (mesh_step != mesh->step && (m_mesh_queued < maxq*1.2 || range <= 2)) {
				m_client->addUpdateMeshTask(bp);
				++m_mesh_queued;
				continue;
			}
*/
			//if (block->getTimestamp() > mesh->timestamp && (m_mesh_queued < maxq*1.5 || range <= 2)) {
			if (block->getTimestamp() > mesh->timestamp /*todo far null block check + (smesh_size ? 0 : range >= 1 ? 60 : 5) */ && (m_mesh_queued < maxq*1.5 || range <= 2)) {
				m_client->addUpdateMeshTaskWithEdge(bp, mesh_step);
				++m_mesh_queued;
			}

			if(!smesh_size)
				continue;

			mesh->incrementUsageTimer(dtime);

			// Add to set
			//block->refGrab();
			block->resetUsageTimer();
			drawlist.set(bp, block);

			blocks_drawn++;

			if(range * MAP_BLOCKSIZE > farthest_drawn)
				farthest_drawn = range * MAP_BLOCKSIZE;

			if (farthest_drawn > m_control.farthest_drawn)
				m_control.farthest_drawn = farthest_drawn;

		if (porting::getTimeMs() > end_ms) {
			break;
		}

	}
	m_drawlist_last = draw_nearest.size();

	//if (m_drawlist_last) infostream<<"breaked UDL "<<m_drawlist_last<<" collected="<<drawlist.size()<<" calls="<<calls<<" s="<<m_blocks.size()<<" maxms="<<max_cycle_ms<<" fw="<<getControl().fps_wanted<<" morems="<<porting::getTimeMs() - end_ms<< " meshq="<<m_mesh_queued<<" occache="<<occlude_cache.size()<<std::endl;

	if (m_drawlist_last)
		return;

	//for (auto & ir : *m_drawlist)
	//	ir.second->refDrop();

	auto m_drawlist_old = !m_drawlist_current ? &m_drawlist_1 : &m_drawlist_0;
	m_drawlist = m_drawlist_current ? &m_drawlist_1 : &m_drawlist_0;
	m_drawlist_old->clear();

	m_control.blocks_would_have_drawn = blocks_would_have_drawn;
	m_control.blocks_drawn = blocks_drawn;
	m_control.farthest_drawn = farthest_drawn;

	g_profiler->avg("CM: blocks total", m_blocks.size());
	g_profiler->avg("CM: blocks in range", blocks_in_range);
	g_profiler->avg("CM: blocks occlusion culled", blocks_occlusion_culled);
	if(blocks_in_range != 0)
		g_profiler->avg("CM: blocks in range without mesh (frac)",
				(float)blocks_in_range_without_mesh/blocks_in_range);
	g_profiler->avg("CM: blocks drawn", blocks_drawn);
	g_profiler->avg("CM: farthest drawn", farthest_drawn);
	g_profiler->avg("CM: wanted max blocks", m_control.wanted_max_blocks);
}

struct MeshBufList
{
	video::SMaterial m;
	std::vector<scene::IMeshBuffer*> bufs;
};

struct MeshBufListList
{
	std::vector<MeshBufList> lists;

	void clear()
	{
		lists.clear();
	}

	void add(scene::IMeshBuffer *buf)
	{
		for(std::vector<MeshBufList>::iterator i = lists.begin();
				i != lists.end(); ++i){
			MeshBufList &l = *i;
			video::SMaterial &m = buf->getMaterial();

			// comparing a full material is quite expensive so we don't do it if
			// not even first texture is equal
			if (l.m.TextureLayer[0].Texture != m.TextureLayer[0].Texture)
				continue;

			if (l.m == m) {
				l.bufs.push_back(buf);
				return;
			}
		}
		MeshBufList l;
		l.m = buf->getMaterial();
		l.bufs.push_back(buf);
		lists.push_back(l);
	}
};

void ClientMap::renderMap(video::IVideoDriver* driver, s32 pass)
{
	DSTACK(FUNCTION_NAME);

	ScopeProfiler sp(g_profiler, "CM::renderMap()", SPT_AVG);
	g_profiler->add("CM::renderMap() count", 1);

	bool is_transparent_pass = pass == scene::ESNRP_TRANSPARENT;

	std::string prefix;
	if(pass == scene::ESNRP_SOLID)
		prefix = "CM: solid: ";
	else
		prefix = "CM: transparent: ";

	//ScopeProfiler sp(g_profiler, "CM::renderMap() " + prefix, SPT_AVG);

	/*
		Get time for measuring timeout.

		Measuring time is very useful for long delays when the
		machine is swapping a lot.
	*/
	//int time1 = time(0);

	/*
		Get animation parameters
	*/
	float animation_time = m_client->getAnimationTime();
	int crack = m_client->getCrackLevel();
	u32 daynight_ratio = m_client->getEnv().getDayNightRatio();

	m_camera_mutex.lock();
	v3f camera_position = m_camera_position;
	f32 camera_fov = m_camera_fov * 1.1;
	m_camera_mutex.unlock();

	//if(m_control.fps_avg > m_control.fps_wanted *0.7)
	//	camera_fov = 0;

	/*
		Get all blocks and draw all visible ones
	*/

	v3s16 cam_pos_nodes = floatToInt(camera_position, BS);

	u32 vertex_count = 0;
	u32 meshbuffer_count = 0;

	// For limiting number of mesh animations per frame
	u32 mesh_animate_count = 0;
	u32 mesh_animate_count_far = 0;

	// Blocks that were drawn and had a mesh
	u32 blocks_drawn = 0;
	// Blocks which had a corresponding meshbuffer for this pass
	u32 blocks_had_pass_meshbuf = 0;
	// Blocks from which stuff was actually drawn
	u32 blocks_without_stuff = 0;

	/*
		Draw the selected MapBlocks
	*/

	{
	//ScopeProfiler sp(g_profiler, prefix+"drawing blocks", SPT_AVG);

	MeshBufListList drawbufs;
	std::vector<MapBlock::mesh_type> used_meshes; //keep shared_ptr
	auto drawlist = m_drawlist.load();
	auto lock = drawlist->lock_shared_rec();
	used_meshes.reserve(drawlist->size());
	//g_profiler->add("CM::renderMap()cnt"+ prefix, drawlist->size());
	for(auto & ir : *drawlist) {
		auto block = ir.second;

		int mesh_step = getFarmeshStep(m_control, getNodeBlockPos(cam_pos_nodes), block->getPos());
		// If the mesh of the block happened to get deleted, ignore it
		auto mapBlockMesh = block->getMesh(mesh_step);
		if (!mapBlockMesh)
			continue;

		float d = 0.0;
		if(isBlockInSight(block->getPos(), camera_position,
				m_camera_direction, camera_fov,
				100000*BS, &d, mapBlockMesh->scale) == false)
		{
			continue;
		}

		used_meshes.emplace_back(mapBlockMesh);

		// Mesh animation
		//if (mesh_step <= 1)
		{
			//MutexAutoLock lock(block->mesh_mutex);

			mapBlockMesh->updateCameraOffset(m_camera_offset);

			// Pretty random but this should work somewhat nicely
#if __ANDROID__
			bool faraway = d >= BS*16;
#else
			bool faraway = d >= BS*50;
#endif
			//bool faraway = d >= m_control.wanted_range * BS;
			if(mapBlockMesh->isAnimationForced() ||
					!faraway ||
					mesh_animate_count_far < (m_control.range_all ? 200 : 50))
			{
				bool animated = mapBlockMesh->animate(
						faraway,
						animation_time,
						crack,
						daynight_ratio);
				if(animated)
					mesh_animate_count++;
				if(animated && faraway)
					mesh_animate_count_far++;
			}
			else
			{
				mapBlockMesh->decreaseAnimationForceTimer();
			}
		}

		/*
			Get the meshbuffers of the block
		*/
		{
			//MutexAutoLock lock(block->mesh_mutex);

			auto *mesh = mapBlockMesh->getMesh();
			if (!mesh)
				continue;

			u32 c = mesh->getMeshBufferCount();
			for(u32 i=0; i<c; i++)
			{
				scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);

				buf->getMaterial().setFlag(video::EMF_TRILINEAR_FILTER, m_cache_trilinear_filter);
				buf->getMaterial().setFlag(video::EMF_BILINEAR_FILTER, m_cache_bilinear_filter);
				buf->getMaterial().setFlag(video::EMF_ANISOTROPIC_FILTER, m_cache_anistropic_filter);

				const video::SMaterial& material = buf->getMaterial();
				video::IMaterialRenderer* rnd =
						driver->getMaterialRenderer(material.MaterialType);
				bool transparent = (rnd && rnd->isTransparent());
				if(transparent == is_transparent_pass)
				{
					if(buf->getVertexCount() == 0)
						errorstream<<"Block ["<<analyze_block(block)
								<<"] contains an empty meshbuf"<<std::endl;
					drawbufs.add(buf);
				}
			}
		}
	}

	std::vector<MeshBufList> &lists = drawbufs.lists;

	//int timecheck_counter = 0;
	for(std::vector<MeshBufList>::iterator i = lists.begin();
			i != lists.end(); ++i) {
#if 0
		timecheck_counter++;
		if(timecheck_counter > 50) {
			timecheck_counter = 0;
			int time2 = time(0);
			if(time2 > time1 + 4) {
				infostream << "ClientMap::renderMap(): "
					"Rendering takes ages, returning."
					<< std::endl;
				return;
			}
		}
#endif

		MeshBufList &list = *i;

		driver->setMaterial(list.m);

		for(std::vector<scene::IMeshBuffer*>::iterator j = list.bufs.begin();
				j != list.bufs.end(); ++j) {
			scene::IMeshBuffer *buf = *j;
			driver->drawMeshBuffer(buf);
			vertex_count += buf->getVertexCount();
			meshbuffer_count++;
		}

	}
	} // ScopeProfiler

	// Log only on solid pass because values are the same
	if(pass == scene::ESNRP_SOLID){
		g_profiler->avg("CM: animated meshes", mesh_animate_count);
		g_profiler->avg("CM: animated meshes (far)", mesh_animate_count_far);
	}

	g_profiler->avg(prefix+"vertices drawn", vertex_count);
	if(blocks_had_pass_meshbuf != 0)
		g_profiler->avg(prefix+"meshbuffers per block",
				(float)meshbuffer_count / (float)blocks_had_pass_meshbuf);
	if(blocks_drawn != 0)
		g_profiler->avg(prefix+"empty blocks (frac)",
				(float)blocks_without_stuff / blocks_drawn);

	g_profiler->avg("CM: PrimitiveDrawn", driver->getPrimitiveCountDrawn());

	/*infostream<<"renderMap(): is_transparent_pass="<<is_transparent_pass
			<<", rendered "<<vertex_count<<" vertices."<<std::endl;*/
}

static bool getVisibleBrightness(Map *map, v3f p0, v3f dir, float step,
		float step_multiplier, float start_distance, float end_distance,
		INodeDefManager *ndef, u32 daylight_factor, float sunlight_min_d,
		int *result, bool *sunlight_seen)
{
	int brightness_sum = 0;
	int brightness_count = 0;
	float distance = start_distance;
	dir.normalize();
	v3f pf = p0;
	pf += dir * distance;
	int noncount = 0;
	bool nonlight_seen = false;
	bool allow_allowing_non_sunlight_propagates = false;
	bool allow_non_sunlight_propagates = false;
	// Check content nearly at camera position
	{
		v3s16 p = floatToInt(p0 /*+ dir * 3*BS*/, BS);
		MapNode n = map->getNodeNoEx(p);
		if(ndef->get(n).param_type == CPT_LIGHT &&
				!ndef->get(n).sunlight_propagates)
			allow_allowing_non_sunlight_propagates = true;
	}
	// If would start at CONTENT_IGNORE, start closer
	{
		v3s16 p = floatToInt(pf, BS);
		MapNode n = map->getNodeNoEx(p);
		if(n.getContent() == CONTENT_IGNORE){
			float newd = 2*BS;
			pf = p0 + dir * 2*newd;
			distance = newd;
			sunlight_min_d = 0;
		}
	}
	for(int i=0; distance < end_distance; i++){
		pf += dir * step;
		distance += step;
		step *= step_multiplier;

		v3s16 p = floatToInt(pf, BS);
		MapNode n = map->getNodeNoEx(p);
		if(allow_allowing_non_sunlight_propagates && i == 0 &&
				ndef->get(n).param_type == CPT_LIGHT &&
				!ndef->get(n).sunlight_propagates){
			allow_non_sunlight_propagates = true;
		}
		if(ndef->get(n).param_type != CPT_LIGHT ||
				(!ndef->get(n).sunlight_propagates &&
					!allow_non_sunlight_propagates)){
			nonlight_seen = true;
			noncount++;
			if(noncount >= 4)
				break;
			continue;
		}
		if(distance >= sunlight_min_d && *sunlight_seen == false
				&& nonlight_seen == false)
			if(n.getLight(LIGHTBANK_DAY, ndef) == LIGHT_SUN)
				*sunlight_seen = true;
		noncount = 0;
		brightness_sum += decode_light(n.getLightBlend(daylight_factor, ndef));
		brightness_count++;
	}
	*result = 0;
	if(brightness_count == 0)
		return false;
	*result = brightness_sum / brightness_count;
	/*std::cerr<<"Sampled "<<brightness_count<<" points; result="
			<<(*result)<<std::endl;*/
	return true;
}

int ClientMap::getBackgroundBrightness(float max_d, u32 daylight_factor,
		int oldvalue, bool *sunlight_seen_result)
{
	const bool debugprint = false;
	INodeDefManager *ndef = m_gamedef->ndef();
	static v3f z_directions[50] = {
		v3f(-100, 0, 0)
	};
	static f32 z_offsets[sizeof(z_directions)/sizeof(*z_directions)] = {
		-1000,
	};
	if(z_directions[0].X < -99){
		for(u32 i=0; i<sizeof(z_directions)/sizeof(*z_directions); i++){
			z_directions[i] = v3f(
				0.01 * myrand_range(-100, 100),
				1.0,
				0.01 * myrand_range(-100, 100)
			);
			z_offsets[i] = 0.01 * myrand_range(0,100);
		}
	}
	if(debugprint)
		std::cerr<<"In goes "<<PP(m_camera_direction)<<", out comes ";
	int sunlight_seen_count = 0;
	float sunlight_min_d = max_d*0.8;
	if(sunlight_min_d > 35*BS)
		sunlight_min_d = 35*BS;
	std::vector<int> values;
	for(u32 i=0; i<sizeof(z_directions)/sizeof(*z_directions); i++){
		v3f z_dir = z_directions[i];
		z_dir.normalize();
		core::CMatrix4<f32> a;
		a.buildRotateFromTo(v3f(0,1,0), z_dir);
		v3f dir = m_camera_direction;
		a.rotateVect(dir);
		int br = 0;
		float step = BS*1.5;
		if(max_d > 35*BS)
			step = max_d / 35 * 1.5;
		float off = step * z_offsets[i];
		bool sunlight_seen_now = false;
		bool ok = getVisibleBrightness(this, m_camera_position, dir,
				step, 1.0, max_d*0.6+off, max_d, ndef, daylight_factor,
				sunlight_min_d,
				&br, &sunlight_seen_now);
		if(sunlight_seen_now)
			sunlight_seen_count++;
		if(!ok)
			continue;
		values.push_back(br);
		// Don't try too much if being in the sun is clear
		if(sunlight_seen_count >= 20)
			break;
	}
	int brightness_sum = 0;
	int brightness_count = 0;
	std::sort(values.begin(), values.end());
	u32 num_values_to_use = values.size();
	if(num_values_to_use >= 10)
		num_values_to_use -= num_values_to_use/2;
	else if(num_values_to_use >= 7)
		num_values_to_use -= num_values_to_use/3;
	u32 first_value_i = (values.size() - num_values_to_use) / 2;
	if(debugprint){
		for(u32 i=0; i < first_value_i; i++)
			std::cerr<<values[i]<<" ";
		std::cerr<<"[";
	}
	for(u32 i=first_value_i; i < first_value_i+num_values_to_use; i++){
		if(debugprint)
			std::cerr<<values[i]<<" ";
		brightness_sum += values[i];
		brightness_count++;
	}
	if(debugprint){
		std::cerr<<"]";
		for(u32 i=first_value_i+num_values_to_use; i < values.size(); i++)
			std::cerr<<values[i]<<" ";
	}
	int ret = 0;
	if(brightness_count == 0){
		MapNode n = getNodeNoEx(floatToInt(m_camera_position, BS));
		if(ndef->get(n).param_type == CPT_LIGHT){
			ret = decode_light(n.getLightBlend(daylight_factor, ndef));
		} else {
			ret = oldvalue;
		}
	} else {
		/*float pre = (float)brightness_sum / (float)brightness_count;
		float tmp = pre;
		const float d = 0.2;
		pre *= 1.0 + d*2;
		pre -= tmp * d;
		int preint = pre;
		ret = MYMAX(0, MYMIN(255, preint));*/
		ret = brightness_sum / brightness_count;
	}
	if(debugprint)
		std::cerr<<"Result: "<<ret<<" sunlight_seen_count="
				<<sunlight_seen_count<<std::endl;
	*sunlight_seen_result = (sunlight_seen_count > 0);
	return ret;
}

void ClientMap::renderPostFx(CameraMode cam_mode)
{
	INodeDefManager *nodemgr = m_gamedef->ndef();

	// Sadly ISceneManager has no "post effects" render pass, in that case we
	// could just register for that and handle it in renderMap().

	m_camera_mutex.lock();
	v3f camera_position = m_camera_position;
	m_camera_mutex.unlock();

	MapNode n = getNodeNoEx(floatToInt(camera_position, BS));

	// - If the player is in a solid node, make everything black.
	// - If the player is in liquid, draw a semi-transparent overlay.
	// - Do not if player is in third person mode
	const ContentFeatures& features = nodemgr->get(n);
	video::SColor post_effect_color = features.post_effect_color;
	if(features.solidness == 2 && !(g_settings->getBool("noclip") &&
			m_gamedef->checkLocalPrivilege("noclip")) &&
			cam_mode == CAMERA_MODE_FIRST)
	{
		post_effect_color = video::SColor(255, 0, 0, 0);
	}
	if (post_effect_color.getAlpha() != 0)
	{
		// Draw a full-screen rectangle
		video::IVideoDriver* driver = SceneManager->getVideoDriver();
		v2u32 ss = driver->getScreenSize();
		core::rect<s32> rect(0,0, ss.X, ss.Y);
		driver->draw2DRectangle(post_effect_color, rect);
	}
}

void ClientMap::renderBlockBoundaries(const std::map<v3POS, MapBlock*> & blocks)
{
	video::IVideoDriver* driver = SceneManager->getVideoDriver();
	video::SMaterial mat;
	mat.Lighting = false;
	mat.ZWriteEnable = false;

	core::aabbox3d<f32> bound;
	//auto & blocks = *m_drawlist;
	const v3f inset(BS/2);
	const v3f blocksize(MAP_BLOCKSIZE);

	for (int pass = 0; pass < 2; ++pass) {
		video::SColor color_offset(0, 0, 0, 0);
		if (pass == 0) {
			mat.Thickness = 1;
			mat.ZBuffer = video::ECFN_ALWAYS;
			color_offset.setGreen(64);
		} else {
			mat.Thickness = 3;
			mat.ZBuffer = video::ECFN_LESSEQUAL;
		}
		driver->setMaterial(mat);

		for(auto i = blocks.begin(); i != blocks.end(); ++i) {
			video::SColor color(255, 0, 0, 0);
			if (i->second) {
				color.setBlue(255);
			} else {
				color.setRed(255);
				color.setGreen(128);
			}

			bound.MinEdge = intToFloat(i->first, BS)*blocksize
				+ inset
				- v3f(BS)*0.5
				- intToFloat(m_camera_offset, BS);
			bound.MaxEdge = bound.MinEdge
				+ blocksize*BS
				- inset
				- inset;
			color = color + color_offset;

			driver->draw3DBox(bound, color);
		}
	}
}


void ClientMap::PrintInfo(std::ostream &out)
{
	out<<"ClientMap: ";
}


