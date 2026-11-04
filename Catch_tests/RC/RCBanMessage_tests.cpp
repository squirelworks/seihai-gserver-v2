// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: regression tests for the kick text sent when an RC bans an online player.
#include "catch2/catch_all.hpp"

#include <CString.h>

#include <player/RCBanMessage.h>

using namespace preagonal;

SCENARIO("rcBanDisconnectMessage builds the kick text for an RC ban")
{
	GIVEN("the reason a real 2.22 RC sent in the lab capture (2026-11-04)")
	{
		// RC_PLAYERBANSET carried the reason as CSV: "lab ban reason".
		// Before the fix, building this text never returned and the server hung.
		const CString reason = "\"lab ban reason\"";

		THEN("the reason is unquoted and the call returns")
		{
			REQUIRE(rcBanDisconnectMessage("labrc", reason).toString() == "labrc has banned you.  Reason: lab ban reason");
		}
	}

	GIVEN("a reason of two lines")
	{
		const CString reason = "\"line one\",\"line two\"";

		THEN("the lines are joined with a carriage return")
		{
			REQUIRE(rcBanDisconnectMessage("labrc", reason).toString() == "labrc has banned you.  Reason: line one\rline two");
		}
	}

	GIVEN("an empty reason")
	{
		THEN("the text ends after \"Reason: \"")
		{
			REQUIRE(rcBanDisconnectMessage("labrc", CString()).toString() == "labrc has banned you.  Reason: ");
		}
	}
}
