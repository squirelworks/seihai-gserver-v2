// Seihai project, 2026-11-25, 2026-11-27: see CHANGES-SEIHAI.md.
// The add-on hooks (addons/ServerAddon.h) leave the core as it is when no add-on
// runs, whether or not any is compiled in.
#include "catch2/catch_all.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <BabyDI.h>
#include <IEnums.h>
#include <IUtil.h>
#include <Server.h>
#include <addons/AddonModule.h>

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

TEST_CASE("the add-on API's own checks", "[addons]")
{
	// The toolchain string names a compiler family and a platform.
	const auto toolchain = addonToolchain();
	CHECK(toolchain.find('/') != std::string::npos);
	CHECK_FALSE(toolchain.empty());
	// The version-table fingerprint is fixed for one gs2lib.
	STATIC_REQUIRE(addonVersionTableFingerprint() == addonVersionTableFingerprint());
	CHECK(kAddonApiVersion >= 1);
}

// Built in, the Graal 3D add-on always wins and no module is looked for, so these
// run only in a server built without it (the addons-off CI job).
#ifndef SEIHAI_ADDON_GRAAL3D
TEST_CASE("graal3d = true with no module runs without it", "[addons][module]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("addons-module-missing"));
	server->prepareSettings();
	server->getSettings().set("graal3d", true);

	SECTION("no module anywhere")
	{
		server->getSettings().set("graal3dmodule", std::string("no/such/libseihai-graal3d.so"));
		server->getAddons().start(*server, "127.0.0.1");
		CHECK(server->getAddons().empty());
	}
	SECTION("a file that is not a module")
	{
		{
			std::ofstream f("not-a-module.so", std::ios::binary);
			f << "not a shared library";
		}
		server->getSettings().set("graal3dmodule", std::string("not-a-module.so"));
		server->getAddons().start(*server, "127.0.0.1");
		CHECK(server->getAddons().empty());
		std::remove("not-a-module.so");
	}
	SECTION("graal3dmodule = off")
	{
		server->getSettings().set("graal3dmodule", std::string("off"));
		server->getAddons().start(*server, "127.0.0.1");
		CHECK(server->getAddons().empty());
	}
	BabyDI_RELEASE(Server);
}

// SEIHAI_GRAAL3D_MODULE names a built seihai-graal3d (the addons-off CI job
// builds one): the server loads it, it opens its UDP port and answers the hooks
// as the built-in add-on does.
TEST_CASE("a server without 3D code loads the seihai-graal3d module", "[addons][module]")
{
	const char* path = std::getenv("SEIHAI_GRAAL3D_MODULE");
	if (path == nullptr || *path == '\0')
		SKIP("SEIHAI_GRAAL3D_MODULE is not set");

	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("addons-module"));
	server->prepareSettings();
	server->getSettings().set("graal3d", true);
	server->getSettings().set("graal3dport", 47313);
	server->getSettings().set("graal3dmodule", std::string(path));

	auto& addons = server->getAddons();
	addons.start(*server, "127.0.0.1");
	REQUIRE_FALSE(addons.empty());
	CHECK(addons.moduleNames() == std::vector<std::string>{"graal3d"});
	CHECK(addons.listHqLevel() == std::optional<int>{4});
	for (const int v : {CLVER_3_153, CLVER_4_208, CLVER_5_12})
	{
		INFO("version " << v);
		const auto needs = addons.clientNeeds(v);
		CHECK(needs.answerMissingChecksumFiles);
		CHECK(needs.alwaysServerListConnected);
		CHECK_FALSE(needs.bareNameExtensions.empty());
		CHECK(needs.refusedFiles != nullptr);
	}
	for (const int v : {CLVER_2_31, CLVER_6_037})
		CHECK_FALSE(addons.clientNeeds(v).answerMissingChecksumFiles);
	// A second start keeps the module already running.
	addons.start(*server, "127.0.0.1");
	CHECK(addons.moduleNames().size() == 1);
	addons.update();
	addons.onPlayerLeft(1234);
	addons.clear();
	CHECK(addons.empty());
	CHECK(addons.moduleNames().empty());
	BabyDI_RELEASE(Server);
}
#endif
