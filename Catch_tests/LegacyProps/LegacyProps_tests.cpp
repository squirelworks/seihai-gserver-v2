// Seihai project, 2026-11-13, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// Player props for 1.x clients on a newmain server (five-client lab, #27).

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <atomic>
#include <set>
#include <string_view>
#include <vector>

#include <BabyDI.h>
#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>
#include <Server.h>
#include <player/LegacyPlayerProps.h>
#include <player/PlayerProps.h>
#include <utilities/CommonTypes.h>
#include <utilities/PropertySerializers.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

namespace
{
	CString str(const char* s)
	{
		CString c;
		c >> (char)std::string_view{s}.size() << s;
		return c;
	}

	// The prop ids of a rewritten stream, read with the 1.x forms.
	std::vector<int> ids(CString props)
	{
		std::vector<int> out;
		props.setRead(0);
		while (props.bytesLeft() > 0)
		{
			const int id = props.readGUChar();
			out.push_back(id);
			switch (static_cast<PlayerProp>(id))
			{
				case PlayerProp::NICKNAME:
				case PlayerProp::MESSAGE:
				case PlayerProp::LEVEL:
				case PlayerProp::HORSEIMAGE:
				case PlayerProp::ACCOUNTNAME:
					props.readChars(props.readGUChar());
					break;
				case PlayerProp::SWORDIMAGE:
				{
					const int b = props.readGUChar();
					if (b >= 30) props.readChars(props.readGUChar());
					break;
				}
				case PlayerProp::SHIELDIMAGE:
				{
					const int b = props.readGUChar();
					if (b >= 10) props.readChars(props.readGUChar());
					break;
				}
				case PlayerProp::HEADIMAGE:
				{
					const int b = props.readGUChar();
					if (b >= 100) props.readChars(b - 100);
					break;
				}
				case PlayerProp::COLORS: props.readChars(5); break;
				case PlayerProp::CARRYNPCID: props.readChars(3); break;
				case PlayerProp::IPADDR: props.readChars(5); break;
				case PlayerProp::UDPPORT: props.readChars(3); break;
				default: props.readChars(1); break;
			}
		}
		return out;
	}
} // namespace

