#include "fm_farmesh.h"

#include "client/client.h"
#include "client/clientmap.h"
#include "client/mapblock_mesh.h"
#include "emerge.h"
#include "mapnode.h"
#include "server.h"
#include "util/directiontables.h"
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
const MapNode &FarContainer::getNodeRefUnsafe(const v3pos_t &p)
{
	const auto &v = m_mg->visible_content(p);
	if (v.getContent())
		return v;
	return m_mg->visible_transparent;
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
	const auto step = getFarmeshStep(m_client->getEnv().getClientMap().getControl(),
			getNodeBlockPos(m_camera_pos_aligned), blockpos);

	const auto blockpos_actual = getFarmeshActual(blockpos, step);

	auto &far_blocks = m_client->getEnv().getClientMap().m_far_blocks;
	{
		const auto lock = far_blocks.lock_unique_rec();
		if (!far_blocks.contains(blockpos_actual)) {
			far_blocks.emplace(blockpos_actual,
					std::make_shared<MapBlock>(
							&m_client->getEnv().getClientMap(), blockpos, m_client));
		}
	}
	const auto &block = far_blocks.at(blockpos_actual);
	const auto lock = block->lock_unique();
	block->setTimestamp(m_client->m_uptime);
	if (!block->getFarMesh(step)) {
		MeshMakeData mdat(m_client, false, 0, step, &farcontainer);
		mdat.block = block.get();
		mdat.m_blockpos = blockpos_actual;
		auto mbmsh = std::make_shared<MapBlockMesh>(&mdat, m_camera_offset);
		block->setFarMesh(mbmsh);
	}
}

void FarMesh::makeFarBlock7(const v3bpos_t &blockpos, size_t step)
{
	for (const auto &dir : g_7dirs) {
		makeFarBlock(blockpos + dir * step);
	}
}

#if 0
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
#endif

FarMesh::FarMesh(Client *client, Server *server) :
		m_camera_pos{-1337, -1337, -1337}, m_client{client}

{

	EmergeManager *emerge_use = server			   ? server->getEmergeManager()
								: client->m_emerge ? client->m_emerge
												   : nullptr;
	DUMP((long)emerge_use, (long)(server), (long)client->m_emerge,
			(long)emerge_use->mgparams);

	DUMP((long)emerge_use);
	if (emerge_use) {
		if (emerge_use->mgparams)
			mg = emerge_use->getFirstMapgen();

		farcontainer.m_mg = mg;
		const auto &ndef = m_client->getNodeDefManager();
		mg->visible_surface = ndef->getId("default:stone");
		mg->visible_water = ndef->getId("default:water_source");
		mg->visible_ice = ndef->getId("default:ice");
		mg->visible_surface_green = ndef->getId("default:dirt_with_grass");
		mg->visible_surface_dry = ndef->getId("default:dirt_with_dry_grass");
		mg->visible_surface_cold = ndef->getId("default:dirt_with_snow");
		mg->visible_surface_hot = ndef->getId("default:sand");
	}

	//for (size_t i = 0; i < process_order.size(); ++i)
	//	process_order[i] = i;
	//auto rng = std::default_random_engine{};
	//std::shuffle(std::begin(process_order), std::end(process_order), rng);
}

FarMesh::~FarMesh()
{
}

