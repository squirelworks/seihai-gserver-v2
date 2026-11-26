// Seihai project, 2026-11-25, 2026-11-26: see CHANGES-SEIHAI.md.
// The Graal 3D side as an add-on: off unless `graal3d = true`, and then the core
// hooks give the 3.151 to 5.x clients what they need and list the server at HQ 4.
#include "catch2/catch_all.hpp"

#include <atomic>
#include <optional>
#include <string>

#ifndef _WIN32
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <BabyDI.h>
#include <IEnums.h>
#include <IUtil.h>
#include <Server.h>
#include <graal3d/Graal3DAddon.h>
#include <graal3d/Graal3DCapabilities.h>
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

	SECTION("login files skip 4.20r8")
	{
		// Windows 4.20r8 draws terrain untextured and grows no cover, so the
		// terrain and grass files are not pushed to it; its capability row agrees.
		CHECK_FALSE(graal3d::Service::takesLoginFiles(CLVER_4_208));
		REQUIRE(graal3d::capabilitiesForToken("G3D20126") != nullptr);
		CHECK_FALSE(graal3d::capabilitiesForToken("G3D20126")->terrainTextures);
		CHECK_FALSE(graal3d::capabilitiesForToken("G3D20126")->groundCover);
		for (const int v : {CLVER_3_153, CLVER_4_208_LINUX, CLVER_5_12, CLVER_5_12_MAC, CLVER_5_24})
			CHECK(graal3d::Service::takesLoginFiles(v));
		for (const int v : {CLVER_2_31, CLVER_6_015})
			CHECK_FALSE(graal3d::Service::takesLoginFiles(v));
		// 6.037 may link (Frankenstein 6.037) but is not offered the 3D login needs.
		for (const int v : {CLVER_6_037, CLVER_6_037_LINUX})
		{
			CHECK(graal3d::Service::mayLinkGraal3D(v));
			CHECK_FALSE(graal3d::Service::clientHasGraal3D(v));
			CHECK_FALSE(server->getAddons().clientNeeds(v).answerMissingChecksumFiles);
		}
		for (const int v : {CLVER_3_153, CLVER_5_12})
			CHECK(graal3d::Service::mayLinkGraal3D(v));
		for (const int v : {CLVER_2_31, CLVER_6_015, CLVER_6_034})
			CHECK_FALSE(graal3d::Service::mayLinkGraal3D(v));
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
			CHECK(needs.loginFiles == nullptr); // no grass in this world
		}
		for (const int v : {CLVER_2_31, CLVER_6_015, CLVER_6_037})
		{
			INFO("version " << v);
			const auto needs = server->getAddons().clientNeeds(v);
			CHECK_FALSE(needs.answerMissingChecksumFiles);
			CHECK_FALSE(needs.alwaysServerListConnected);
			CHECK(needs.bareNameExtensions.empty());
			CHECK(needs.loginFiles == nullptr);
		}

#ifndef _WIN32
		// Junk and floods on the 3D port (T3D-142 to T3D-144): nothing links,
		// nothing crashes, and past the per-address budget datagrams are dropped.
		{
			const int s = socket(AF_INET, SOCK_DGRAM, 0);
			REQUIRE(s >= 0);
			sockaddr_in to{};
			to.sin_family = AF_INET;
			to.sin_port = htons(47311);
			to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			const auto send = [&](const std::string& d) { sendto(s, d.data(), d.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)); };
			send(std::string(1, '\x1a'));                    // a connect request with nothing after its type
			send(std::string("\x1a\xff\xff\xff\xff", 5)); // a connect request cut short
			send(std::string(1400, '\xff'));                  // an odd type from an address with no link
			// In batches the server reads between, so the kernel's receive
			// buffer never overflows (it did on a CI runner: 83 of 303 lost).
			for (int i = 0; i < 300; ++i)
			{
				send(std::string(1, static_cast<char>((i * 2) & 0xfe)) + std::string(static_cast<size_t>(i % 64), static_cast<char>(i)));
				if (i % 20 == 19)
					addon->service().update();
			}
			for (int i = 0; i < 4; ++i)
				addon->service().update();
			close(s);
			CHECK(addon->service().sessionCount() == 0);
			CHECK(addon->service().droppedDatagrams() >= 200);
		}
#endif

		server->getAddons().clear();
		CHECK(server->getAddons().empty());
	}

	BabyDI_RELEASE(Server);
}
