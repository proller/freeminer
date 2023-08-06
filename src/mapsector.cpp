/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

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

//not used minetest shit
#if 0

#include "mapsector.h"
#include "exceptions.h"
#include "mapblock.h"
#include "serialization.h"

MapSector::MapSector(Map *parent, v2bpos_t pos, IGameDef *gamedef):
		m_parent(parent),
		m_pos(pos),
		m_gamedef(gamedef)
{
}

MapSector::~MapSector()
{
	deleteBlocks();
}

void MapSector::deleteBlocks()
{
	// Clear cache
	m_block_cache = nullptr;

	// Delete all
	for (auto &block : m_blocks) {
		delete block.second;
	}

	// Clear container
	m_blocks.clear();
}

MapBlock * MapSector::getBlockBuffered(bpos_t y)
{
	MapBlock *block;

	if (m_block_cache && y == m_block_cache_y) {
		return m_block_cache;
	}

	// If block doesn't exist, return NULL
	std::unordered_map<bpos_t, MapBlock*>::const_iterator n = m_blocks.find(y);
	block = (n != m_blocks.end() ? n->second : nullptr);

	// Cache the last result
	m_block_cache_y = y;
	m_block_cache = block;

	return block;
}

MapBlock * MapSector::getBlockNoCreateNoEx(bpos_t y)
{
	return getBlockBuffered(y);
}

MapBlock * MapSector::createBlankBlockNoInsert(bpos_t y)
{
	assert(getBlockBuffered(y) == NULL);	// Pre-condition

	v3bpos_t blockpos_map(m_pos.X, y, m_pos.Y);

	MapBlock *block = new MapBlock(m_parent, blockpos_map, m_gamedef);

	return block;
}

MapBlock * MapSector::createBlankBlock(bpos_t y)
{
	MapBlock *block = createBlankBlockNoInsert(y);

	m_blocks[y] = block;

	return block;
}

void MapSector::insertBlock(MapBlock *block)
{
	bpos_t block_y = block->getPos().Y;

	MapBlock *block2 = getBlockBuffered(block_y);
	if (block2) {
		throw AlreadyExistsException("Block already exists");
	}

	v2bpos_t p2d(block->getPos().X, block->getPos().Z);
	assert(p2d == m_pos);

	// Insert into container
	m_blocks[block_y] = block;
}

void MapSector::deleteBlock(MapBlock *block)
{
	detachBlock(block);
	delete block;
}

void MapSector::detachBlock(MapBlock *block)
{
	bpos_t block_y = block->getPos().Y;

	// Clear from cache
	m_block_cache = nullptr;

	// Remove from container
	m_blocks.erase(block_y);

	// Mark as removed
	block->makeOrphan();
}

void MapSector::getBlocks(MapBlockVect &dest)
{
	dest.reserve(dest.size() + m_blocks.size());
	for (auto &block : m_blocks) {
		dest.push_back(block.second);
	}
}
#endif
