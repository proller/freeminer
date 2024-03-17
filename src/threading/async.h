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
