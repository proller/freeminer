
#include "fm_farmesh.h"
//#include "EMaterialFlags.h"
#include "EPrimitiveTypes.h"
#include "IMeshBuffer.h"
#include "client/client.h"
#include "client/clientmap.h"
//#include "client/mapblock_mesh.h"
#include "client/mapblock_mesh.h"
#include "constants.h"
#include "debug/iostream_debug_helpers.h"
#include "emerge.h"
#include "fm_nodecontainer.h"
#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "light.h"
#include "log.h"
#include "log_types.h"
#include "map.h"
//#include "mapblock.h"
#include "mapgen/mapgen.h"
#include "mapnode.h"
#include "nodedef.h"
#include "server.h"
#include "util/directiontables.h"
#include "util/numeric.h"
#include "util/timetaker.h"
#include "util/unordered_map_hash.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
//#include <memory>
//#include <ostream>
#include <memory>
#include <random>
#include <utility>

const v3f g_6dirsf[6] = {
		// +right, +top, +back
		v3f(0, 0, 1),  // back
		v3f(0, 1, 0),  // top
		v3f(1, 0, 0),  // right
		v3f(0, 0, -1), // front
		v3f(0, -1, 0), // bottom
		v3f(-1, 0, 0), // left
};

FarContainer::FarContainer(){};
MapNode air_node{CONTENT_AIR, LIGHT_SUN};
MapNode &FarContainer::getNodeRefUnsafe(const v3pos_t &p)
{
	if (m_mg->visible(p.X, p.Y, p.Z))
		return visible_node;
	if (p.Y < m_water_level)
		return water_node;
	return air_node;
};
MapNode FarContainer::getNodeNoExNoEmerge(const v3pos_t &p)
{
	return getNodeRefUnsafe(p);
};
MapNode FarContainer::getNodeNoEx(const v3pos_t &p)
{
	return getNodeRefUnsafe(p);
};

void FarMesh::makeFarBlock(const v3bpos_t &blockpos)
{
	//const auto blockpos = getNodeBlockPos(pos_int);
	const auto step = getFarmeshStep(m_client->getEnv().getClientMap().getControl(),
			getNodeBlockPos(m_camera_pos_aligned), blockpos);

	const auto blockpos_actual = getFarmeshActual(blockpos, step);
	//errorstream << " step="<<step << " blockpos="<<blockpos << " blockposa=" << blockpos_actual << std::endl;

	auto &far_blocks = m_client->getEnv().getClientMap().m_far_blocks;
	if (!far_blocks.contains(blockpos_actual)) {
		far_blocks.emplace(blockpos_actual,
				std::make_shared<MapBlock>(
						&m_client->getEnv().getClientMap(), blockpos, m_client));
	}
	const auto &block = far_blocks.at(blockpos_actual);
	if (!block->getFarMesh(step)) {
		//DUMP("mkfr", blockpos_actual, step, (long)&farcontainer);
		MeshMakeData mdat(m_client, false, 0, step, &farcontainer);
		mdat.block = block.get();
		mdat.m_blockpos = blockpos_actual;
		auto mbmsh = std::make_shared<MapBlockMesh>(&mdat, m_camera_offset);
		//mbmsh->getMesh();
		//DUMP(mbmsh->getMesh(0)->getMeshBufferCount(), mbmsh->getMesh()->getMeshBuffer(0)->getVertexCount(), mbmsh->getMesh(0)->getReferenceCount());

		//DUMP(block->getPos(), mbmsh->far_step, mbmsh->lod_step);
		block->setFarMesh(mbmsh);
	} else {
		//DUMP("already farmesh", step, blockpos_actual, block->getPos());
	}
}

void FarMesh::makeFarBlock6(const v3bpos_t &blockpos)
{
	for (const auto &dir : g_6dirs) {
		makeFarBlock(blockpos + dir);
	}
}

void FarMesh::makeFarBlocks(const v3bpos_t &blockpos)
{
	int radius = 20;
	int &dr = m_make_far_blocks_last;
	//int end_ms = os.clock() + tnt.time_max
	bool last = false;

	const int max_cycle_ms = 500;
	u32 end_ms = porting::getTimeMs() + max_cycle_ms;

	while (dr < radius) {
		if (porting::getTimeMs() > end_ms) {
			return;
			//last = 1;
		}

		if (m_make_far_blocks_list.empty()) {
			++dr;
			//if os.clock() > end_ms or dr>=radius then last=1 end
			for (pos_t dx = -dr; dx <= dr; dx += dr * 2) {
				for (pos_t dy = -dr; dy <= dr; ++dy) {
					for (pos_t dz = -dr; dz <= dr; ++dz) {
						m_make_far_blocks_list.emplace_back(dx, dy, dz);
					}
				}
			}
			for (int dy = -dr; dy <= dr; dy += dr * 2) {
				for (int dx = -dr + 1; dx <= dr - 1; ++dx) {
					for (int dz = -dr; dz <= dr; ++dz) {
						m_make_far_blocks_list.emplace_back(dx, dy, dz);
					}
				}
			}
			for (int dz = -dr; dz <= dr; dz += dr * 2) {
				for (int dx = -dr + 1; dx <= dr - 1; ++dx) {
					for (int dy = -dr + 1; dy <= dr - 1; ++dy) {
						m_make_far_blocks_list.emplace_back(dx, dy, dz);
					}
				}
			}
		}
		for (const auto p : m_make_far_blocks_list) {
			//DUMP(dr, p, blockpos);
			makeFarBlock(blockpos + p);
		}
		m_make_far_blocks_list.clear();

		if (last) {
			break;
		}
	}
	if (m_make_far_blocks_last >= radius) {
		m_make_far_blocks_last = 0;
	}
}

FarMesh::FarMesh(scene::ISceneNode *parent, scene::ISceneManager *mgr, s32 id,
		// u64 seed,
		Client *client, Server *server) :
#if 0
		scene::ISceneNode(parent, mgr, id),
