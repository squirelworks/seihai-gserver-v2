// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// What the server sends back for a gr.flagset / gr.flagdel trigger from a translated GS1 script (seihai-core #30).

#include <catch2/catch_all.hpp>

#include <chrono>
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

TEST_CASE("A script flag write that changes nothing is dropped", "[scriptflag]")
{
	// Issue #75. Held with text, held with no text (`set name`), and not held.
	CHECK(PlayerClient::scriptFlagUnchanged(false, "quest=done", true, std::string{"done"}));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "quest=done,1", true, std::string{"done"}));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "quest=done", false, std::nullopt));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "quest=done", true, std::nullopt));
	CHECK(PlayerClient::scriptFlagUnchanged(false, "gotkey", true, std::nullopt));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "gotkey", true, std::string{"1"}));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "gotkey", false, std::nullopt));

	// A delete, or an empty value (which PLI_FLAGSET treats as one), changes nothing only when the flag is gone.
	CHECK(PlayerClient::scriptFlagUnchanged(true, "gotkey", false, std::nullopt));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(true, "gotkey", true, std::nullopt));
	CHECK(PlayerClient::scriptFlagUnchanged(false, "quest=", false, std::nullopt));
	CHECK_FALSE(PlayerClient::scriptFlagUnchanged(false, "quest=", true, std::string{"done"}));
}

TEST_CASE("Script flag writes are capped per client", "[scriptflag]")
{
	using Budget = PlayerClient::ScriptFlagBudget;
	Budget budget;
	const auto start = clock::now();

	// A burst of CAPACITY goes; the next write in the same instant does not.
	for (int i = 0; i < static_cast<int>(Budget::CAPACITY); ++i)
		CHECK(budget.take(start));
	CHECK_FALSE(budget.take(start));

	// Then PER_SECOND: a 50 ms tick (20 a second) for 10 s gets about 80 of 200 through.
	int through = 0;
	for (int tick = 1; tick <= 200; ++tick)
		through += budget.take(start + std::chrono::milliseconds{50 * tick}) ? 1 : 0;
	CHECK(through >= static_cast<int>(Budget::PER_SECOND * 10) - 1);
	CHECK(through <= static_cast<int>(Budget::PER_SECOND * 10));

	// A quiet second refills it, up to CAPACITY and no further.
	CHECK(budget.take(start + std::chrono::seconds{60}));
	CHECK(budget.tokens == Budget::CAPACITY - 1.0);
}
