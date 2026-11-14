// Seihai project, 2026-11-14: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// An RC props set (PLI_RC_PLAYERPROPSSET2, 76) on an account that is not online
// keeps bomb power and the level, and the RC reads bomb power back (#83).

#include "catch2/catch_all.hpp"

#include <memory>
#include <string>

#include <CString.h>

#include <BabyDI.h>
#include <Server.h>
#include <object/Player.h>
#include <player/PlayerProps.h>
#include <utilities/CommonTypes.h>
#include <utilities/Log.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;

namespace
{
	CString gstring(const std::string& text)
	{
		return CString() >> (char)text.length() << text;
	}

	/// The part of a 76 after the account name: world, props, no flags, no chests, no weapons.
	CString propsSet(const CString& props)
	{
		return CString() << gstring("main") >> (char)props.length() << props >> (short)0 >> (short)0 >> (char)0;
	}

	/// Walks an RC props list and returns the GBYTE1 value of prop 7, or -1 if it is not there.
	int bombPowerIn(CString props)
	{
		props.setRead(0);
		while (props.bytesLeft() > 0)
		{
			const auto id = static_cast<PlayerProp>(props.readGUChar());
			switch (id)
			{
				case PlayerProp::BOMBPOWER:
					return props.readGUChar();
				default:
					return -1; // Only the props written below are expected.
			}
		}
		return -1;
	}
} // namespace

TEST_CASE("an RC props set keeps bomb power and the level of an offline account", "[RC][issue83]")
{
	log::rc.disabled = true;
	log::server.disabled = true;

	BabyDI_RELEASE(ITranslationManager);
	BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
	BabyDI_RELEASE(Server);
	BabyDI_PROVIDE(Server, new Server("test"));

	// As msgPLI_RC_PLAYERPROPSSET2 does for an account that is not online.
	auto p = std::make_shared<Player>(nullptr, 0);
	p->account.name = "labflags";
	p->account.level = "onlinestartlocal.nw";
	p->account.character.bombPower = 1;

	CString props = CString() >> (char)PlayerProp::BOMBPOWER >> (char)2
		>> (char)PlayerProp::LEVEL << gstring("cave.nw");
	CString packet = propsSet(props);
	p->setPropsFromRCPacket(packet);

	CHECK(p->account.character.bombPower == 2);
	CHECK(p->account.level == "cave.nw");

	SECTION("the RC view lists bomb power, so a read back shows it")
	{
		REQUIRE(clientPropsForRCView[PROPID(PlayerProp::BOMBPOWER)]);
		PropList only{};
		only[PROPID(PlayerProp::BOMBPOWER)] = true;
		CHECK(bombPowerIn(p->getPropsPacketFromList(only)) == 2);
	}
}