#endif
		// m_seed(seed),
		m_camera_pos(-1337, -1337, -1337),
		// m_time(0),
		m_client(client) //,
						 // m_render_range(20*MAP_BLOCKSIZE)
{
	// dstream<<__FUNCTION_NAME<<std::endl;

	// video::IVideoDriver* driver = mgr->getVideoDriver();
	/*
	m_materials[0].setFlag(video::EMF_LIGHTING, false);
	m_materials[0].setFlag(video::EMF_BACK_FACE_CULLING, true);
	// m_materials[0].setFlag(video::EMF_BACK_FACE_CULLING, false);
	m_materials[0].setFlag(video::EMF_BILINEAR_FILTER, false);
	m_materials[0].setFlag(video::EMF_FOG_ENABLE, false);
	// m_materials[0].setFlag(video::EMF_ANTI_ALIASING, true);
	// m_materials[0].MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
	m_materials[0].setFlag(video::EMF_FOG_ENABLE, true);

	m_materials[1].setFlag(video::EMF_LIGHTING, false);
	m_materials[1].setFlag(video::EMF_BACK_FACE_CULLING, false);
	m_materials[1].setFlag(video::EMF_BILINEAR_FILTER, false);
	m_materials[1].setFlag(video::EMF_FOG_ENABLE, false);
	// m_materials[1].setTexture(0, client->tsrc()->getTexture("treeprop.png"));
	m_materials[1].MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
	m_materials[1].setFlag(video::EMF_FOG_ENABLE, true);
*/

	m_render_range_max = g_settings->getS32("farmesh5");

	//  m_box = core::aabbox3d<f32>(-BS * MAX_MAP_GENERATION_LIMIT,-BS *
	//  MAX_MAP_GENERATION_LIMIT,-BS * MAX_MAP_GENERATION_LIMIT, BS * 1000000, BS
	//  * MAX_MAP_GENERATION_LIMIT, BS * MAX_MAP_GENERATION_LIMIT);
	m_box = core::aabbox3d<f32>(-BS * m_render_range_max, -BS * m_render_range_max,
			-BS * m_render_range_max, BS * m_render_range_max, BS * m_render_range_max,
			BS * m_render_range_max);

	EmergeManager *emerge_use = server			   ? server->getEmergeManager()
								: client->m_emerge ? client->m_emerge
												   : nullptr;

	if (emerge_use) {
		errorstream << "Farmesh: mgtype=" << emerge_use->mgparams->mgtype
					<< " water_level=" << emerge_use->mgparams->water_level
					<< " render_range_max=" << m_render_range_max << "\n";
		mg = emerge_use->getFirstMapgen();
		m_water_level = emerge_use->mgparams->water_level;

		farcontainer.m_mg = mg;
		farcontainer.visible_node = {client->ndef()->getId("default:stone")};
		//farcontainer.visible_node = {client->ndef()->getId("mapgen_stone")};
		//farcontainer.visible_node =
		//farcontainer.water_node = {client->ndef()->getId("mapgen_water_source")};
		//farcontainer.water_node = {client->ndef()->getId("mapgen_stone")};
		//farcontainer.water_node = farcontainer.visible_node;
		//farcontainer.water_node = {client->ndef()->getId("default:water_source")};
		//farcontainer.water_node = {client->ndef()->getId("default:tree")};
		//farcontainer.water_node = {client->ndef()->getId("default:water_flowing")};
		farcontainer.water_node = {client->ndef()->getId("default:diamondblock")};

		DUMP(farcontainer.water_node, farcontainer.visible_node);

		farcontainer.m_water_level = m_water_level;
		//DUMP(farcontainer.visible_node, farcontainer.water_node);
	}

	for (size_t i = 0; i < process_order.size(); ++i)
		process_order[i] = i;
	auto rng = std::default_random_engine{};
	//std::shuffle(std::begin(process_order), std::end(process_order), rng);
}

FarMesh::~FarMesh()
{
	// dstream<<__FUNCTION_NAME<<std::endl;
	// removeAll();
#if 0
	remove();
#endif
}

#if 0
void FarMesh::OnRegisterSceneNode()
{
	if (IsVisible) {
		// SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
		// SceneManager->registerNodeForRendering(this, scene::ESNRP_SKY_BOX);
	}

	ISceneNode::OnRegisterSceneNode();
}
#endif

int FarMesh::go_direction(const size_t dir_n, direction_cache &cache)
{
	DUMP(mg_cache.size(), dir_n);

	auto &draw_control = m_client->getEnv().getClientMap().getControl();

	//auto &plane_cache_item = plane_cache[m_camera_pos_aligned];
	//auto &plane_cache_item = plane_cache_dir[dir_n];
	const auto dir = g_6dirsf[dir_n];

	const auto grid_size_xy = grid_size_x * grid_size_y;
	int processed = 0;
	m_cycle_stop_i = 0;
	for (uint16_t i = m_cycle_stop_i; i < grid_size_xy; ++i) {
		//const auto p = i/grid_size_xy;
		{
			uint16_t y = uint16_t(process_order[i] / grid_size_x);
			uint16_t x = process_order[i] % grid_size_x;
			auto &ray_cache = cache[i];
			if (ray_cache.finished)
				continue;
			//ray_cache.filled = true;

			//(*grid_result_fill)[x][y].depth = 0; // clean cache

			//const auto first_square_r = MAP_BLOCKSIZE * grid_size_x;
			v3f dir_first = dir * m_render_range / 2; //first_square_r;
			//dir_l.X += x * MAP_BLOCKSIZE - MAP_BLOCKSIZE * grid_size_x/2;
			//dir_l.Y += y * MAP_BLOCKSIZE - MAP_BLOCKSIZE * grid_size_y/2;
			auto pos_center = dir_first + m_camera_pos;
			if (!dir.X)
				dir_first.X += MAP_BLOCKSIZE * (x - grid_size_x / 2);
			if (!dir.Y)
				dir_first.Y += MAP_BLOCKSIZE * (y - grid_size_y / 2);
			if (!dir.Z)
				dir_first.Z += MAP_BLOCKSIZE * (x - grid_size_x / 2);

			//DUMP(dir_l, x, y);
			v3f dir_l = dir_first.normalize();

			//const int depth_steps = 512 + 100; // TODO CALC  256+128+64+32+16+8+4+2+1+?
			// const int grid_size = 64;		   // 255;
			const int depth_max = m_render_range_max;
			int depth = m_render_range /** BS*/; // 255;
			//DUMP(pos_center, depth);
			for (size_t steps = 0;
					//ray_cache.step_num < depth_steps &&
					steps < 10; ++ray_cache.step_num, ++steps) {

				const auto dstep = (ray_cache.step_num + 1);
				const auto block_step =
						getFarmeshStep(draw_control, m_camera_pos_aligned / MAP_BLOCKSIZE,
								floatToInt(pos_center, BS) / MAP_BLOCKSIZE);
				const auto step_width = MAP_BLOCKSIZE * pow(2, block_step);
				//const auto step_width =	std::min(std::max<int>(1, step_r.getLength() / BS), 1024);
				//DUMP(dstep, m_camera_pos_aligned, block_step, step_width);
				depth += step_width * dstep; // TODO: TUNE ME
				// errorstream << depth << "\n";
				if (!i)
					DUMP(steps, ray_cache.step_num, processed, depth, step_width);

				if (depth > depth_max) {
					//DUMP("b1", depth, depth_max, step_width, dstep);
					ray_cache.finished = true;
					break;
				}

				// v1 auto pos_l = pos_ld + (step_u * y);

				// auto pos_l = pos_ld + (step_u * y);
				const auto pos = dir_l * depth * BS + m_camera_pos;

				//++stat_probes;
				// auto pos = pos_l + (step_r * x);
				if (pos.X > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.X < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z < -MAX_MAP_GENERATION_LIMIT * BS) {
					//DUMP("b2", pos);
					ray_cache.finished = true;
					break;
				}
				++processed;

				//DUMP(processed, steps, ray_cache.step_num, depth);
				const int step_aligned = pow(2, ceil(log(step_width) / log(2)));

				v3pos_t pos_int_raw = floatToInt(pos, BS);
				v3pos_t pos_int((pos_int_raw.X / step_aligned) * step_aligned,
						(pos_int_raw.Y / step_aligned) * step_aligned,
						(pos_int_raw.Z / step_aligned) * step_aligned);

				//bool visible = false;
				auto &visible = ray_cache.visible;

				if (pos_int.Y <= m_water_level && m_camera_pos.Y > m_water_level) {
					visible = true;
					ray_cache.finished = true;
				} else

				{
					if (const auto &it = mg_cache.find(pos_int); it != mg_cache.end()) {
						visible = it->second;
						//++stat_cached;
					} else {

						//++stat_mg;
						visible = mg->visible(pos_int.X, pos_int.Y, pos_int.Z);
						// if (visible) {
						//  cache.first = true;
						//  cache.second = pos_int;
						//}
						mg_cache[pos_int] = visible;
					}
				}
				//DUMP(visible, step_num, pos, pos_int);
				//plane_cache_item.depth[p]
				if (!visible) {
					continue;
				}
				ray_cache.finished = true;
				{
					const auto blockpos = getNodeBlockPos(pos_int);
					//DUMP("mfb", pos_int, blockpos);
					makeFarBlock6(blockpos);
					ray_cache.visible = visible;
				}
				break;
			}
		}
	}

	m_cycle_stop_i = 0;
	DUMP(processed);
	return processed;
}

