// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// ghostmodeenabled / ghostmodefornotstaff policy and SettingCache (#97 follow-up).
#include "catch2/catch_all.hpp"

#include <BabyDI.h>
#include <Server.h>
#include <utilities/GhostMode.h>

using namespace preagonal;

TEST_CASE("ghost-mode keys are tracked", "[ghostmode][serveroptions]")
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

TEST_CASE("ghost-mode appearance matrix", "[ghostmode][serveroptions]")
{
	SECTION("disabled means nobody")
	{
		CHECK_FALSE(playerAppearsAsGhost(false, false, false));
		CHECK_FALSE(playerAppearsAsGhost(false, false, true));
		CHECK_FALSE(playerAppearsAsGhost(false, true, false));
		CHECK_FALSE(playerAppearsAsGhost(false, true, true));
	}

	SECTION("enabled + fornotstaff: only non-staff")
	{
		CHECK(playerAppearsAsGhost(true, true, false));
		CHECK_FALSE(playerAppearsAsGhost(true, true, true));
	}

	SECTION("enabled without fornotstaff: everybody")
	{
		CHECK(playerAppearsAsGhost(true, false, false));
		CHECK(playerAppearsAsGhost(true, false, true));
	}
}

TEST_CASE("ghost appearance uses ghostani gani name", "[ghostmode][serveroptions]")
{
	CHECK(GhostAppearanceGani == "ghostani");
	CHECK_FALSE(GhostAppearanceGani.ends_with(".gani"));
}
