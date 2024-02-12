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

#include <string>
#include <unordered_map>

#include "key_value_storage.h"

class Stat
{
public:
	using stat_value = float;

	KeyValueStorage database;
	std::unordered_map<std::string, stat_value> stats; // todo: make shared

	std::string day, week, month;

	Stat(const std::string &savedir);
	~Stat();

	void save();
	void unload();
	void open();
	void close();

	const stat_value &get(const std::string &key);
	const stat_value &write_one(const std::string &key, const stat_value &value);
	const stat_value &add(
			const std::string &key, const std::string &player = "", stat_value value = 1);

	void update_time();

private:
	std::mutex mutex;
};
