

#include <utility>

template <class T>
class atomic_fake
{
	T obj;

public:
	// Constructors
	atomic_fake() = default;
	atomic_fake(T desired) : obj(desired) {}
	
	// Delete copy constructor and assignment to prevent accidental copying
	atomic_fake(const atomic_fake &) = delete;
	atomic_fake &operator=(const atomic_fake &) = delete;
	
	// Load operation
	T load() const { return obj; }
	
	// Store operation
	void store(T desired) { obj = desired; }
	
	// Exchange operation
	T exchange(T desired)
	{
		auto ret = obj;
		obj = desired;
		return std::move(ret);
	}
	
	// Assignment operator
	T operator=(T desired) { 
		obj = desired; 
		return desired; 
	}
	
	// Conversion operator
	operator T() const { return obj; }

};
