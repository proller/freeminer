/*
Copyright (C) 2024 proller <proler@gmail.com>
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

#pragma once

#include <optional>
#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "mapblock.h"

struct MapDrawControl;

namespace farmesh
{

#if USE_POS32
constexpr uint16_t tree_pow_default = FARMESH_STEP_MAX;
#else
constexpr uint16_t tree_pow_default = 12;
#endif

struct tree_params
{
	const uint16_t tree_pow = tree_pow_default;
	const uint16_t tree_size = 1 << tree_pow;
	const uint16_t tree_align = tree_pow - 1;
	const uint16_t tree_align_size = 1 << (tree_align);
	const uint16_t external_pow = tree_pow - 2;
};

struct tree_result_t
{
	v3bpos_t pos;
	bpos_t size;
	block_step_t step;
};

block_step_t getLodStep(const MapDrawControl &draw_control,
		const v3bpos_t &player_block_pos, const v3bpos_t &block_pos, const pos_t speedf);

block_step_t getFarStep(const MapDrawControl &draw_control,
		const v3bpos_t &player_block_pos, const v3bpos_t &block_pos,
		bool cell_each = false);

block_step_t getFarStepBad(const MapDrawControl &draw_control,
		const v3bpos_t &player_block_pos, const v3bpos_t &block_pos);

bool inFarGrid(const MapDrawControl &draw_control, const v3bpos_t &player_block_pos,
		const v3bpos_t &blockpos, const block_step_t step, const bool cell_each = false);

std::optional<tree_result_t> getFarParams(const MapDrawControl &draw_control,
		const v3bpos_t &player_block_pos, const v3bpos_t &blockpos,
		bool cell_each = false);

v3bpos_t getFarActualBlockPos(const MapDrawControl &draw_control,
		const v3bpos_t &player_block_pos, const v3bpos_t &blockpos,
		bool cell_each = false);

v3bpos_t playerBlockAlign(
		const MapDrawControl &draw_control, const v3bpos_t &player_block_pos);

void runFarAll(const v3bpos_t &player_block_pos, uint8_t cell_size_pow, int farmesh,
		uint8_t farmesh_quality_pow, pos_t two_d, bool cell_each,
		const std::function<bool(const v3bpos_t &, const bpos_t &, const block_step_t &)>
				&func);

uint8_t rangeToStep(const int range);
}