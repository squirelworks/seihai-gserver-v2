// Seihai project, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
// PLO_PING (108) / PLI_PONG (162) for game clients (issue #25).

#define CATCH_CONFIG_MAIN
#include "catch2/catch_all.hpp"

#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>

#include <atomic>

#include <player/PlayerClient.h>

using namespace preagonal;

// Server.cpp refers to it; a Debug link keeps the reference (main.cpp, which defines it, is not in the test).
std::atomic_bool shutdownProgram{false};

TEST_CASE("PLO_PING goes only to the builds that answer it", "[ping]")
{
	CHECK(PlayerClient::handlesPing(CLVER_4_208));
	CHECK(PlayerClient::handlesPing(CLVER_4_102_MAC)); // G3D27076, the build the Tiger lab ran
	CHECK(PlayerClient::handlesPing(CLVER_6_015));
	CHECK(PlayerClient::handlesPing(CLVER_6_034));
	CHECK(PlayerClient::handlesPing(CLVER_6_037));
	CHECK(PlayerClient::handlesPing(CLVER_6_037_LINUX));

	for (const int v : {CLVER_UNKNOWN, CLVER_2_31, CLVER_3_153, CLVER_4_0211, CLVER_4_042, CLVER_4_042_MAC, CLVER_4_110, CLVER_4_206_MAC, CLVER_5_07, CLVER_5_12, CLVER_5_12_MAC, CLVER_5_31x, CLVER_IPHONE_1_1})
		CHECK_FALSE(PlayerClient::handlesPing(v));
}

TEST_CASE("PLO_PING is {108}{c 0}{s value}, sub 0 only", "[ping]")
{
	CString packet = PlayerClient::pingPacket(1234);
	REQUIRE(packet.length() == 4);
	CHECK(packet.readGUChar() == PLO_PING);
	CHECK(packet.readGUChar() == 0);
	CHECK(packet.readGUShort() == 1234);

	CString capped = PlayerClient::pingPacket(0xFFFF);
	capped.setRead(2);
	CHECK(capped.readGUShort() == PlayerClient::MAX_PING_VALUE);
}

TEST_CASE("PLI_PONG from a game client is {162}{'!'}{s value}", "[ping]")
{
	// The body after the id, as the handler gets it.
	CString answer = CString() >> (char)1 >> (short)1234;
	CHECK(answer.text()[0] == '!');
	CHECK(PlayerClient::readPong(answer) == std::optional<uint16_t>{1234});

	CString empty;
	CHECK_FALSE(PlayerClient::readPong(empty).has_value()); // RC-style keepalive

	CString odd = CString() >> (char)2 >> (short)5;
	CHECK_FALSE(PlayerClient::readPong(odd).has_value());
}
