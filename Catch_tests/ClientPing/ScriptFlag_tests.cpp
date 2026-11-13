// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// What the server sends back for a gr.flagset / gr.flagdel trigger from a translated GS1 script (seihai-core #30).

#include <catch2/catch_all.hpp>

#include <optional>
#include <string>

#include <IEnums.h>
#include <player/PlayerClient.h>

using namespace preagonal;

TEST_CASE("A script flag trigger sends the flag's state back", "[scriptflag]")
{
	CString set = PlayerClient::scriptFlagState("gotkey", true, std::nullopt);
	CHECK(set.readGUChar() == PLO_FLAGSET);
	CHECK(set.readString("") == "gotkey");

	CString text = PlayerClient::scriptFlagState("quest", true, std::string{"done,1"});
	CHECK(text.readGUChar() == PLO_FLAGSET);
	CHECK(text.readString("") == "quest=done,1");

	CString gone = PlayerClient::scriptFlagState("gotkey", false, std::nullopt);
	CHECK(gone.readGUChar() == PLO_FLAGDEL);
	CHECK(gone.readString("") == "gotkey");
}

TEST_CASE("Nothing is sent back for this., server. and gr. names", "[scriptflag]")
{
	CHECK(PlayerClient::echoesScriptFlag("gotkey"));
	CHECK(PlayerClient::echoesScriptFlag("client.seen"));
	CHECK_FALSE(PlayerClient::echoesScriptFlag(""));
	CHECK_FALSE(PlayerClient::echoesScriptFlag("this.open"));
	CHECK_FALSE(PlayerClient::echoesScriptFlag("server.count"));
	CHECK_FALSE(PlayerClient::echoesScriptFlag("gr.x"));
}