TEST_CASE("1.x clients get player props in the pre-2.1 forms", "[legacy]")
{
	BabyDI_PROVIDE(Server, new Server("test"));

	// What a newmain server sends another player when one joins the level:
	// JOINLEAVELVL, then the props in id order, a gani in prop 10.
	CString props;
	props >> (char)PROPID(PlayerProp::JOINLEAVELVL) >> (char)1;
	props >> (char)PROPID(PlayerProp::NICKNAME) << str("m231");
	props >> (char)PROPID(PlayerProp::SWORDIMAGE) >> (char)(1 + 30) << str("sword1.png");
	props >> (char)PROPID(PlayerProp::SHIELDIMAGE) >> (char)(1 + 10) << str("shield1.png");
	props >> (char)PROPID(PlayerProp::GANI) << str("idle");
	props >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)(100 + 9) << "head0.png";
	props >> (char)PROPID(PlayerProp::COLORS) >> (char)2 >> (char)0 >> (char)10 >> (char)4 >> (char)18;
	props >> (char)PROPID(PlayerProp::X) >> (char)60;
	props >> (char)PROPID(PlayerProp::Y) >> (char)61;
	props >> (char)PROPID(PlayerProp::SPRITE) >> (char)2;
	props >> (char)PROPID(PlayerProp::ACCOUNTNAME) << str("m231");
	props >> (char)PROPID(PlayerProp::BODYIMAGE) << str("body.png");
	props >> (char)PROPID(PlayerProp::GATTRIB1) << str("hat0.png");

	SECTION("1.39 keeps what it reads, X and Y included")
	{
		const CString out = legacy::rewritePropsForPre21(props, CLVER_1_39, true);
		const std::vector<int> expected{
			PROPID(PlayerProp::JOINLEAVELVL), PROPID(PlayerProp::NICKNAME), PROPID(PlayerProp::SWORDIMAGE),
			PROPID(PlayerProp::SHIELDIMAGE), PROPID(PlayerProp::HEADIMAGE), PROPID(PlayerProp::COLORS),
			PROPID(PlayerProp::X), PROPID(PlayerProp::Y), PROPID(PlayerProp::SPRITE), PROPID(PlayerProp::ACCOUNTNAME)};
		REQUIRE(ids(out) == expected);
		REQUIRE(out.find("idle") < 0);
		REQUIRE(out.find("body") < 0);
		REQUIRE(out.find(".png") < 0);
	}

	SECTION("stock sword, shield and head go as preset numbers")
	{
		CString out = legacy::rewritePropsForPre21(props, CLVER_1_39, true);
		out.setRead(0);
		out.readGUChar(); out.readGUChar();           // JOINLEAVELVL 1
		out.readGUChar(); out.readChars(out.readGUChar()); // nick
		REQUIRE(out.readGUChar() == PROPID(PlayerProp::SWORDIMAGE));
		REQUIRE(out.readGUChar() == 1);
		REQUIRE(out.readGUChar() == PROPID(PlayerProp::SHIELDIMAGE));
		REQUIRE(out.readGUChar() == 1);
		REQUIRE(out.readGUChar() == PROPID(PlayerProp::HEADIMAGE));
		REQUIRE(out.readGUChar() == 0);
	}

	SECTION("a custom image becomes .gif")
	{
		CString custom;
		custom >> (char)PROPID(PlayerProp::SWORDIMAGE) >> (char)(2 + 30) << str("blade.png");
		custom >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)(100 + 10) << "mine1.PNG";
		const CString out = legacy::rewritePropsForPre21(custom, CLVER_1_39, true);
		REQUIRE(out.find("blade.gif") >= 0);
		REQUIRE(out.find("mine1.gif") >= 0);
	}

	SECTION("1.33 drops the account name and the extra flags")
	{
		const auto got = ids(legacy::rewritePropsForPre21(props, CLVER_1_33, true));
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::ACCOUNTNAME)) == got.end());
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::X)) != got.end());
	}

	SECTION("own props stop at the client's last prop")
	{
		const auto got = ids(legacy::rewritePropsForPre21(props, CLVER_1_39, false));
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::GANI)) == got.end());
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::BODYIMAGE)) == got.end());
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::ACCOUNTNAME)) != got.end());
	}

	SECTION("the packet id and player id stay")
	{
		CString packet;
		packet >> (char)PLO_OTHERPLPROPS >> (short)7 << props;
		const CString out = legacy::rewritePacketForPre21(packet, CLVER_1_39);
		REQUIRE(out.subString(0, 3) == packet.subString(0, 3));
	}

	SECTION("legacyprops_drop leaves out more props")
	{
		REQUIRE(legacy::parseDropList("19, 30,31") == std::set<int>{19, 30, 31});
		REQUIRE(legacy::parseDropList("").empty());
		const auto got = ids(legacy::rewritePropsForPre21(props, CLVER_1_39, true, {PROPID(PlayerProp::SPRITE)}));
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::SPRITE)) == got.end());
		REQUIRE(std::ranges::find(got, PROPID(PlayerProp::ACCOUNTNAME)) != got.end());
	}

	SECTION("status bit 4 (male from 2.0, swimming for 1.x) is cleared")
	{
		CString status;
		status >> (char)PROPID(PlayerProp::NICKNAME) << str("m222");
		status >> (char)PROPID(PlayerProp::STATUS) >> (char)(PLSTATUS_MALE | PLSTATUS_ALLOWWEAPONS);
		status >> (char)PROPID(PlayerProp::X) >> (char)60;

		const auto statusOf = [](CString out) -> int
		{
			out.setRead(0);
			while (out.bytesLeft() > 0)
			{
				const int id = out.readGUChar();
				if (id == PROPID(PlayerProp::STATUS))
					return out.readGUChar();
				if (id == PROPID(PlayerProp::NICKNAME))
					out.readChars(out.readGUChar());
				else
					out.readGUChar();
			}
			return -1;
		};

		// Another 2.x player, and the client's own props.
		REQUIRE(statusOf(legacy::rewritePropsForPre21(status, CLVER_1_39, true)) == PLSTATUS_ALLOWWEAPONS);
		REQUIRE(statusOf(legacy::rewritePropsForPre21(status, CLVER_1_39, false)) == PLSTATUS_ALLOWWEAPONS);
		// Another 1.x player: bit 4 is its swimming, kept.
		REQUIRE(statusOf(legacy::rewritePropsForPre21(status, CLVER_1_39, true, {}, true)) == (PLSTATUS_MALE | PLSTATUS_ALLOWWEAPONS));
		// The props after it are still read.
		const auto got = ids(legacy::rewritePropsForPre21(status, CLVER_1_39, true));
		REQUIRE(got == std::vector<int>{PROPID(PlayerProp::NICKNAME), PROPID(PlayerProp::STATUS), PROPID(PlayerProp::X)});

		// Through the packet path too.
		CString packet;
		packet >> (char)PLO_OTHERPLPROPS >> (short)6 << status;
		REQUIRE(statusOf(legacy::rewritePacketForPre21(packet, CLVER_1_39).subString(3)) == PLSTATUS_ALLOWWEAPONS);
	}

	SECTION("only 1.20 to 1.41r1 are rewritten")
	{
		REQUIRE(legacy::isPre21Client(CLVER_1_39));
		REQUIRE(legacy::isPre21Client(CLVER_1_411));
		REQUIRE_FALSE(legacy::isPre21Client(CLVER_2_0));
		REQUIRE_FALSE(legacy::isPre21Client(CLVER_2_22));
		REQUIRE_FALSE(legacy::isPre21Client(CLVER_UNKNOWN));
		REQUIRE_FALSE(legacy::isPre21Client(CLVER_NW_2_02));
	}
}

