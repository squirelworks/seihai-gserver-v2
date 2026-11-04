// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: regression tests for the Edit Account fields an RC sends and reads back.
#include "catch2/catch_all.hpp"

#include <string>

#include <CString.h>

#include <Account.h>
#include <player/RCAccountFields.h>

using namespace preagonal;

namespace
{
	/// A GSTRING: its length as a GCHAR (+32), then the text.
	CString gstring(const std::string& text)
	{
		return CString() >> (char)text.length() << text;
	}

	/// The PLI_RC_ACCOUNTSET fields after "test1" from lab capture rc-capture-2026-11-04b,
	/// with Load only ticked, admin level 2 and admin worlds "main,lab".
	CString capturedAccountSet()
	{
		return CString() << gstring("") << gstring("lab@example.invalid")
			>> (char)0 >> (char)1 >> (char)2
			<< gstring("main,lab") << gstring("\"lab reason\"");
	}
}

SCENARIO("an RC's Edit Account fields are read and kept")
{
	GIVEN("the account set a real 2.22 RC sent in the lab capture")
	{
		CString packet = capturedAccountSet();
		const RCAccountFields fields = readRCAccountFields(packet);

		THEN("every field is read in the RC's order")
		{
			REQUIRE(fields.password.empty());
			REQUIRE(fields.email == "lab@example.invalid");
			REQUIRE_FALSE(fields.banned);
			REQUIRE(fields.loadOnly);
			REQUIRE(fields.adminLevel == 2);
			REQUIRE(fields.adminWorlds == "main,lab");
			REQUIRE(fields.banReason == "\"lab reason\"");
		}

		WHEN("they are stored on the account by an RC that may ban")
		{
			Account target;
			applyRCAccountFields(target, fields, true);

			THEN("load only, admin level and admin worlds are kept, not only the e-mail")
			{
				REQUIRE(target.email == "lab@example.invalid");
				REQUIRE(target.loadOnly);
				REQUIRE(target.adminLevel == 2);
				REQUIRE(target.adminWorlds == "main,lab");
				REQUIRE(target.banReason == "\"lab reason\"");
			}

			THEN("the readback the RC gets carries the same fields")
			{
				CString reply = rcAccountGetPayload("test1", target);
				REQUIRE(reply.readChars(reply.readGUChar()) == "test1");
				const RCAccountFields back = readRCAccountFieldsWithBanTime(reply);
				REQUIRE(back.email == "lab@example.invalid");
				REQUIRE(back.loadOnly);
				REQUIRE(back.adminLevel == 2);
				REQUIRE(back.adminWorlds == "main,lab");
				REQUIRE(back.banReason == "\"lab reason\"");
			}
		}

		WHEN("they are stored by an RC without the ban right")
		{
			Account target;
			target.banReason = "kept";
			RCAccountFields banning = fields;
			banning.banned = true;
			applyRCAccountFields(target, banning, false);

			THEN("the ban is left alone but the other fields are kept")
			{
				REQUIRE_FALSE(target.banned);
				REQUIRE(target.banReason == "kept");
				REQUIRE(target.adminLevel == 2);
			}
		}
	}

	GIVEN("an account with no admin worlds")
	{
		Account target;
		CString reply = rcAccountGetPayload("labadd", target);
		reply.readChars(reply.readGUChar());

		THEN("it reads back as \"main\", as before")
		{
			REQUIRE(readRCAccountFieldsWithBanTime(reply).adminWorlds == "main");
		}
	}
}
