// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// PLO_SERVERWARP (178) is held until the client's file requests have gone quiet, so a
// 5.24r4 or 6.03r7 gets all of pics1.png before it leaves the login server.

#include <catch2/catch_all.hpp>

#include <chrono>

#include <IEnums.h>
#include <player/PlayerClient.h>

using namespace preagonal;
using namespace std::chrono_literals;

TEST_CASE("A held server warp waits for the file requests to go quiet", "[serverwarp]")
{
	const clock::time_point t0{};

	// The lab case: the 178 came about 0.2 s after level entry, while pics1.png was still in the file queue.
	CHECK_FALSE(PlayerClient::serverWarpReady(t0 + 300ms, t0 + 200ms, t0));
	// A request a second after the hold starts pushes it back.
	CHECK_FALSE(PlayerClient::serverWarpReady(t0 + 2500ms, t0 + 200ms, t0 + 1200ms));
	CHECK(PlayerClient::serverWarpReady(t0 + 3200ms, t0 + 200ms, t0 + 1200ms));
	// No activity at all still holds for the quiet time, so a client that has not asked yet can.
	CHECK_FALSE(PlayerClient::serverWarpReady(t0 + 1s, t0, t0 - 1h));
	CHECK(PlayerClient::serverWarpReady(t0 + 2s, t0, t0 - 1h));
}

TEST_CASE("A held server warp goes after the longest hold even with requests still coming", "[serverwarp]")
{
	const clock::time_point t0{};
	CHECK_FALSE(PlayerClient::serverWarpReady(t0 + 29s, t0, t0 + 28s));
	CHECK(PlayerClient::serverWarpReady(t0 + PlayerClient::SERVERWARP_MAX_HOLD, t0, t0 + 29s));
}

TEST_CASE("File requests that count for the server warp hold", "[serverwarp]")
{
	for (const uint8_t id : {PLI_WANTFILE, PLI_UPDATEFILE, PLI_VERIFYWANTSEND, PLI_UPDATEGANI, PLI_UPDATESCRIPT, PLI_UPDATECLASS, PLI_UPDATEPACKAGEREQUESTFILE})
		CHECK(PlayerClient::isFileRequest(id));
	for (const uint8_t id : {PLI_LEVELWARP, PLI_PLAYERPROPS, PLI_TOALL, PLI_SERVERWARP})
		CHECK_FALSE(PlayerClient::isFileRequest(id));
}
