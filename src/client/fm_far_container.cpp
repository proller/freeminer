#include "fm_far_container.h"
#include "client.h"
#include "client/clientmap.h"
#include "database/database.h"
#include "fm_far_calc.h"
#include "irr_v3d.h"
#include "mapblock.h"
#include "mapgen/mapgen.h"
#include "mapnode.h"
#include "server.h"

FarContainer::FarContainer(Client *client) : m_client{client}
{
}

namespace
{
thread_local MapBlockPtr block_cache{};
thread_local std::pair<block_step_t, v3bpos_t> block_cache_p;
}

const MapNode &FarContainer::getNodeRefUnsafe(const v3pos_t &pos)
{
	const auto block_pos = getNodeBlockPos(pos);
	auto &client_map = m_client->getEnv().getClientMap();
	const auto player_block_pos = getNodeBlockPos(client_map.far_blocks_last_cam_pos);
	const auto &control = client_map.getControl();

	const auto tree_result = farmesh::getFarParams(control, player_block_pos, block_pos, true);
	if (tree_result) {
	const auto &step = tree_result->step;
	const v3bpos_t &bpos_aligned = tree_result->pos;

	MapBlockPtr block;
	const auto step_block_pos = std::make_pair(step, bpos_aligned);
	if (block_cache && step_block_pos == block_cache_p) {
		block = block_cache;
	}

	if (!block && step < FARMESH_STEP_MAX) {
		const auto &storage = client_map.far_blocks_storage[step];
		block = storage.get(bpos_aligned).block;
	}

	const auto loadBlock = [this, &client_map](
								   const auto &bpos, const auto step) -> MapBlockPtr {
		auto *dbase =
				GetFarDatabase({}, m_client->far_dbases, m_client->m_world_path, step);
		if (!dbase) {
			return {};
		}
		MapBlockPtr block = client_map.createBlankBlockNoInsert(bpos);

		std::string blob;
		dbase->loadBlock(bpos, &blob);
		if (!blob.length()) {
			return {};
		}

		std::istringstream is(blob, std::ios_base::binary);

		u8 version = SER_FMT_VER_INVALID;
		is.read((char *)&version, 1);

		if (is.fail()) {
			return {};
		}

		// Read basic data
		if (!block->deSerialize(is, version, true)) {
			return {};
		}
		return block;
	};

	if (!block && !m_client->m_simple_singleplayer_mode &&
			!m_client->far_container.have_params) {
		thread_local static std::array<std::unordered_set<v3bpos_t>, FARMESH_STEP_MAX>
				miss_cache;
		if (!miss_cache[step].contains(bpos_aligned)) {
			block = loadBlock(bpos_aligned, step);
			if (!block) {
				miss_cache[step].emplace(bpos_aligned);
			}
		}
	}

	if (block) {
		block_cache_p = step_block_pos;
		block_cache = block;

		const v3pos_t relpos = pos - bpos_aligned * MAP_BLOCKSIZE;

		const auto &relpos_shift = step;
		const auto relpos_shifted = v3pos_t{relpos.X >> relpos_shift,
				relpos.Y >> relpos_shift, relpos.Z >> relpos_shift};
		const auto &n = block->getNodeNoLock(relpos_shifted);
		if (n.getContent() != CONTENT_IGNORE) {
			return n;
		}
	}
	}
	if (const auto &v = m_mg->visible_content(pos, use_weather); v.getContent()) {
		return v;
	}

	return m_mg->visible_transparent;
};