TEST_CASE("prop 10 follows the client on a classic server (#107)", "[legacy]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("test"));
	server->Generation = ServerGeneration::CLASSIC;

	SECTION("a 2.22 client's walk gani is read as a gani, not as bow power 4 and prop 87")
	{
		CString in;
		in << str("walk");
		in.setRead(0);
		const props::ClientVersionScope scope{CLVER_2_22};
		props::PropertyGaniOrBowGif prop;
		prop.deserialize(in);
		REQUIRE(prop.gani.has_value());
		CHECK(prop.gani.value() == "walk");
		CHECK_FALSE(prop.bowGif.has_value());
		CHECK(in.bytesLeft() == 0);
	}

	SECTION("a 1.39 client's prop 10 is still a bow")
	{
		CString in;
		in >> (char)(10 + 8) << "bow1.gif";
		in.setRead(0);
		const props::ClientVersionScope scope{CLVER_1_39};
		props::PropertyGaniOrBowGif prop;
		prop.deserialize(in);
		REQUIRE(prop.bowGif.has_value());
		CHECK(prop.bowGif->first == "bow1.gif");
	}

	SECTION("a 2.x client is sent the gani in place of the bow form")
	{
		CString props;
		props >> (char)PROPID(PlayerProp::NICKNAME) << str("m139");
		props >> (char)PROPID(PlayerProp::GANI) >> (char)(10 + 8) << "bow1.gif";
		props >> (char)PROPID(PlayerProp::X) >> (char)60;

		CString out = legacy::rewriteBowPropForPost20(props, "walk");
		out.setRead(0);
		CHECK(out.readGUChar() == PROPID(PlayerProp::NICKNAME));
		CHECK(out.readChars(out.readGUChar()) == "m139");
		CHECK(out.readGUChar() == PROPID(PlayerProp::GANI));
		CHECK(out.readChars(out.readGUChar()) == "walk");
		CHECK(out.readGUChar() == PROPID(PlayerProp::X));
		CHECK(out.readGUChar() == 60);
		CHECK(out.bytesLeft() == 0);

		// A head from the classic account goes as the .png 2.x ships, and the body follows it.
		CString full;
		full >> (char)PROPID(PlayerProp::GANI) >> (char)(10 + 8) << "bow1.gif";
		full >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)(100 + 9) << "head0.gif";
		CString look = legacy::rewriteBowPropForPost20(full, "idle", "body.png");
		CHECK(look.find("head0.png") >= 0);
		CHECK(look.find("head0.gif") < 0);
		look.setRead(0);
		look.readGUChar(); look.readChars(look.readGUChar());   // gani
		look.readGUChar(); look.readChars(look.readGUChar() - 100); // head
		CHECK(look.readGUChar() == PROPID(PlayerProp::BODYIMAGE));
		CHECK(look.readChars(look.readGUChar()) == "body.png");
		CHECK(look.bytesLeft() == 0);

		// No gani known: idle.
		CHECK(legacy::rewriteBowPropForPost20(props, "").find("idle") >= 0);

		// No prop 10: unchanged.
		CString noGani;
		noGani >> (char)PROPID(PlayerProp::X) >> (char)60;
		CHECK(legacy::rewriteBowPropForPost20(noGani, "walk") == noGani);
	}

	BabyDI_RELEASE(Server);
}

TEST_CASE("PLO_ARROWADD goes to 1.x without the shooter's id", "[LegacyProps][issue106]")
{
	// 1.39 lab, 2026-11-19: 1.39 drew `52 68 23 a3 20` (a ball) but nothing for `20 20 52 68 23 a3 20`.
	CString npcBall;
	npcBall >> (char)PLO_ARROWADD >> (short)0 >> (char)50 >> (char)72 >> (char)3 >> (char)131 >> (char)0;
	CString expected;
	expected >> (char)PLO_ARROWADD >> (char)50 >> (char)72 >> (char)3 >> (char)131 >> (char)0;
	CHECK(legacy::rewriteArrowAddForPre21(npcBall) == expected);

	// A player's arrow, relayed with that player's id.
	CString relayed;
	relayed >> (char)PLO_ARROWADD >> (short)3 << "^t\"\x8d!";
	CHECK(legacy::rewriteArrowAddForPre21(relayed) == (CString() >> (char)PLO_ARROWADD << "^t\"\x8d!"));

	// Other packets are untouched.
	CString other;
	other >> (char)PLO_BOMBADD >> (short)0 >> (char)1;
	CHECK(legacy::rewriteArrowAddForPre21(other) == other);
}
