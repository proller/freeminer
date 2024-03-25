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
#include <future>
#include <chrono>

class async_step_runner
{
	std::future<void> future;

public:
	~async_step_runner() { wait(); }
	int wait(const int ms = 10000, const int step_ms = 100)
	{
		int i = 0;
		for (; i < ms / step_ms; ++i) { // 10s max
			if (!valid())
				return i;
			future.wait_for(std::chrono::milliseconds(step_ms));
		}
		return i;
	}

	inline bool valid() { return future.valid(); }

	template <class Func, typename... Args>
	void step(Func func, Args &&...args)
	{
		if (future.valid()) {
			auto res = future.wait_for(std::chrono::milliseconds(0));
			if (res == std::future_status::timeout) {
				return;
			}
		}

		future = std::async(std::launch::async, func, std::forward<Args>(args)...);
	}
};
