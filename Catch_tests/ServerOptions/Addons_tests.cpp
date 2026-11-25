// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// The add-on hooks (addons/ServerAddon.h) leave the core as it is when no add-on
// runs, whether or not any is compiled in.
#include "catch2/catch_all.hpp"

#include <string_view>

#include <BabyDI.h>
#include <IEnums.h>
#include <IUtil.h>
#include <Server.h>

using namespace preagonal;

TEST_CASE("with no add-on running, every hook is the core's behaviour", "[addons][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("addons-none"));
	server->prepareSettings();

	// Every compiled-in add-on is off by default.
	server->getAddons().start(*server, "");
	const auto& addons = server->getAddons();
	CHECK(addons.empty());
	CHECK_FALSE(addons.listHqLevel().has_value());

	for (const int v : {CLVER_2_31, CLVER_3_153, CLVER_5_12, CLVER_6_037})
	{
		const auto needs = addons.clientNeeds(v);
		CHECK_FALSE(needs.answerMissingChecksumFiles);
		CHECK_FALSE(needs.alwaysServerListConnected);
		CHECK(needs.bareNameExtensions.empty());
	}

	BabyDI_RELEASE(Server);
}

TEST_CASE("add-on client needs combine", "[addons]")
{
	static constexpr const char* exts[] = {".png"};
	AddonClientNeeds a;
	AddonClientNeeds b;
	b.alwaysServerListConnected = true;
	b.bareNameExtensions = exts;
	a |= b;
	CHECK(a.alwaysServerListConnected);
	CHECK_FALSE(a.answerMissingChecksumFiles);
	REQUIRE(a.bareNameExtensions.size() == 1);
	CHECK(std::string_view{a.bareNameExtensions[0]} == ".png");
}