void FarMesh::update(v3f camera_pos, v3f camera_dir, f32 camera_fov,
		CameraMode camera_mode, f32 camera_pitch, f32 camera_yaw, v3pos_t camera_offset,
		float brightness, s16 render_range)
{
	// errorstream << "update    " << (long)mesh << std::endl;

	if (!mg)
		return;

	if (0) {
		auto camera_block = floatToInt(camera_pos, BS * 16);
		//auto camera_pos_aligned = floatToInt(intToFloat(floatToInt(camera_pos, BS * 16), BS * 16), BS); // todo optimize
		//makeFarBlocks(camera_pos_aligned);
		//DUMP(camera_pos, camera_block);
		makeFarBlocks(camera_block);
		return;
	}

#if cache0
	auto camera_pos_aligned = floatToInt(
			intToFloat(floatToInt(camera_pos, BS * 16), BS * 16), BS); // todo optimize
	// reset on significant move
	if (m_camera_pos_aligned != camera_pos_aligned) {
		//errorstream << " m_camera_pos_aligned=" << m_camera_pos_aligned << "!=" << " camera_pos_aligned=" <<  camera_pos_aligned << " m_cycle_stop_i=" << m_cycle_stop_i << std::endl;
		m_cycle_stop_i = 0;
	}

#endif

	//DUMP(m_cycle_stop_i);
	if (!m_cycle_stop_i) {

#if cache_step
		m_camera_pos_aligned_by_step.clear();
		m_camera_pos_aligned_by_step.reserve(256); // todo calc from grid_size?
#endif
#if cache0
		m_camera_pos_aligned = camera_pos_aligned;
		//floatToInt(intToFloat(floatToInt(camera_pos, BS * 16), BS * 16), BS); // todo optimize
#endif

		const auto camera_pos_aligned = intToFloat(m_camera_pos_aligned, BS);
		// errorstream << " camera_pos_aligned=" << camera_pos_aligned	<< "
		// m_camera_pos="
		// << m_camera_pos << "\n";
		if (m_camera_pos == camera_pos_aligned) {
			//DUMP(m_cycle_stop_i, m_camera_pos, camera_pos_aligned);

			//return;
		}
		m_camera_pos = camera_pos_aligned;
		// errorstream << " camera_pos_aligned=" << camera_pos_aligned << "\n";
		//  v1:
		//  m_camera_pos = camera_pos;

		m_camera_dir = camera_dir;
		m_camera_fov = camera_fov;
		m_camera_pitch = camera_pitch;
		m_camera_yaw = camera_yaw;
		m_camera_offset = camera_offset;
		m_render_range = std::min<pos_t>(render_range, 255);

		errorstream << "update pos=" << m_camera_pos << " dir=" << m_camera_dir
					<< " fov=" << m_camera_fov << " pitch=" << m_camera_pitch
					<< " yaw=" << m_camera_yaw << " render_range=" << m_render_range
					<< std::endl;
	}

	if (direction_caches_pos != m_camera_pos_aligned) {
		DUMP("cache clear", direction_caches_pos);
		direction_caches_pos = m_camera_pos_aligned;
		direction_caches.fill({});
	}

	const int max_cycle_ms = 1000;
	u32 end_ms = porting::getTimeMs() + max_cycle_ms;

	//for (const auto dir & :  g_6dirsf) {
	for (int depth = 0; depth < 100; ++depth) {
		int processed = 0;
		for (auto i = 0; i < sizeof(g_6dirsf) / sizeof(g_6dirsf[0]); ++i) {
			//const auto dir = g_6dirsf[i];
			processed += go_direction(i, direction_caches[i]);
			DUMP("godir", depth, i, processed, direction_caches[i][0].step_num);
		}
		if (!processed)
			break;
		if (porting::getTimeMs() > end_ms)
			break;
	}
	DUMP("up finifhed", direction_caches[0][0].step_num);
	return;

#if 0

	// const auto start = 2 << 7;

	// auto from_center = m_camera_pos + m_camera_dir /** BS */ * start;

	/* v1: plane in front of player
	v3f dir_lu = v3f(0, 0, 1);
	dir_lu.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2));
	dir_lu.rotateYZBy(irr::core::radToDeg(-m_camera_fov / 2));
	dir_lu.rotateYZBy(m_camera_yaw);
	dir_lu.rotateXZBy(m_camera_pitch);

	v3f dir_ld = v3f(0, 0, 1);
	dir_ld.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2));
	dir_ld.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2));
	dir_ld.rotateYZBy(m_camera_yaw);
	dir_ld.rotateXZBy(m_camera_pitch);
	v3f dir_rd = v3f(0, 0, 1);
	dir_rd.rotateXZBy(irr::core::radToDeg(-m_camera_fov / 2));
	dir_rd.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2));
	dir_rd.rotateYZBy(m_camera_yaw);
	dir_rd.rotateXZBy(m_camera_pitch);
	*/

	v3f dir_lu = v3f(0, 0, 1);
	// dir_lu.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2)); //    <- left
	dir_lu.rotateYZBy(irr::core::radToDeg(-m_camera_fov / 2)); // ^ up

	v3f dir_ld = v3f(0, 0, 1);
	// dir_ld.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2)); //  <- left
	dir_ld.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2)); //  _ down
	// v3f dir_rd = v3f(0, 0, 1);
	// dir_rd.rotateXZBy(irr::core::radToDeg(-m_camera_fov / 2)); //  -> right
	// dir_rd.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2)); //  _ down

	v3f dir_l1 = v3f(0, 0, 1);
	dir_l1.rotateXZBy(360.0 / grid_size_x);					   //    -> right 1 step
	dir_l1.rotateYZBy(irr::core::radToDeg(-m_camera_fov / 2)); // ^ up
	// errorstream << " dir_l1=" << dir_l1 << "\n";

	const int depth_steps = 512 + 100; // TODO CALC  256+128+64+32+16+8+4+2+1+?
	// const int grid_size = 64;		   // 255;
	const int depth_max = m_render_range_max;
	// const int grid_size = 64; // 255;
	//  unordered_map_v2POS<std::pair<bool, v3pos_t>> grid_cache;

	TimeTaker timer_step("Client: Farmesh");

	uint32_t stat_probes = 0, stat_cached = 0, stat_mg = 0, stat_occluded = 0,
			 stat_plane_cache = 0, stat_cache_used = 0, stat_cache_sky = 0;

	auto nodemgr = m_client->getNodeDefManager();

	float step = BS * 1;
	float stepfac = 1.2;
	float startoff = BS * 1;
	float endoff = -MAP_BLOCKSIZE;
	unordered_map_v3pos<bool> occlude_cache;
	const auto cache_time = m_client->getEnv().getTimeOfDay();

	//const int max_cycle_ms = 20;
	const int max_cycle_ms = 500;
	u32 end_ms = porting::getTimeMs() + max_cycle_ms;

	/*
			for (short y = m_cycle_stop_y; y < grid_size; y += skip_y_num) {
					// errorstream << "pos_l=" << pos_l << "\n";
					// constexpr short skip_x_num = 7;

					for (short x = m_cycle_stop_x; x < grid_size; x += skip_x_num)
	   {
	*/