int FarMesh::go_direction(const size_t dir_n)
{
	constexpr auto block_step_reduce = 1;
	constexpr auto align_reduce = 1;

	auto &cache = direction_caches[dir_n];
	auto &mg_cache = mg_caches[dir_n];

	auto &draw_control = m_client->getEnv().getClientMap().getControl();

	const auto dir = g_6dirsf[dir_n];
	const auto grid_size_xy = grid_size_x * grid_size_y;

	int processed = 0;
	for (uint16_t i = 0; i < grid_size_xy; ++i) {

		auto &ray_cache = cache[i];
		if (ray_cache.finished > last_distance_max)
			continue;
		//uint16_t y = uint16_t(process_order[i] / grid_size_x);
		//uint16_t x = process_order[i] % grid_size_x;
		uint16_t y = uint16_t(i / grid_size_x);
		uint16_t x = i % grid_size_x;

		v3f dir_first = dir * distance_min / 2;
		auto pos_center = dir_first + m_camera_pos;

		if (!dir.X)
			dir_first.X += distance_min / grid_size_x * (x - grid_size_x / 2);
		if (!dir.Y)
			dir_first.Y += distance_min / grid_size_x * (y - grid_size_x / 2);
		if (!dir.Z)
			dir_first.Z +=
					distance_min / grid_size_x * ((!dir.Y ? x : y) - grid_size_x / 2);

		v3f dir_l = dir_first.normalize();

		v3f pos_last = pos_center;
		for (size_t steps = 0; steps < 20; ++ray_cache.step_num, ++steps) {

			const auto dstep = ray_cache.step_num; // + 1;
			const auto block_step =
					getFarmeshStep(draw_control, m_camera_pos_aligned / MAP_BLOCKSIZE,
							floatToInt(pos_last, BS) / MAP_BLOCKSIZE);
			const auto step_width =
					MAP_BLOCKSIZE * pow(2, block_step - block_step_reduce);

			unsigned int depth = distance_min + step_width * dstep;

			if (depth > last_distance_max) {
				ray_cache.finished = distance_min + step_width * (dstep - 1);
				break;
			}

			const auto pos = dir_l * depth * BS + m_camera_pos;
			pos_last = pos;
			if (pos.X > MAX_MAP_GENERATION_LIMIT * BS ||
					pos.X < -MAX_MAP_GENERATION_LIMIT * BS ||
					pos.Y > MAX_MAP_GENERATION_LIMIT * BS ||
					pos.Y < -MAX_MAP_GENERATION_LIMIT * BS ||
					pos.Z > MAX_MAP_GENERATION_LIMIT * BS ||
					pos.Z < -MAX_MAP_GENERATION_LIMIT * BS) {
				ray_cache.finished = -1;
				break;
			}
			++processed;

			const int step_aligned =
					pow(2, ceil(log(step_width) / log(2)) - align_reduce);

			v3pos_t pos_int_raw = floatToInt(pos, BS);
			v3pos_t pos_int((pos_int_raw.X / step_aligned) * step_aligned,
					(pos_int_raw.Y / step_aligned) * step_aligned,
					(pos_int_raw.Z / step_aligned) * step_aligned);

			auto &visible = ray_cache.visible;

			{
				if (const auto &it = mg_cache.find(pos_int); it != mg_cache.end()) {
					visible = it->second;
				} else {

					visible = mg->visible(pos_int);
					mg_cache[pos_int] = visible;
				}
			}
			if (visible) {
				ray_cache.finished = -1;
				const auto blockpos = getNodeBlockPos(pos_int);
				makeFarBlock7(blockpos, pow(2, block_step));
				break;
			}
		}
	}

	return processed;
}

void FarMesh::update(v3f camera_pos, v3f camera_dir, f32 camera_fov,
		CameraMode camera_mode, f32 camera_pitch, f32 camera_yaw, v3pos_t camera_offset,
		float brightness, int render_range, float speed)
{
	if (!mg)
		return;

	auto camera_pos_aligned_int = floatToInt(
			intToFloat(floatToInt(camera_pos, BS * 16), BS * 16), BS); // todo optimize
	const auto distance_max =
			(std::min<unsigned int>(render_range, 1.2 * m_client->fog_range / BS) >> 7)
			<< 7;

	if (!timestamp_complete) {
		if (!m_camera_pos_aligned.X && !m_camera_pos_aligned.Y && !m_camera_pos_aligned.Z)
			m_camera_pos_aligned = camera_pos_aligned_int;
		m_client->getEnv().getClientMap().m_far_blocks_last_cam_pos =
				m_camera_pos_aligned;
		if (!last_distance_max)
			last_distance_max = distance_max;
	}

	m_camera_pos = intToFloat(m_camera_pos_aligned, BS);

	m_camera_dir = camera_dir;
	m_camera_fov = camera_fov;
	m_camera_pitch = camera_pitch;
	m_camera_yaw = camera_yaw;
	m_camera_offset = camera_offset;
	m_speed = speed;
	if (direction_caches_pos != m_camera_pos_aligned && !planes_processed_last) {
		timestamp_clean = m_client->m_uptime - 1;
		direction_caches_pos = m_camera_pos_aligned;
		direction_caches.fill({});
		plane_processed.fill({});
	} else if (last_distance_max < distance_max) {
		plane_processed.fill({});
		last_distance_max = distance_max; // * 1.1;
	}

	{
		size_t planes_processed = 0;
		for (size_t i = 0; i < sizeof(g_6dirsf) / sizeof(g_6dirsf[0]); ++i) {
			if (!plane_processed[i].processed)
				continue;
			++planes_processed;
			async[i].step([this, i = i]() {
				for (int depth = 0; depth < 100; ++depth) {
					plane_processed[i].processed = go_direction(i);
					if (!plane_processed[i].processed)
						break;
				}
			});
		}
		planes_processed_last = planes_processed;

		if (planes_processed) {
			complete_set = false;
		} else if (m_camera_pos_aligned != camera_pos_aligned_int) {
			m_client->getEnv().getClientMap().m_far_blocks_last_cam_pos =
					m_camera_pos_aligned;
			m_camera_pos_aligned = camera_pos_aligned_int;
		}
		if (!planes_processed && !complete_set) {
			constexpr auto clean_old_time = 300;
			if (timestamp_complete - clean_old_time > 0)
				m_client->getEnv().getClientMap().m_far_blocks_clean_timestamp =
						timestamp_complete - clean_old_time;
			timestamp_complete = m_client->m_uptime;
			complete_set = true;
		}
	}
}
