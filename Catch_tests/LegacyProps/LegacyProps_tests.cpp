// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
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