const auto grid_size_xy = grid_size_x * grid_size_y;
	for (uint16_t i = m_cycle_stop_i; i < grid_size_xy; ++i) {
//const auto p = i/grid_size_xy;
		{
			uint16_t y = uint16_t(process_order[i] / grid_size_x);
			uint16_t x = process_order[i] % grid_size_x;
			//			errorstream << " x=" << x << " y=" << y << " i=" << i << "
			// v="<<process_order[i] << "\n";

			if (porting::getTimeMs() > end_ms) {
				DUMP("stop", m_cycle_stop_i, i, porting::getTimeMs(), end_ms,
						max_cycle_ms);
				m_cycle_stop_i = i;
				// m_cycle_stop_x = x;
				/// m_cycle_stop_y = y;
				// grid_size = std::max<POS>(grid_size/2, grid_size_min);
				/*				errorstream << " stop "
																		<< "
				   m_cycle_stop_x=" << m_cycle_stop_x << " x=" << x
																		<< "
				   m_cycle_stop_y=" << m_cycle_stop_y << " y=" << y << "\n";
				*/
				return;
			}

			(*grid_result_fill)[x][y].depth = 0; // clean cache
					// errorstream << "grid clean" << " x=" << x << " y=" << y << "depth=" << (*grid_result_fill)[x][y].depth << "\n";

			int depth = m_render_range /** BS*/; // 255;

			/*auto &cache = grid_cache[{x, y}];
			if (cache.first) {
			  ++stat_cached;
			  continue;
			}*/


/*
//cylinder:
			v3f dir_l = v3f(0, 0, 1);

			auto stpy = (((float)y) / grid_size_y);
			auto degy = 70 - (140.0 * stpy);
			dir_l.rotateYZBy(degy); // ^ up same as dir_lu

			float movx = (((float)x / grid_size_x));
			dir_l.rotateXZBy(360.0 * movx); //    -> right 1 step
			// todo fov y
*/

/*
			v3f dir_l (g_6dirs[0].X, g_6dirs[0].Y, g_6dirs[0].Z);

auto dir_lu = dir_l;
	dir_lu.rotateXZBy(45); //    <- left
	dir_lu.rotateYZBy(-45); // ^ up
*/
//const auto first_square_r = MAP_BLOCKSIZE * 10;
const auto first_square_r = MAP_BLOCKSIZE * grid_size_x;
v3f dir_l =  g_6dirsf[0]*first_square_r;
//dir_l.X += x * MAP_BLOCKSIZE - MAP_BLOCKSIZE * grid_size_x/2;
//dir_l.Y += y * MAP_BLOCKSIZE - MAP_BLOCKSIZE * grid_size_y/2;
dir_l.X += MAP_BLOCKSIZE * (x - grid_size_x/2);
dir_l.Y += MAP_BLOCKSIZE * (y - grid_size_y/2);
DUMP(dir_l, x,y);
dir_l = dir_l.normalize();


/*
			auto rotateSquareXZBy = [](auto &dir, auto angle) {
				//        0    45
				// 315  ___.___
				//    |       |
				//    |       |
				// 270 -   o   - 90
				//    |       |
				//    |       |
				//     ___.___  135
				//  225  180
				// errorstream << "rotateSquareXZBy ddir? " << dir << " angle=" << angle
				// << "\n";
				if (angle <= 45) {
					// dir.X
				} else if (angle < 135) {
				} else if (angle < 225) {
				} else if (angle < 315) {
				}
			};
			rotateSquareXZBy(dir_l, 360.0 * movx);
*/
			if (0)
				errorstream << "deg "
							<< " x=" << x << "/" << grid_size_x << " y=" << y << "/"
							<< grid_size_y 
							//<< " movx=" << movx 
							//<< " stpy=" << stpy
							//<< " degy=" << degy 
							<< " dir_l=" << dir_l << "\n";

#if cache0
			v3pos_t pos_int_0;
#endif
#if cache_step
			std::vector<v3pos_t> plane_cache_step_pos;
#endif
			for (short step_num = 0; step_num < depth_steps; ++step_num) {

				const auto pos_ld = dir_ld * depth * BS + m_camera_pos;
				// v1 const auto pos_rd = dir_rd * depth * BS + m_camera_pos;
				const auto pos_l1 = dir_l1 * depth * BS + m_camera_pos;

				const auto pos_lu = dir_lu * depth * BS + m_camera_pos;
				const auto step_r = pos_l1 - pos_lu;
				// const auto step_r = (pos_rd - pos_ld) / grid_size_x;
				const auto step_u = (pos_lu - pos_ld) / grid_size_y;
				// auto step_width = std::max(1, int(step_r.getLength()) >> 1 << 1);
				const auto step_width =
						std::min(std::max<int>(1, step_r.getLength() / BS), 1024);
				// #if cache0
				const int step_aligned = pow(2, ceil(log(step_width) / log(2)));
				// #else
				//  int step_aligned = pow(2, std::floor(log(step_width) / log(2)));
				// #endif

				const auto dstep = (step_num + 1);
				// DUMP(x,y, depth, dstep, step_width);
				//  errorstream << " x=" << x << " y=" << y << " " << " depth +=
				//  step_width
				//  * 8 :" << depth << "+=" << step_width << "*" << dstep << " = ";
				depth += step_width * dstep; // TODO: TUNE ME
				// errorstream << depth << "\n";
				if (depth > depth_max)
					break;

				// v1 auto pos_l = pos_ld + (step_u * y);

				// auto pos_l = pos_ld + (step_u * y);
				const auto pos = dir_l * depth * BS + m_camera_pos;

				++stat_probes;
				// auto pos = pos_l + (step_r * x);
				if (pos.X > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.X < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z < -MAX_MAP_GENERATION_LIMIT * BS)
					break;

				v3pos_t pos_int_raw = floatToInt(pos, BS);
				v3pos_t pos_int((pos_int_raw.X / step_aligned) * step_aligned,
						(pos_int_raw.Y / step_aligned) * step_aligned,
						(pos_int_raw.Z / step_aligned) * step_aligned);
				/*

								if (x == grid_size_x / 2 && y == grid_size_y / 2)
										errorstream << " step_num=" << step_num
																<< " step_width=" <<
				   step_width
																<< " step_aligned=" <<
				   step_aligned << "depth=" << depth
																<< " pos=" << pos
																<< " pos_int=" <<  pos_int
																//<< " pos_l=" << pos_l
																<< " pos_ld=" <<
								   pos_ld
								   << " dir_l=" << dir_l
								   << "\n";
				*/
				// if (step_num == 0) {
#if cache_step
				// m_camera_pos_aligned.reserve(step_num);
				// m_camera_pos_aligned[step_num] =
				// floatToInt(intToFloat(floatToInt(m_camera_pos, BS * step_aligned), BS
				// * step_aligned),	BS); // todo optimize
				if (step_num >= m_camera_pos_aligned_by_step.size())
					m_camera_pos_aligned_by_step.emplace_back(floatToInt(
							intToFloat(floatToInt(m_camera_pos, BS * step_aligned),
									BS * step_aligned),
							BS)); // todo optimize
				//}
				auto &plane_cache_item =
						plane_cache[m_camera_pos_aligned_by_step[step_num]];

				/*
												if (x == grid_size / 2 && y == grid_size
				   / 2) errorstream << " planecache ini"
																				<< "
				   step_num=" << step_num
																				<< "
				   cpa=" << m_camera_pos_aligned[step_num]
																				<< "
				   pos_int=" << pos_int
																				<< "
				   step_aligned=" << step_aligned
																				<< "
				   m_camera_pos_aligned.size="
																				<<
				   m_camera_pos_aligned.size()
																				<< "
				   m_camera_pos_aligned0=" << m_camera_pos_aligned[0]
																				<< "
				   tm=" << plane_cache_item.time
																				<< "
				   sz=" << plane_cache_step_pos.size()
				   << "\n";
				*/
				plane_cache_step_pos.emplace_back(pos_int);

				// cache try 2
				{
					// pos_int_0 = pos_int;
					/*					if (const auto &it =
					   plane_cache.find(m_camera_pos_aligned); it != plane_cache.end()) {
																													if (const auto &it2 =
					   it->second.depth.find(pos_int); it2 != it->second.depth.end()) {*/
					// TODO: deleter
					plane_cache_item.time = cache_time;
					if (const auto &it2 = plane_cache_item.depth.find(pos_int);
							it2 != plane_cache_item.depth.end()) {
						++stat_plane_cache;
						const auto depth_cached = it2->second;
						const auto &pos_cached = plane_cache_item.pos[pos_int];
						const auto cache_valid =
								step_num < plane_cache_item.step[pos_int];

						/*						if (x == grid_size / 2 && y ==
						   grid_size / 2) errorstream
																								<< " planecache got"
																								<< " step_num=" << step_num
																								<< " cpa=" <<
						   m_camera_pos_aligned[step_num]
																								<< " pos_int=" << pos_int << "
						   => " << depth_cached
																								<< " pos_cached=" <<
						   pos_cached << " t="
																								<<
						   plane_cache[m_camera_pos_aligned[step_num]].time
																								<< " s1=" <<
						   plane_cache.size() << " s2="
																								<<
						   plane_cache[m_camera_pos_aligned[step_num]] .depth.size()
																								//<< " sw=" << step_width
																								<< " swc=" <<
						   plane_cache_item.step_width[pos_int]
																								<< " snc=" <<
						   plane_cache_item.step[pos_int]
																								<< " cache_valid=" <<
						   cache_valid << "\n";
						*/
						if (!depth_cached && cache_valid) {
							++stat_cache_sky;
							break;
						}
						if (cache_valid) {
							++stat_cache_used;
							/*
							auto l = depth_cached / (MAX_MAP_GENERATION_LIMIT * 2);
							uint8_t color = 255 * l;
							driver->draw3DLine(
											intToFloat(pos_cached - m_camera_offset, BS),
											intToFloat(v3pos_t(plane_cache_item.step_width[pos_int],
																			   0, 0 +
							!pos_cached.Y) + pos_cached - m_camera_offset, BS),
											irr::video::SColor(255 * (pos_cached.Y < 0),
							color, 255 * !pos_cached.Y, 0));
			  */
							/*
																					if (x
							   == grid_size / 2 && y == grid_size / 2) errorstream
																											<< " draw cached "
																											<<
							   intToFloat(pos_cached - m_camera_offset, BS)
																											<< " , "
																											<< intToFloat(
																															   v3pos_t(plane_cache_item
																																							   .step_width[pos_int],
																																			   0,
							   0 + !pos_cached.Y) + pos_cached - m_camera_offset, BS)
																											<< "\n";
							*/
							break;
						}
						/*						} else {
																																		plane_cache[m_camera_pos_aligned].depth[pos_int]
						   = 0;
																														}*/
					} else {
						/*
																		if (x == grid_size
						   / 2 && y == grid_size / 2) errorstream
																								<< " planecache MISS "
																								<< " cpa=" <<
						   m_camera_pos_aligned[step_num]
																								<< " pos_int=" << pos_int << "
						   t="
																								<<
						   plane_cache[m_camera_pos_aligned[step_num]].time
																								<< " s1=" <<
						   plane_cache.size() << " s2="
																								<<
						   plane_cache[m_camera_pos_aligned[step_num]] .depth.size()
																								<< "\n";
						*/
						plane_cache_item.depth[pos_int] = 0;
						// plane_cache_item.step_width[pos_int] = step_num;
						plane_cache_item.step[pos_int] = step_num;
					}
				}
#endif
				auto &plane_cache_item = plane_cache[m_camera_pos_aligned];
				if (step_num == 0) {

#if cache0
					pos_int_0 = pos_int;
					/*if (const auto &it =
					   plane_cache.find(m_camera_pos_aligned); it != plane_cache.end()) {
																													if (const auto &it2 =
					   it->second.depth.find(pos_int); it2 != it->second.depth.end()) {*/

					// TODO: deleter
					plane_cache_item.time = cache_time;
					if (const auto &it2 = plane_cache_item.depth.find(pos_int_0);
							it2 != plane_cache_item.depth.end()) {
						++stat_plane_cache;
						const auto depth_cached = it2->second;
						const auto &pos_cached = plane_cache_item.pos[pos_int_0];
						/*
																		errorstream<<" planecache "<<" x=" << x << " y=" << y << " i=" << i<<" v=" << process_order[i]<<" cpa=" << m_camera_pos_aligned<<" pos_int_0=" << pos_int_0 << "=> " << depth_cached<<" t=" << plane_cache[m_camera_pos_aligned].time<<" s1=" << plane_cache.size() << " s2="<<plane_cache[m_camera_pos_aligned].depth.size() << "\n";
						*/

						if (!depth_cached)
							break;
						/*
																		auto l =
						   depth_cached / (MAX_MAP_GENERATION_LIMIT * 2); uint8_t color =
						   255 * l; driver->draw3DLine(intToFloat(pos_cached -
						   m_camera_offset, BS),
																						intToFloat(v3pos_t(plane_cache_item.step_width[pos_int_0],
																														   0, 0 +
						   !pos_cached.Y) + pos_cached - m_camera_offset, BS),
																						irr::video::SColor(255
						   * (pos_cached.Y < 0), color, 255 * !pos_cached.Y, 0));
																										*/
						(*grid_result_fill)[x][y].pos = pos_cached;
						(*grid_result_fill)[x][y].depth = depth_cached;
						(*grid_result_fill)[x][y].step_width =
								plane_cache_item.step_width[pos_int_0];

						break;
						/*						} else {
																																		plane_cache[m_camera_pos_aligned].depth[pos_int]
						   = 0;
																														}*/
					} else {
						/*errorstream << " planecaMISS "
																		<< " cpa=" <<
						   m_camera_pos_aligned
																		<< " pos_int=" <<
						   pos_int << " t=" << plane_cache[m_camera_pos_aligned].time
																		<< " s1=" <<
						   plane_cache.size()
																		<< " s2=" <<
						   plane_cache[m_camera_pos_aligned].depth.size()
																		<< "\n";*/
						plane_cache_item.depth[pos_int_0] = 0;
					}

#endif
					/*					 OK
					// TODO: test speed, really faster with?:
					if (isOccluded(&m_client->getEnv().getClientMap(),
								floatToInt(m_camera_pos, BS), pos_int, step, stepfac,
								startoff, endoff, 1, nodemgr, occlude_cache)) {
						++stat_occluded;
						break;
					}
					 */
				}
				/*
												if (x == grid_size / 2 && y == grid_size
				   / 2) // if (!x
				   && !y) errorstream << " x=" << x << " y=" << y << " step_num=" <<
				   step_num
																								  << " depth=" << depth << "
				   pos=" << pos
																								  << " pos_int=" << pos_int << "
				   pos_ld=" << pos_ld
																								  << " pos_rd=" << pos_rd << "
				   step_r=" << step_r
																								  << " srl=" <<
				   step_r.getLength()
																								  << " step_width=" <<
				   step_width
																								  << " step_aligned=" <<
				   step_aligned
																								  << " step_u=" << step_u << "
				   sul=" << step_u.getLength()
																								  << " water_level=" <<
				   m_water_level << "\n";
				*/
				bool visible = false;

				if (pos_int.Y <= m_water_level && m_camera_pos.Y > m_water_level) {
					visible = true;
					// cache.second = pos_int;

					/*
					if (x == grid_size / 2 && y == grid_size / 2) // if (!x && !y)
					  errorstream << "underwater"
																	  << " x=" << x << "
					y=" << y << " step_num="
					<< step_num
																	  << " depth=" <<
					depth << " pos=" << pos
																	  << " pos_int=" <<
					pos_int << " visible=" << visible
																	  << "\n";
		  */
				} else

				{
					/*
			   if (x == grid_size / 2 && y == grid_size / 2) // if (!x && !y)
							   errorstream << "have mg"
																			   << " x=" <<
			   x << " y=" << y
																			   << "
			   step_num=" << step_num << " depth=" << depth
																			   << " pos="
			   << pos << " pos_int=" << pos_int
																			   << "
			   visible=" << visible << "\n";
					*/
					if (const auto &it = mg_cache.find(pos_int); it != mg_cache.end()) {
						visible = it->second;
						++stat_cached;
						/*
				   if (
												   // visible ||
												   x == grid_size / 2 && y == grid_size /
				   2) errorstream << "have mg cache"
																				   << "
				   x="
				   << x << " y=" << y
																				   << "
				   step_num=" << step_num << " depth=" << depth
																				   << "
				   pos=" << pos << " pos_int=" << pos_int
																				   << "
				   visible=" << visible << "\n";
						*/
					} else {

						++stat_mg;
						visible = mg->visible(pos_int.X, pos_int.Y, pos_int.Z);
						// if (visible) {
						//  cache.first = true;
						//  cache.second = pos_int;
						//}
						mg_cache[pos_int] = visible;
						/*
				   if (
												   // visible||
												   x == grid_size / 2 && y == grid_size /
				   2) errorstream << "mg calc"
																				   << "
				   x="
				   << x << " y=" << y
																				   << "
				   step_num=" << step_num << " depth=" << depth
																				   << "
				   pos=" << pos << " pos_int=" << pos_int
																				   << "
				   srl=" << step_r.getLength()
																				   << "
				   step_width=" << step_width
																				   << "
				   step_aligned=" << step_aligned

																				   << "
				   visible=" << visible << "\n";
						*/
					}
				}

				if (!visible) {

					/*
													   for (short step_old = 0; step_old
					   <= step_num;
					   ++step_old) { auto &plane_cache_item_save =
																			   plane_cache[m_camera_pos_aligned[step_old]];
															   plane_cache_item_save.depth[plane_cache_step_pos[step_old]]
					   = 0;
													   }*/

					continue;
				}

				/* // if (x == grid_size / 2 && y == grid_size / 2)
				errorstream << "draw "
										<< " x=" << x << " y=" << y << " step_num=" <<
				step_num
										<< "depth = " << depth << " pos=" << pos
										<< " pos_int=" << pos_int
										<< " pos_int_float=" << intToFloat(pos_int, BS)
										<< "step_r=" << step_r << " step_width=" <<
				step_width
										<< "step_aligned =" << step_aligned << "\n ";*/

#if cache0
				// plane_cache[m_camera_pos_aligned]
				plane_cache_item.depth[pos_int_0] = depth;
				plane_cache_item.pos[pos_int_0] = pos_int;
				plane_cache_item.step_width[pos_int_0] = step_width;
#endif

#if cache_step
				// for ( auto & step_old : plane_cache_step_pos)
				for (short step_old = 0; step_old <= step_num; ++step_old) {
					/*
															if (x == grid_size / 2 && y ==
					   grid_size / 2) errorstream << " plane cache set step_old=" <<
					   step_old << "/"
																							<< step_num << " step pos"
																							<< plane_cache_step_pos[step_old]
																							<< " depth=" << depth << "
					   pos_int=" << pos_int
																							<< " step_width=" << step_width <<
					   "\n";
					*/

					auto &plane_cache_item_save =
							plane_cache[m_camera_pos_aligned_by_step[step_old]];
					plane_cache_item_save.depth[plane_cache_step_pos[step_old]] = depth;
					plane_cache_item_save.pos[plane_cache_step_pos[step_old]] = pos_int;
					plane_cache_item_save.step_width[plane_cache_step_pos[step_old]] =
							step_width;
					plane_cache_item_save.step[plane_cache_step_pos[step_old]] =
							step_num; // step_old;
				}
#endif

				/*errorstream << " set pcache " << m_camera_pos_aligned << " : "
																<< pos_int_0 << " = " <<
				   depth << " s1=" << plane_cache.size()
																<< " s2=" <<
				   plane_cache[m_camera_pos_aligned].depth.size()
																<< "\n";*/

				// pos -= intToFloat(m_camera_offset, BS);
				//  driver->draw3DLine(pos, pos + v3f(step_width*BS, 0, 0),
				//  driver->draw3DLine(pos, intToFloat(pos_int - m_camera_offset, BS),

				(*grid_result_fill)[x][y].pos = pos_int;
				(*grid_result_fill)[x][y].depth = depth;
				(*grid_result_fill)[x][y].step_width = step_width;

				if (0)
					errorstream << "grid res"
								<< " x=" << x << " y=" << y << " depth=" << depth
								<< " step_width = " << step_width
								<< " pos_int=" << pos_int << "\n";

				/*
												driver->draw3DLine(intToFloat(pos_int
				   - m_camera_offset, BS), intToFloat(v3pos_t(step_width, 0, 0 +
				   step_width
				   * !pos_int.Y) + pos_int - m_camera_offset, BS),
																irr::video::SColor(255
				   * (pos_int.Y < 0), 255 - step_num, 255 * !pos_int.Y, 0));
				*/
				//if(0)
				{
					const auto blockpos = getNodeBlockPos(pos_int);
					/*
					const auto step =
							getFarmeshStep(m_client->getEnv().getClientMap().getControl(),
									getNodeBlockPos(camera_pos_aligned), blockpos);
					//const auto g = inFarmeshGrid(bp, step);
*/
					//makeFarBlock(blockpos, step);
DUMP("mfb", pos_int, blockpos);
					makeFarBlock(blockpos);
				}
				break;
			}
		}
		// m_cycle_stop_x = m_skip_x_current;
		//(++m_skip_x_current) %= skip_x_num;
		//  errorstream << " m_skip_x_current=" << m_skip_x_current << "\n";
	}
	m_cycle_stop_i = 0;
	// m_cycle_stop_y = m_skip_y_current;
	//(++m_skip_y_current) %= skip_y_num;
	//  grid_size = std::min<POS>(grid_size * 2, grid_size_max);

	// info
	/*
	errorstream
					<< " time=" << timer_step.getTimerTime() << " grid_size=" <<
	grid_size
					<< " stat_probes=" << stat_probes << " stat_cached=" <<
	stat_cached
					<< " stat_mg=" << stat_mg << " stat_occluded=" <<
	stat_occluded
					<< " plane_cache=" << stat_plane_cache << " cache_used=" <<
	stat_cache_used
					<< " cache_sky="
					<< stat_cache_sky
					//<< " mg_cache=" << mg_cache.size() << " cpa0=" <<
					// m_camera_pos_aligned_by_step[0]
					//<< " pcache=" <<
	plane_cache[m_camera_pos_aligned_by_step[0]].depth.size()
					<< "\n";
  */
	// errorstream << "fin\n";
	//  grid_result = std::move(grid_result_wip);
	std::swap(grid_result_use, grid_result_fill);

	//!! CreateMesh();
