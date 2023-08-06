/*
nodetimer.h
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

#pragma once

#include "irr_v3d.h"
#include <iostream>
#include <map>
#include <vector>

/*
	NodeTimer provides per-node timed callback functionality.
	Can be used for:
	- Furnaces, to keep the fire burnin'
	- "activated" nodes that snap back to their original state
	  after a fixed amount of time (mesecons buttons, for example)
*/

class NodeTimer
{
public:
	NodeTimer() = default;
	NodeTimer(const v3pos_t &position_):
		position(position_) {}
	NodeTimer(f32 timeout_, f32 elapsed_, v3pos_t position_):
		timeout(timeout_), elapsed(elapsed_), position(position_) {}
	~NodeTimer() = default;

	void serialize(std::ostream &os) const;
	void deSerialize(std::istream &is);

	f32 timeout = 0.0f;
	f32 elapsed = 0.0f;
	v3pos_t position;
};

/*
	List of timers of all the nodes of a block
*/

class NodeTimerList
{
public:
	NodeTimerList() = default;
	~NodeTimerList() = default;

	void serialize(std::ostream &os, u8 map_format_version) const;
	void deSerialize(std::istream &is, u8 map_format_version);

	// Get timer
	NodeTimer get(const v3pos_t &p) {
		std::map<v3pos_t, std::multimap<double, NodeTimer>::iterator>::iterator n =
			m_iterators.find(p);
		if (n == m_iterators.end())
			return NodeTimer();
		NodeTimer t = n->second->second;
		t.elapsed = t.timeout - (n->second->first - m_time);
		return t;
	}
	// Deletes timer
	void remove(v3pos_t p) {
		std::map<v3pos_t, std::multimap<double, NodeTimer>::iterator>::iterator n =
			m_iterators.find(p);
		if(n != m_iterators.end()) {
			double removed_time = n->second->first;
			m_timers.erase(n->second);
			m_iterators.erase(n);
			// Yes, this is float equality, but it is not a problem
			// since we only test equality of floats as an ordered type
			// and thus we never lose precision
			if (removed_time == m_next_trigger_time) {
				if (m_timers.empty())
					m_next_trigger_time = -1.;
				else
					m_next_trigger_time = m_timers.begin()->first;
			}
		}
	}
	// Undefined behavior if there already is a timer
	void insert(const NodeTimer &timer) {
		v3pos_t p = timer.position;
		double trigger_time = m_time + (double)(timer.timeout - timer.elapsed);
		std::multimap<double, NodeTimer>::iterator it = m_timers.emplace(trigger_time, timer);
		m_iterators.emplace(p, it);
		if (m_next_trigger_time == -1. || trigger_time < m_next_trigger_time)
			m_next_trigger_time = trigger_time;
	}
	// Deletes old timer and sets a new one
	inline void set(const NodeTimer &timer) {
		remove(timer.position);
		insert(timer);
	}
	// Deletes all timers
	void clear() {
		m_timers.clear();
		m_iterators.clear();
		m_next_trigger_time = -1.;
	}

	double m_uptime_last = 0;

	// Move forward in time, returns elapsed timers
	std::vector<NodeTimer> step(float dtime);

private:
	std::multimap<double, NodeTimer> m_timers;
	std::map<v3pos_t, std::multimap<double, NodeTimer>::iterator> m_iterators;
	double m_next_trigger_time = -1.0;
	double m_time = 0.0;
};
