#pragma once
#include <future>
#include <chrono>
#include "log.h"

class async_step_runner
{
	std::future<void> future;

public:
	~async_step_runner()
	{
		if (future.valid())
			future.wait_for(std::chrono::seconds(10));
	}

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
