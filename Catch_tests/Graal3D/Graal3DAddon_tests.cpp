// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// The Graal 3D side as an add-on: off unless `graal3d = true`, and then the core
// hooks give the 3.151 to 5.x clients what they need and list the server at HQ 4.
#include "catch2/catch_all.hpp"

#include <atomic>
#include <optional>

#include <BabyDI.h>
#include <IEnums.h>
#include <IUtil.h>
#include <Server.h>
#include <graal3d/Graal3DAddon.h>
#include <graal3d/Graal3DService.h>

using namespace preagonal;

// Server.cpp refers to it; a Debug link keeps the reference (main.cpp, which defines it, is not in the test).
std::atomic_bool shutdownProgram{false};

TEST_CASE("the Graal 3D add-on runs only with graal3d = true", "[graal3d][addons]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("graal3d-addon"));
	server->prepareSettings();

	SECTION("off by default")
	{
		CHECK(graal3d::Addon::create(*server) == nullptr);
		server->getAddons().start(*server, "");
		CHECK(server->getAddons().find<graal3d::Addon>() == nullptr);
	}

	SECTION("on")
	{
		server->getSettings().set("graal3d", true);
		// A loopback UDP port well away from the game port; the test does not send to it.
		server->getSettings().set("graal3dport", 47311);
		server->getAddons().start(*server, "127.0.0.1");
		auto* addon = server->getAddons().find<graal3d::Addon>();
		REQUIRE(addon != nullptr);
		CHECK(addon->service().port() == 47311);

		// A second start keeps the one already running.
		server->getAddons().start(*server, "127.0.0.1");
		CHECK(server->getAddons().find<graal3d::Addon>() == addon);

		CHECK(server->getAddons().listHqLevel() == std::optional<int>{4});

		for (const int v : {CLVER_3_151_LINUX, CLVER_3_153, CLVER_4_208, CLVER_5_12})
		{
			INFO("version " << v);
			const auto needs = server->getAddons().clientNeeds(v);
			CHECK(needs.answerMissingChecksumFiles);
			CHECK(needs.alwaysServerListConnected);
			CHECK_FALSE(needs.bareNameExtensions.empty());
		}
		for (const int v : {CLVER_2_31, CLVER_6_015, CLVER_6_037})
		{
			INFO("version " << v);
			const auto needs = server->getAddons().clientNeeds(v);
			CHECK_FALSE(needs.answerMissingChecksumFiles);
			CHECK_FALSE(needs.alwaysServerListConnected);
			CHECK(needs.bareNameExtensions.empty());
		}

		server->getAddons().clear();
		CHECK(server->getAddons().empty());
	}

	BabyDI_RELEASE(Server);
}
