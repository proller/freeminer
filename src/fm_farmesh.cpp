
#include "fm_farmesh.h"
#include "client.h"
#include "clientmap.h"
#include "constants.h"
#include "emerge.h"
#include "enet/types.h"
#include "irr_v3d.h"
#include "log.h"
#include "log_types.h"
#include "map.h"
#include "mapgen.h"
#include "mapnode.h"
#include "nodedef.h"
#include "server.h"
#include "util/numeric.h"
#include "util/timetaker.h"
#include "util/unordered_map_hash.h"
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>

FarMesh::FarMesh(scene::ISceneNode *parent, scene::ISceneManager *mgr, s32 id,
		// u64 seed,
		Client *client, Server *server) :
		scene::ISceneNode(parent, mgr, id),
		// m_seed(seed),
		m_camera_pos(0, 0, 0),
		// m_time(0),
		m_client(client) //,
						 // m_render_range(20*MAP_BLOCKSIZE)
{
	// dstream<<__FUNCTION_NAME<<std::endl;

	// video::IVideoDriver* driver = mgr->getVideoDriver();

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

	m_render_range_max = g_settings->getS32("farmesh5");

	//  m_box = core::aabbox3d<f32>(-BS * MAX_MAP_GENERATION_LIMIT,-BS *
	//  MAX_MAP_GENERATION_LIMIT,-BS * MAX_MAP_GENERATION_LIMIT, BS * 1000000, BS
	//  * MAX_MAP_GENERATION_LIMIT, BS * MAX_MAP_GENERATION_LIMIT);
	m_box = core::aabbox3d<f32>(-BS * m_render_range_max, -BS * m_render_range_max,
			-BS * m_render_range_max, BS * m_render_range_max, BS * m_render_range_max,
			BS * m_render_range_max);

	Settings user_settings;
	std::string test_mapmeta_path = ""; // makeMetaFile(false);
	MapSettingsManager map_settings_manager(&user_settings, test_mapmeta_path);

	Server *server_use = server					 ? server
						 : client->m_localserver ? client->m_localserver
												 : nullptr;
	if (server_use) {
		errorstream << "Farmesh: mgtype="
					<< server_use->getEmergeManager()->mgparams->mgtype << " water_level="
					<< server_use->getEmergeManager()->mgparams->water_level
					<< " render_range_max=" << m_render_range_max << "\n";
		mg = Mapgen::createMapgen(server_use->getEmergeManager()->mgparams->mgtype, 0,
				server_use->getEmergeManager()->mgparams, server_use->getEmergeManager());
		m_water_level = server_use->getEmergeManager()->mgparams->water_level;
	}

	for (size_t i = 0; i < process_order.size(); ++i)
		process_order[i] = i;
	auto rng = std::default_random_engine{};
	std::shuffle(std::begin(process_order), std::end(process_order), rng);
}

FarMesh::~FarMesh()
{
	// dstream<<__FUNCTION_NAME<<std::endl;
}

void FarMesh::OnRegisterSceneNode()
{
	if (IsVisible) {
		// SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
		// SceneManager->registerNodeForRendering(this, scene::ESNRP_SKY_BOX);
	}

	ISceneNode::OnRegisterSceneNode();
}

