// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// Hard guards: never classic script ids to 5.24+/v6; never modern cache ids to pure 4.x.
#include <catch2/catch_all.hpp>

#include <IUtil.h>
#include <misc/ScriptPacketGuards.h>

using namespace preagonal;

TEST_CASE("script packet guards: classic through 5.12, modern from 6.00 beta1 / 5.24", "[script][guards]")
{
	SECTION("pure 4.x uses classic only")
	{
		for (const int v : {CLVER_4_042, CLVER_4_208, CLVER_4_206_MAC})
		{
			REQUIRE(scriptPackets::usesClassicScriptBytecode(v));
			REQUIRE_FALSE(scriptPackets::usesModernScriptCache(v));
		}
	}

	SECTION("5.00r7 and 5.12 prefer classic (both id sets exist on the client)")
	{
		for (const int v : {CLVER_5_07, CLVER_5_12, CLVER_5_12_MAC})
		{
			REQUIRE(scriptPackets::usesClassicScriptBytecode(v));
			REQUIRE_FALSE(scriptPackets::usesModernScriptCache(v));
		}
	}

	SECTION("5.24 / 6.x use modern only")
	{
		for (const int v : {CLVER_6_00_BETA1, CLVER_5_24, CLVER_5_31x, CLVER_6_015, CLVER_6_037, CLVER_6_037_LINUX})
		{
			REQUIRE_FALSE(scriptPackets::usesClassicScriptBytecode(v));
			REQUIRE(scriptPackets::usesModernScriptCache(v));
		}
	}
}