#endif
}

#if 0 

// creates a hill plane
void FarMesh::CreateMesh()
{

	/*irr::scene::IMesh* createHillPlaneMesh(
					const core::dimension2d<f32>& tileSize,
					const core::dimension2d<u32>& tc, video::SMaterial* material,
					f32 hillHeight, const core::dimension2d<f32>& ch,
					const core::dimension2d<f32>& textureRepeatCount) //const
	*/

	core::dimension2d<u32> tc = {grid_result_use->size(), grid_result_use->size()};
	core::dimension2d<f32> textureRepeatCount{1, 1};
	//f32 hillHeight = 1;
	const core::dimension2d<f32> &ch{1, 1};

	core::dimension2d<u32> tileCount = tc;
	core::dimension2d<f32> countHills = ch;

	if (countHills.Width < 0.01f)
		countHills.Width = 1.f;
	if (countHills.Height < 0.01f)
		countHills.Height = 1.f;

	// center
	// const core::position2d<f32> center((tileSize.Width * tileCount.Width) *
	// 0.5f, (tileSize.Height * tileCount.Height) * 0.5f);

	// texture coord step
	const core::dimension2d<f32> tx(textureRepeatCount.Width / tileCount.Width,
			textureRepeatCount.Height / tileCount.Height);

	// add one more point in each direction for proper tile count
	//++tileCount.Height;
	//++tileCount.Width;

	irr::scene::SMeshBuffer *buffer = new irr::scene::SMeshBuffer();
	video::S3DVertex vtx;
	vtx.Color.set(255, 155, 155, 155);

	// create vertices from left-front to right-back
	// u32 x;

	constexpr auto debug1 = 0;

	// f32 sx=0.f, tsx=0.f;
	size_t cnt = 0;
	size_t x = 0;
	// size_t y = 0;
	for (auto &ya : *grid_result_use) {
		// f32 sy=0.f, tsy=0.f;
		size_t y = 0;
		// size_t x = 0;
		for (auto &point : ya) {
			const auto &pos_int = point.pos;
			//const auto step_width = point.step_width;
			const auto depth_cached = point.depth;
			// if (!depth_cached)
			//	continue;
			/*
					f32 sx=0.f, tsx=0.f;
					for (x=0; x<tileCount.Width; ++x)
					{
							f32 sy=0.f, tsy=0.f;
							for (u32 y=0; y<tileCount.Height; ++y)
							{*/
			// vtx.Pos.set(sx - center.X, 0, sy - center.Y);
			if (depth_cached)
				vtx.Pos = intToFloat(pos_int - m_camera_offset, BS);
			// vtx.Pos.set(pos_int.X, pos_int.Y, pos_int.Z);
			// vtx.TCoords.set(tsx, 1.0f - tsy);

			/*			if (core::isnotzero(hillHeight))
											vtx.Pos.Y = sinf(vtx.Pos.X *
			   countHills.Width * core::PI / center.X) * cosf(vtx.Pos.Z *
			   countHills.Height * core::PI / center.Y) * hillHeight;
			*/
			if (debug1)
				errorstream << "vtx add " << cnt << " depth_cached=" << depth_cached
							<< " x=" << x << " y=" << y << " pos=" << vtx.Pos << "\n";

			auto color = (4 - log(depth_cached)) * 50;
			vtx.Color =
					irr::video::SColor(255, 255 - 100 * (pos_int.Y < 0), /*- step_num*/
							255 * (m_water_level - 1 == pos_int.Y), color);

			buffer->Vertices.push_back(vtx);
			// sy += tileSize.Height;
			// tsy += tx.Height;
			++cnt;
			++y;
			//++x;
		}
		// sx += tileSize.Width;
		// tsx += tx.Width;
		++x;
		//++y;
	}

	// create indices
	/*   	size_t x = 0;
			for (auto &ya : *grid_result_use) {

	size_t y = 0;
					f32 sy=0.f, tsy=0.f;
					for (auto &point : ya) {
							*/
	/*	for (x=0; x<tileCount.Width-1; ++x)
			{
					for (u32 y=0; y<tileCount.Height-1; ++y)
					{*/

	for (size_t x = 0; x < grid_size_x - 1; ++x) {
		for (u32 y = 0; y < grid_size_y - 1; ++y) {
			const auto &point = (*grid_result_use)[x][y];
			// const auto &pos_int = point.pos;
			// const auto step_width = point.step_width;
			const auto depth_cached = point.depth;
			if (!depth_cached)
				continue;

			// const s32 current = x * tileCount.Height + y;
			const s32 current = x * grid_size_y + y;

			if ((*grid_result_use)[x][y + 1].depth &&
					(*grid_result_use)[x + 1][y].depth) {

				if ((*grid_result_use)[x][y].depth /*&&
					   (*grid_result_use)[x][y].pos.getDistanceFrom(
							   (*grid_result_use)[x + 1][y].pos) <
							   (*grid_result_use)[x][y].step_width * 2 &&
					   (*grid_result_use)[x][y].pos.getDistanceFrom(
							   (*grid_result_use)[x][y + 1].pos) <
							   (*grid_result_use)[x][y].step_width * 2*/
				) {
					if (debug1)
						errorstream << "tri add1 " << current
									<< " depth_cached=" << depth_cached << " x=" << x
									<< " y=" << y
									<< " pos1=" << (*grid_result_use)[x][y].pos
									<< " pos2=" << (*grid_result_use)[x][y + 1].pos
									<< " pos3=" << (*grid_result_use)[x + 1][y].pos
									<< " dp1=" << (*grid_result_use)[x][y].depth
									<< " dp2=" << (*grid_result_use)[x][y + 1].depth
									<< " dp3=" << (*grid_result_use)[x + 1][y].depth
									<< " sw1=" << (*grid_result_use)[x][y].step_width
									<< " sw2=" << (*grid_result_use)[x][y + 1].step_width
									<< " sw3=" << (*grid_result_use)[x + 1][y].step_width
									<< std::endl;
					/*
						* --- *
						|
						*
					*/
					buffer->Indices.push_back(current);
					buffer->Indices.push_back(current + 1);
					buffer->Indices.push_back(current + tileCount.Height);
					// buffer->Indices.push_back(current + grid_size);
				}

				if ((*grid_result_use)[x + 1][y + 1].depth
						/*&&
								(*grid_result_use)[x + 1][y].pos.getDistanceFrom(
										(*grid_result_use)[x + 1][y + 1].pos) <
										(*grid_result_use)[x + 1][y].step_width * 2 &&
								(*grid_result_use)[x + 1][y].pos.getDistanceFrom(
										(*grid_result_use)[x][y + 1].pos) <
										(*grid_result_use)[x + 1][y].step_width * 2 */

				) {

					if (debug1)
						errorstream
								<< "tri add2 " << current
								<< " depth_cached=" << depth_cached << " x=" << x
								<< " y=" << y
								<< " pos1=" << (*grid_result_use)[x][y + 1].pos
								<< " pos2=" << (*grid_result_use)[x + 1][y + 1].pos
								<< " pos3=" << (*grid_result_use)[x + 1][y].pos
								<< " dp1=" << (*grid_result_use)[x][y + 1].depth
								<< " dp2=" << (*grid_result_use)[x + 1][y + 1].depth
								<< " dp3=" << (*grid_result_use)[x + 1][y].depth
								<< " sw1=" << (*grid_result_use)[x][y + 1].step_width
								<< " sw2=" << (*grid_result_use)[x + 1][y + 1].step_width
								<< " sw3=" << (*grid_result_use)[x + 1][y].step_width
								<< std::endl;
					/*
							  *
							  |
						* --- *
					*/

					buffer->Indices.push_back(current + 1);
					buffer->Indices.push_back(current + 1 + tileCount.Height);
					buffer->Indices.push_back(current + tileCount.Height);
				}
			} else {
				if (debug1)
					errorstream << "tri skip " << current
								<< " depth_cached=" << depth_cached << " x=" << x
								<< " y=" << y << std::endl;
			}
			//++y;
		}
		//++x;
	}

	// recalculate normals
	for (u32 i = 0; i < buffer->Indices.size(); i += 3) {
		const core::vector3df normal =
				core::plane3d<f32>(buffer->Vertices[buffer->Indices[i + 0]].Pos,
						buffer->Vertices[buffer->Indices[i + 1]].Pos,
						buffer->Vertices[buffer->Indices[i + 2]].Pos)
						.Normal;

		buffer->Vertices[buffer->Indices[i + 0]].Normal = normal;
		buffer->Vertices[buffer->Indices[i + 1]].Normal = normal;
		buffer->Vertices[buffer->Indices[i + 2]].Normal = normal;
	}

	/*	irr::video::SMaterial material_;
			material_.setFlag(irr::video::EMF_FOG_ENABLE, 1);
			// material_.setFlag(irr::video::EMF_NORMALIZE_NORMALS, 1);
			//  material_.ZBuffer = irr::video::ECFN_ALWAYS;
			// material_.setFlag(irr::video::EMF_ZBUFFER,
	   irr::video::ECFN_ALWAYS); material_.setFlag(irr::video::EMF_WIREFRAME, 1);
			auto material = &material_;
	*/
	irr::video::SMaterial *material = nullptr;

	if (material)
		buffer->Material = *material;

	buffer->recalculateBoundingBox();
	buffer->setHardwareMappingHint(irr::scene::EHM_STATIC);

	// irr::scene::SMesh*
	// errorstream << "mesh replace  " << (long)mesh << std::endl;
	if (mesh)
		mesh->drop();
	mesh = new irr::scene::SMesh();
	// errorstream << "mesh replaced " << (long)mesh << std::endl;
	mesh->addMeshBuffer(buffer);
	mesh->recalculateBoundingBox();
	buffer->drop();
	// errorstream << "mesh filleed  " << (long)mesh << std::endl;

	//mesh->setMaterialFlag(irr::video::EMF_FOG_ENABLE, 1);
	// mesh->setMaterialFlag(irr::video::EMF_NORMALIZE_NORMALS, 1);
	//   material_.ZBuffer = irr::video::ECFN_ALWAYS;
	//  material_.setFlag(irr::video::EMF_ZBUFFER, irr::video::ECFN_ALWAYS);
	// mesh->setMaterialFlag(irr::video::EMF_WIREFRAME, 1);

	// return mesh;
	// errorstream << "mesh=" << mesh << "\n";
}

