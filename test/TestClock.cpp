#include "TestClock.h"

#include <atomic>
#include <chrono>

#include "raknet/GetTime.h"

namespace
{
	std::atomic<unsigned int> offset{0};

	unsigned int steadyMilliseconds()
	{
		auto since = std::chrono::steady_clock::now().time_since_epoch();
		return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(since).count());
	}
}

// This definition replaces the one in the raknet library. The linker takes a member of a static
// library only for symbols that are still missing, and GetTime.cpp defines nothing else.
unsigned int RakNet::GetTime()
{
	return steadyMilliseconds() + offset.load();
}

void setRakNetTime(unsigned int now)
{
	offset.store(now - steadyMilliseconds());
}

void advanceRakNetTime(unsigned int ms)
{
	offset.fetch_add(ms);
}

void resetRakNetTime()
{
	offset.store(0);
}
