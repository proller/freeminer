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
	bool requeststop;

	thread_pool() {
		requeststop = false;
	};
	~thread_pool(){
		stop();
	};

	virtual void func() {
		Thread();
	};

	void start (unsigned int n = 1) {
		for(unsigned int i = 0; i < n; ++i) {
			workers.emplace_back(std::thread(&thread_pool::func, this));
		} 
	}
	void stop () {
		requeststop = true;
		join();
		workers.clear();
	}
	void join () {
		for (auto & worker : workers) {
			worker.join();
		}
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
	virtual void * Thread() { return nullptr;};
};


#endif