void FarMesh::render()
{
	// errorstream << "render  " << (long)mesh << std::endl;
	video::IVideoDriver *driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);
	driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
	irr::video::SMaterial material;
	material.Lighting = false;
	material.BackfaceCulling = false;
	// material.ZBuffer = irr::video::ECFN_ALWAYS;
	material.ZBuffer = irr::video::ECFN_LESSEQUAL;
	driver->setMaterial(material);

	/*
		if (mesh)
			driver->drawMeshBuffer(mesh->getMeshBuffer(0));
	*/

	for (auto &ya : *grid_result_use) {
		for (auto &point : ya) {
			const auto &pos_int = point.pos;
			const auto step_width = point.step_width;
			const auto depth_cached = point.depth;
			if (!depth_cached)
				continue;
			// auto l = (float)depth_cached / (MAX_MAP_GENERATION_LIMIT * 2);
			auto color = (4 - log(depth_cached)) * 50;
			// uint8_t color = 255 * l;
			// log(step_width) / log(2)

			// errorstream << "depth_cached=" << depth_cached << " l=" << l << "
			// color="
			// << (int)color << "\n";

			{
				video::SMaterial m_material;
				video::S3DVertex m_vertices[4];

				{
					f32 tx0 = 0, tx1 = 0.0, ty0 = 0.0, ty1 = 0;
					v2f scale;
					float m_size = 10;
					video::SColor m_color;

					m_color = irr::video::SColor(255, 255 - 100 * (pos_int.Y < 0),
							255 * (m_water_level - 1 == pos_int.Y), color);

					/*
						if (m_texture.tex != nullptr)
							scale = m_texture.tex -> scale.blend(m_time /
					   (m_expiration+0.1)); else
					*/
					scale = v2f(1.f, 1.f);

					/*	if (m_animation.type != TAT_NONE) {
							const v2u32 texsize = m_material.getTexture(0)->getSize();
							v2f texcoord, framesize_f;
							v2u32 framesize;
							texcoord = m_animation.getTextureCoords(texsize,
					   m_animation_frame); m_animation.determineParams(texsize, NULL,
					   NULL, &framesize); framesize_f = v2f(framesize.X / (float)
					   texsize.X, framesize.Y / (float) texsize.Y);

							tx0 = m_texpos.X + texcoord.X;
							tx1 = m_texpos.X + texcoord.X + framesize_f.X * m_texsize.X;
							ty0 = m_texpos.Y + texcoord.Y;
							ty1 = m_texpos.Y + texcoord.Y + framesize_f.Y * m_texsize.Y;
						} else {
							tx0 = m_texpos.X;
							tx1 = m_texpos.X + m_texsize.X;
							ty0 = m_texpos.Y;
							ty1 = m_texpos.Y + m_texsize.Y;
						}
					*/

					auto half = m_size * .5f, hx = half * scale.X, hy = half * scale.Y;

					m_vertices[0] =
							video::S3DVertex(-hx, -hy, 0, 0, 0, 0, m_color, tx0, ty1);
					m_vertices[1] =
							video::S3DVertex(hx, -hy, 0, 0, 0, 0, m_color, tx1, ty1);
					m_vertices[2] =
							video::S3DVertex(hx, hy, 0, 0, 0, 0, m_color, tx1, ty0);
					m_vertices[3] =
							video::S3DVertex(-hx, hy, 0, 0, 0, 0, m_color, tx0, ty0);

					// see #10398
					// v3s16 camera_offset = m_env->getCameraOffset();
					// particle position is now handled by step()
					/*
						m_box.reset(v3f());

						for (video::S3DVertex &vertex : m_vertices) {
							if (m_vertical) {
								v3f ppos = m_player->getPosition()/BS;
								vertex.Pos.rotateXZBy(std::atan2(ppos.Z - m_pos.Z, ppos.X
					   - m_pos.X) / core::DEGTORAD + 90); } else {
								vertex.Pos.rotateYZBy(m_player->getPitch());
								vertex.Pos.rotateXZBy(m_player->getYaw());
							}
							m_box.addInternalPoint(vertex.Pos);
						}
					*/

					{
						const auto ppos = intToFloat(pos_int - m_camera_offset, BS);
						for (video::S3DVertex &vertex : m_vertices) {
							vertex.Pos += ppos;
						}
					}
				}

				{
					m_material.FogEnable = true;
					m_material.Thickness =
							// step_width;
							// BS * MAP_BLOCKSIZE;
							// BS * MAP_BLOCKSIZE * 100000;
							200;

					video::IVideoDriver *driver = SceneManager->getVideoDriver();
					driver->setMaterial(m_material);
					driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

					u16 indices[] = {0, 1, 2, 2, 3, 0};
					/*
						driver->drawVertexPrimitiveList(m_vertices, 4,
								indices, 2, video::EVT_STANDARD,
								scene::EPT_TRIANGLES,
								//scene::EPT_POINTS,
								//scene::EPT_POINT_SPRITES,
								video::EIT_16BIT);
					*/
					driver->drawVertexPrimitiveList(m_vertices, 4, indices, 1,
							video::EVT_STANDARD,
							// scene::EPT_TRIANGLES,
							// scene::EPT_POINTS,
							scene::EPT_POINT_SPRITES, video::EIT_16BIT);
				}
			}
			if (0)
				driver->draw3DBox(
						{intToFloat(pos_int - m_camera_offset, BS),
								intToFloat(v3pos_t(step_width, 0,
												   0 + step_width * !pos_int.Y) +
												   pos_int - m_camera_offset,
										BS)},

						irr::video::SColor(255,
								255 - 100 * (pos_int.Y < 0), /*- step_num*/
								255 * (m_water_level - 1 == pos_int.Y), color));
			if (0)
				driver->draw3DLine(intToFloat(pos_int - m_camera_offset, BS),
						intToFloat(v3pos_t(step_width, 0, 0 + step_width * !pos_int.Y) +
										   pos_int - m_camera_offset,
								BS),
						irr::video::SColor(255,
								255 - 100 * (pos_int.Y < 0), /*- step_num*/
								255 * (m_water_level - 1 == pos_int.Y), color));
		}
	}
	// auto tmesh = createHillPlaneMesh({10,10},{10,10},&material,1,{1,1},{1,1});
	// driver->drawMeshBuffer(tmesh->getMeshBuffer(0));
	// errorstream << "mesh use  " << (long)mesh << std::endl;

	//
}
#endif
