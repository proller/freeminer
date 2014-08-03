#ifndef UTIL_THREAD_POOL_HEADER
#define UTIL_THREAD_POOL_HEADER

#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <condition_variable>

class thread_pool {
public:

	std::mutex queue_mutex;
	std::condition_variable condition;
	std::vector<std::thread> workers;
	std::atomic_bool requeststop;

	thread_pool() {
		requeststop = false;
	};
	virtual ~thread_pool() {
		join();
	};

	virtual void func() {
		Thread();
	};

	void start (int n = 1) {
		for(int i = 0; i < n; ++i) {
			workers.emplace_back(std::thread(&thread_pool::func, this));
		}
	}
	void stop () {
		requeststop = true;
	}
	void join () {
		stop();
		for (auto & worker : workers) {
			worker.join();
		}
		workers.clear();
	}

// JThread compat:
	void ThreadStarted(){};
	inline bool StopRequested()
		{ return requeststop; }
	inline bool IsRunning()
		{ return !workers.empty(); }
	int Start(int n = 1){ start(n); return 0; };
	inline void Stop()
		{ stop(); }
	void Wait(){ join(); };
	void Kill(){ join(); };
	virtual void * Thread() { return nullptr; };
};


#endif
