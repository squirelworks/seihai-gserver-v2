// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// ghostmodeenabled / ghostmodefornotstaff stubs (#97).
#include "catch2/catch_all.hpp"

#include <atomic>

#include <BabyDI.h>
#include <Server.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

TEST_CASE("ghost-mode keys are tracked stubs", "[ghostmode][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("ghostmode"));
	server->prepareSettings();

	CHECK(server->cached.ghostModeEnabled.key == "ghostmodeenabled");
	CHECK(server->cached.ghostModeForNonStaff.key == "ghostmodefornotstaff");
	CHECK(server->cached.ghostModeEnabled.getValue() == false);
	CHECK(server->cached.ghostModeForNonStaff.getValue() == false);

	server->getSettings().set("ghostmodeenabled", true);
	server->getSettings().set("ghostmodefornotstaff", true);
	CHECK(server->cached.ghostModeEnabled.getValue() == true);
	CHECK(server->cached.ghostModeForNonStaff.getValue() == true);
}
