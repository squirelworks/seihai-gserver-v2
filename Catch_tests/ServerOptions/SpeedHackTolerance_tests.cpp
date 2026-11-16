// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// speedhacktolerance stub (#97).
#include "catch2/catch_all.hpp"

#include <atomic>
#include <cstdint>

#include <BabyDI.h>
#include <Server.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

TEST_CASE("speedhacktolerance is a tracked stub", "[speedhacktolerance][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("speedhack"));
	server->prepareSettings();

	CHECK(server->cached.speedHackTolerance.key == "speedhacktolerance");
	CHECK(server->cached.speedHackTolerance.getValue() == 90);

	server->getSettings().set("speedhacktolerance", static_cast<uint16_t>(50));
	CHECK(server->cached.speedHackTolerance.getValue() == 50);
}