void FarMesh::update(v3f camera_pos, v3f camera_dir, f32 camera_fov,
		CameraMode camera_mode, f32 camera_pitch, f32 camera_yaw, v3POS camera_offset,
		float brightness, s16 render_range)
{

	m_camera_pos = camera_pos;

#if cache_step
	m_camera_pos_aligned_by_step.clear();
	m_camera_pos_aligned_by_step.reserve(256); // todo calc from grid_size?
#endif
#if cache0
	m_camera_pos_aligned =
			floatToInt(intToFloat(floatToInt(m_camera_pos, BS * 16), BS * 16),
					BS); // todo optimize
#endif

	m_camera_dir = camera_dir;
	m_camera_fov = camera_fov;
	m_camera_pitch = camera_pitch;
	m_camera_yaw = camera_yaw;
	m_camera_offset = camera_offset;
	m_render_range = std::min<POS>(render_range, 255);

	/*
   errorstream << "update pos=" << m_camera_pos << " dir=" << m_camera_dir
			   << " fov=" << m_camera_fov << " pitch=" << m_camera_pitch
			   << " yaw=" << m_camera_yaw << " render_range=" << m_render_range
			   << std::endl;
	*/

	// const auto start = 2 << 7;

	// auto from_center = m_camera_pos + m_camera_dir /** BS */ * start;

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

	const int depth_steps = 512 + 100; // TODO CALC  256+128+64+32+16+8+4+2+1+?
	// const int grid_size = 64;		   // 255;
	const int depth_max = m_render_range_max;
	// const int grid_size = 64; // 255;
	//  unordered_map_v2POS<std::pair<bool, v3POS>> grid_cache;

	TimeTaker timer_step("Client: Farmesh");

	uint32_t stat_probes = 0, stat_cached = 0, stat_mg = 0, stat_occluded = 0,
			 stat_plane_cache = 0, stat_cache_used = 0, stat_cache_sky = 0;

	INodeDefManager *nodemgr = m_client->getNodeDefManager();

	float step = BS * 1;
	float stepfac = 1.2;
	float startoff = BS * 1;
	float endoff = -MAP_BLOCKSIZE;
	unordered_map_v3POS<bool> occlude_cache;
	const auto cache_time = m_client->getEnv().getTimeOfDay();

	const int max_cycle_ms = 20;
	u32 end_ms = porting::getTimeMs() + max_cycle_ms;

	/*
		for (short y = m_cycle_stop_y; y < grid_size; y += skip_y_num) {
			// errorstream << "pos_l=" << pos_l << "\n";
			// constexpr short skip_x_num = 7;

			for (short x = m_cycle_stop_x; x < grid_size; x += skip_x_num) {
	*/
	for (uint16_t i = m_cycle_stop_i; i < grid_size * grid_size; ++i) {
		{
			uint16_t y = uint16_t(process_order[i] / grid_size);
			uint16_t x = process_order[i] % grid_size;
			// errorstream << " x=" << x << " y=" << y << " i=" << i << " v="<<
			// process_order[i] << "\n";

			if (porting::getTimeMs() > end_ms) {
				m_cycle_stop_i = i;
				// m_cycle_stop_x = x;
				/// m_cycle_stop_y = y;
				// grid_size = std::max<POS>(grid_size/2, grid_size_min);
				/*				errorstream << " stop "
											<< " m_cycle_stop_x=" << m_cycle_stop_x << "
				   x=" << x
											<< " m_cycle_stop_y=" << m_cycle_stop_y << "
				   y=" << y << "\n";
				*/
				return;
			}

			int depth = m_render_range /** BS*/; // 255;

			/*auto &cache = grid_cache[{x, y}];
			if (cache.first) {
			  ++stat_cached;
			  continue;
			}*/
#if cache0
			v3POS pos_int_0;
#endif
#if cache_step
			std::vector<v3POS> plane_cache_step_pos;
#endif
			for (short step_num = 0; step_num < depth_steps; ++step_num) {

				auto pos_ld = dir_ld * depth * BS + m_camera_pos;
				auto pos_rd = dir_rd * depth * BS + m_camera_pos;
				auto pos_lu = dir_lu * depth * BS + m_camera_pos;
				auto step_r = (pos_rd - pos_ld) / grid_size;
				auto step_u = (pos_lu - pos_ld) / grid_size;
				// auto step_width = std::max(1, int(step_r.getLength()) >> 1 << 1);
				auto step_width =
						std::min(std::max<int>(1, step_r.getLength() / BS), 1024);
				//#if cache0
				int step_aligned = pow(2, ceil(log(step_width) / log(2)));
				//#else
				// int step_aligned = pow(2, std::floor(log(step_width) / log(2)));
				//#endif

				depth += step_width * 8; // TODO: TUNE ME

				if (depth > depth_max)
					break;

				auto pos_l = pos_ld + (step_u * y);

				++stat_probes;
				auto pos = pos_l + (step_r * x);
				if (pos.X > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.X < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Y < -MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z > MAX_MAP_GENERATION_LIMIT * BS ||
						pos.Z < -MAX_MAP_GENERATION_LIMIT * BS)
					break;

				v3POS pos_int_raw = floatToInt(pos, BS);
				v3POS pos_int((pos_int_raw.X / step_aligned) * step_aligned,
						(pos_int_raw.Y / step_aligned) * step_aligned,
						(pos_int_raw.Z / step_aligned) * step_aligned);

				/*if (x == grid_size / 2 && y == grid_size / 2)
					errorstream << " step_num=" << step_num
								<< " step_width=" << step_width
								<< " step_aligned=" << step_aligned << " depth=" << depth
								<< " pos=" << pos << " pos_int=" << pos_int
								<< " pos_l=" << pos_l << " pos_ld=" << pos_ld << "\n";*/

				// if (step_num == 0) {
#if cache_step
				// m_camera_pos_aligned.reserve(step_num);
				// m_camera_pos_aligned[step_num] =
				// floatToInt(intToFloat(floatToInt(m_camera_pos, BS * step_aligned), BS *
				// step_aligned),	BS); // todo optimize
				if (step_num >= m_camera_pos_aligned_by_step.size())
					m_camera_pos_aligned_by_step.emplace_back(floatToInt(
							intToFloat(floatToInt(m_camera_pos, BS * step_aligned),
									BS * step_aligned),
							BS)); // todo optimize
				//}
				auto &plane_cache_item =
						plane_cache[m_camera_pos_aligned_by_step[step_num]];

				/*
								if (x == grid_size / 2 && y == grid_size / 2)
									errorstream << " planecache ini"
												<< " step_num=" << step_num
												<< " cpa=" <<
				   m_camera_pos_aligned[step_num]
												<< " pos_int=" << pos_int
												<< " step_aligned=" << step_aligned
												<< " m_camera_pos_aligned.size="
												<< m_camera_pos_aligned.size()
												<< " m_camera_pos_aligned0=" <<
				   m_camera_pos_aligned[0]
												<< " tm=" << plane_cache_item.time
												<< " sz=" << plane_cache_step_pos.size()
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

						/*						if (x == grid_size / 2 && y == grid_size /
						   2) errorstream
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
									intToFloat(v3POS(plane_cache_item.step_width[pos_int],
													   0, 0 + !pos_cached.Y) +
													   pos_cached - m_camera_offset,
											BS),
									irr::video::SColor(255 * (pos_cached.Y < 0), color,
											255 * !pos_cached.Y, 0));
*/
							/*
														if (x == grid_size / 2 && y ==
							   grid_size / 2) errorstream
																	<< " draw cached "
																	<<
							   intToFloat(pos_cached - m_camera_offset, BS)
																	<< " , "
																	<< intToFloat(
																			   v3POS(plane_cache_item
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
												if (x == grid_size / 2 && y == grid_size /
						   2) errorstream
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
												errorstream
														<< " planecache "
														<< " x=" << x << " y=" << y << "
						   i=" << i
														<< " v=" << process_order[i]
														<< " cpa=" << m_camera_pos_aligned
														<< " pos_int_0=" << pos_int_0 << "
						   => " << depth_cached
														<< " t=" <<
						   plane_cache[m_camera_pos_aligned].time
														<< " s1=" << plane_cache.size() <<
						   " s2="
														<<
						   plane_cache[m_camera_pos_aligned].depth.size() << "\n";
						*/

						if (!depth_cached)
							break;
						/*
												auto l = depth_cached /
						   (MAX_MAP_GENERATION_LIMIT * 2); uint8_t color = 255 * l;
												driver->draw3DLine(intToFloat(pos_cached -
						   m_camera_offset, BS),
														intToFloat(v3POS(plane_cache_item.step_width[pos_int_0],
																		   0, 0 +
						   !pos_cached.Y) + pos_cached - m_camera_offset, BS),
														irr::video::SColor(255 *
						   (pos_cached.Y < 0), color, 255 * !pos_cached.Y, 0));
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
												<< " cpa=" << m_camera_pos_aligned
												<< " pos_int=" << pos_int << " t=" <<
						   plane_cache[m_camera_pos_aligned].time
												<< " s1=" << plane_cache.size()
												<< " s2=" <<
						   plane_cache[m_camera_pos_aligned].depth.size()
												<< "\n";*/
						plane_cache_item.depth[pos_int_0] = 0;
					}

#endif
					// /*					 OK
					// TODO: test speed, really faster with?:
					if (isOccluded(&m_client->getEnv().getClientMap(),
								floatToInt(m_camera_pos, BS), pos_int, step, stepfac,
								startoff, endoff, 1, nodemgr, occlude_cache)) {
						++stat_occluded;
						break;
					}
					// */
				}
				/*
								if (x == grid_size / 2 && y == grid_size / 2) // if (!x
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
											  << " x=" << x << " y=" << y << " step_num="
					<< step_num
											  << " depth=" << depth << " pos=" << pos
											  << " pos_int=" << pos_int << " visible=" <<
					visible
											  << "\n";
		  */
				} else

						if (mg) {
					/*
				   if (x == grid_size / 2 && y == grid_size / 2) // if (!x && !y)
						   errorstream << "have mg"
												   << " x=" << x << " y=" << y
												   << " step_num=" << step_num << "
				   depth=" << depth
												   << " pos=" << pos << " pos_int=" <<
				   pos_int
												   << " visible=" << visible << "\n";
					*/
					if (const auto &it = mg_cache.find(pos_int); it != mg_cache.end()) {
						visible = it->second;
						++stat_cached;
						/*
					   if (
									   // visible ||
									   x == grid_size / 2 && y == grid_size / 2)
							   errorstream << "have mg cache"
													   << " x=" << x << " y=" << y
													   << " step_num=" << step_num << "
					   depth=" << depth
													   << " pos=" << pos << " pos_int=" <<
					   pos_int
													   << " visible=" << visible << "\n";
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
									   x == grid_size / 2 && y == grid_size / 2)
							   errorstream << "mg calc"
													   << " x=" << x << " y=" << y
													   << " step_num=" << step_num << "
					   depth=" << depth
													   << " pos=" << pos << " pos_int=" <<
					   pos_int
													   << " srl=" << step_r.getLength()
													   << " step_width=" << step_width
													   << " step_aligned=" << step_aligned

													   << " visible=" << visible << "\n";
						*/
					}
				}

				if (!visible) {

					/*
									   for (short step_old = 0; step_old <= step_num;
					   ++step_old) { auto &plane_cache_item_save =
												   plane_cache[m_camera_pos_aligned[step_old]];
										   plane_cache_item_save.depth[plane_cache_step_pos[step_old]]
					   = 0;
									   }*/

					continue;
				}

				/* // if (x == grid_size / 2 && y == grid_size / 2)
				errorstream << "draw "
							<< " x=" << x << " y=" << y << " step_num=" << step_num
							<< "depth = " << depth << " pos=" << pos
							<< " pos_int=" << pos_int
							<< " pos_int_float=" << intToFloat(pos_int, BS)
							<< "step_r=" << step_r << " step_width=" << step_width
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
										if (x == grid_size / 2 && y == grid_size / 2)
											errorstream << " plane cache set step_old=" <<
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
										<< pos_int_0 << " = " << depth << " s1=" <<
				   plane_cache.size()
										<< " s2=" <<
				   plane_cache[m_camera_pos_aligned].depth.size()
										<< "\n";*/

				// pos -= intToFloat(m_camera_offset, BS);
				//  driver->draw3DLine(pos, pos + v3f(step_width*BS, 0, 0),
				//  driver->draw3DLine(pos, intToFloat(pos_int - m_camera_offset, BS),

				(*grid_result_fill)[x][y].pos = pos_int;
				(*grid_result_fill)[x][y].depth = depth;
				(*grid_result_fill)[x][y].step_width = step_width;

				/*
								driver->draw3DLine(intToFloat(pos_int - m_camera_offset,
				   BS), intToFloat(v3POS(step_width, 0, 0 + step_width * !pos_int.Y) +
														   pos_int - m_camera_offset,
												BS),
										irr::video::SColor(255 * (pos_int.Y < 0), 255 -
				   step_num, 255 * !pos_int.Y, 0));
				*/
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
			<< " time=" << timer_step.getTimerTime() << " grid_size=" << grid_size
			<< " stat_probes=" << stat_probes << " stat_cached=" << stat_cached
			<< " stat_mg=" << stat_mg << " stat_occluded=" << stat_occluded
			<< " plane_cache=" << stat_plane_cache << " cache_used=" << stat_cache_used
			<< " cache_sky="
			<< stat_cache_sky
			//<< " mg_cache=" << mg_cache.size() << " cpa0=" <<
			// m_camera_pos_aligned_by_step[0]
			//<< " pcache=" << plane_cache[m_camera_pos_aligned_by_step[0]].depth.size()
			<< "\n";
*/
	// grid_result = std::move(grid_result_wip);
	// std::swap(grid_result_use, grid_result_fill);
}

void FarMesh::render()
{
	video::IVideoDriver *driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);
	driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
	irr::video::SMaterial material;
	material.Lighting = false;
	material.BackfaceCulling = false;
	material.ZBuffer = irr::video::ECFN_ALWAYS;
	driver->setMaterial(material);

	for (auto &ya : *grid_result_use) {
		for (auto &point : ya) {
			const auto &pos_int = point.pos;
			const auto step_width = point.step_width;
			const auto depth_cached = point.depth;

			auto l = depth_cached / (MAX_MAP_GENERATION_LIMIT * 2);
			uint8_t color = 255 * l;

			driver->draw3DLine(intToFloat(pos_int - m_camera_offset, BS),
					intToFloat(v3POS(step_width, 0, 0 + step_width * !pos_int.Y) +
									   pos_int - m_camera_offset,
							BS),
					irr::video::SColor(255, 255 - 100 * (pos_int.Y < 0), /*- step_num*/
							255 * (m_water_level == pos_int.Y), color));
		}
	}
}