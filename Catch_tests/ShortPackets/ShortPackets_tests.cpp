// Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// Short and empty client packets (#103, #87). The 2026-11-18 lab sweep sent every
// client-to-server id 0-223 with an empty body from a live 6.03r7 session; these cases
// keep what it found fixed, and run the same sweep through the real handlers.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>

#include <CSocket.h>
#include <CString.h>
#include <IEnums.h>

#include <BabyDI.h>
#include <Server.h>
#include <filesystem/File.h>
#include <level/LevelBaddy.h>
#include <player/PlayerClient.h>
#include <player/PlayerRC.h>
#include <utilities/Log.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

static CString body(std::initializer_list<unsigned char> bytes)
{
	CString s;
	for (const auto b : bytes) s.writeChar(static_cast<char>(b));
	return s;
}

TEST_CASE("An empty or short BADDYADD makes no baddy", "[baddyadd]")
{
	// The lab case: no body at all read as type 224 at tile 112,112.
	CString empty;
	CHECK_FALSE(PlayerClient::readBaddyAdd(empty).has_value());

	// One to three bytes are still short.
	for (int n = 1; n <= 3; ++n)
	{
		CString s;
		for (int i = 0; i < n; ++i) s.writeGChar(10);
		CHECK_FALSE(PlayerClient::readBaddyAdd(s).has_value());
	}
}

TEST_CASE("BADDYADD checks the type and the tile", "[baddyadd]")
{
	// x 30*2, y 20*2, type, power. GCHAR adds 32 on the wire.
	auto make = [](uint8_t x2, uint8_t y2, uint8_t type, uint8_t power) {
		CString s;
		s.writeGChar(x2).writeGChar(y2).writeGChar(type).writeGChar(power);
		return s;
	};

	auto good = make(60, 40, static_cast<uint8_t>(BaddyType::DRAGON), 4);
	const auto req = PlayerClient::readBaddyAdd(good);
	REQUIRE(req.has_value());
	CHECK(req->x == 30.0f);
	CHECK(req->y == 20.0f);
	CHECK(req->type == static_cast<uint8_t>(BaddyType::DRAGON));
	CHECK(req->power == 4);

	auto badType = make(60, 40, static_cast<uint8_t>(BADDYTYPE_COUNT), 4);
	CHECK_FALSE(PlayerClient::readBaddyAdd(badType).has_value());

	auto offLevel = make(128, 40, 0, 4);
	CHECK_FALSE(PlayerClient::readBaddyAdd(offLevel).has_value());

	// Power is held to six hearts.
	auto strong = make(60, 40, 0, 100);
	const auto strongReq = PlayerClient::readBaddyAdd(strong);
	REQUIRE(strongReq.has_value());
	CHECK(strongReq->power == 12);

	// A 1.41 image with no extension gets .gif.
	auto withImage = make(60, 40, 0, 4);
	withImage << "baddy1";
	const auto imageReq = PlayerClient::readBaddyAdd(withImage);
	REQUIRE(imageReq.has_value());
	CHECK(imageReq->image == "baddy1.gif");
}

// handlePacket is protected; the sweep calls it the way the socket loop does.
struct SweepClient : PlayerClient
{
	using PlayerClient::PlayerClient;
	using PlayerClient::handlePacket;
};

struct SweepFixture
{
	SweepFixture()
	{
		log::networkdump.disabled = true;
		log::npc.disabled = true;
		log::rc.disabled = true;
		log::script.disabled = true;
		log::server.disabled = true;

		BabyDI_RELEASE(ITranslationManager);
		BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
		BabyDI_RELEASE(Server);
		server = BabyDI_PROVIDE(Server, new Server("test"));
	}

	std::shared_ptr<SweepClient> newClient()
	{
		auto client = std::make_shared<SweepClient>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
		client->setType(PLTYPE_CLIENT3);
		client->account.name = "Sweep";
		server->addPlayer(client, client->getId());
		return client;
	}

	Server* server = nullptr;
};

TEST_CASE_METHOD(SweepFixture, "MUTEPLAYER and PROCESSRUNNING do not count as invalid packets", "[sweep]")
{
	auto client = newClient();
	for (int i = 0; i < 5; ++i)
	{
		CString mute = body({32 + 1, 32 + 2, 32 + 1});
		CHECK(client->handlePacket(PLI_MUTEPLAYER, mute) == HandlePacketResult::Handled);
		CString running = body({32 + 1});
		CHECK(client->handlePacket(PLI_PROCESSRUNNING, running) == HandlePacketResult::Handled);
	}
}

TEST_CASE_METHOD(SweepFixture, "Every client id with an empty body is handled without a crash", "[sweep]")
{
	// Under the sanitizer job (#87) this catches a handler that reads or writes out of
	// bounds on a short body. A fresh client per id, since some ids log the client out.
	for (int id = 0; id < 224; ++id)
	{
		// RAWDATA announces the size of the next packet; it has no body of its own to test.
		if (id == PLI_RAWDATA)
			continue;

		CAPTURE(id);
		auto client = newClient();
		CString empty;
		client->handlePacket(static_cast<uint8_t>(id), empty);
		server->deletePlayer(client);
	}
	SUCCEED();
}

// A level name with a byte from 0xF5 to 0xFF (Windows-1252 õ to ÿ) made ztd.text's
// UTF-8 decoder loop forever in fs::getANSIFileName, so one PLI_LEVELWARP hung the
// server (found by fuzz/handlers). A hang must fail the test, not stall it.
template <typename F>
static void requireFinishes(F&& f)
{
	auto done = std::async(std::launch::async, std::forward<F>(f));
	if (done.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
	{
		FAIL_CHECK("did not finish within 10 seconds");
		std::_Exit(1);
	}
}

TEST_CASE("getANSIFileName keeps a raw Windows-1252 name", "[ansi]")
{
	for (int b = 0xF5; b <= 0xFF; ++b)
	{
		CAPTURE(b);
		const std::string name = std::string("M") + static_cast<char>(b) + "ller.nw";
		std::string out;
		requireFinishes([&] { out = fs::getANSIFileName(std::filesystem::path{name}); });
		CHECK(out == name);
	}
	CHECK(fs::getANSIFileName(std::filesystem::path{"dir/plain.nw"}) == "plain.nw");
}

TEST_CASE_METHOD(SweepFixture, "LEVELWARP to a name with 0xFC returns", "[ansi]")
{
	auto client = newClient();
	CString warp;
	warp.writeGChar(60).writeGChar(60);
	warp << "M\xfcller.nw";
	requireFinishes([&] { client->handlePacket(PLI_LEVELWARP, warp); });
	server->deletePlayer(client);
}

struct OptionsRC : PlayerRC
{
	using PlayerRC::PlayerRC;
	using PlayerRC::handlePacket;
};

TEST_CASE_METHOD(SweepFixture, "RC server options on a world with no serveroptions.txt", "[rc]")
{
	// The test world has no config folder, so the file cannot be opened. This was a
	// null dereference (found by fuzz/handlers).
	auto rc = std::make_shared<OptionsRC>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
	rc->setType(PLTYPE_RC2);
	rc->account.name = "OptionsRC";
	server->addPlayer(rc, rc->getId());
	CString empty;
	CHECK(rc->handlePacket(PLI_RC_SERVEROPTIONSGET, empty) == HandlePacketResult::Handled);
	server->deletePlayer(rc);
}
