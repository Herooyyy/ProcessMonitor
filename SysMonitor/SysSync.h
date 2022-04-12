#include "pch.h"

template<typename TLock>
class AutoLock {
public:
	AutoLock(TLock& lock) : _lock(lock) {
		lock.Lock();
	}

	~AutoLock() {
		_lock.Unlock();
	}

private:
	TLock& _lock;
};

// Synchronization
struct FastMutex {
	void Init() {
		ExInitializeFastMutex(&_fastMutex);
	}

	void Lock() {
		ExAcquireFastMutex(&_fastMutex);
	}

	void Unlock() {
		ExReleaseFastMutex(&_fastMutex);
	}

private:
	FAST_MUTEX _fastMutex;
};

struct SpinLock {
	void Init() {
		KeInitializeSpinLock(&_spinLock);
	}

	void Lock() {
		KeAcquireSpinLockAtDpcLevel(&_spinLock);
	}

	void Unlock() {
		KeReleaseSpinLockFromDpcLevel(&_spinLock);
	}

private:
	KSPIN_LOCK _spinLock;
};