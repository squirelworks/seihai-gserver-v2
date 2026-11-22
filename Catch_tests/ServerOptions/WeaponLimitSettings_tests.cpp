// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include "catch2/catch_all.hpp"

#include <cstdint>

#include <BabyDI.h>
#include <Server.h>
#include <utilities/Log.h>
#include <utilities/PropertySerializers.h>

using namespace preagonal;

namespace
{
	struct WeaponLimitSettingsFixture
	{
		WeaponLimitSettingsFixture()
		{
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;
			BabyDI_RELEASE(Server);
			server = BabyDI_PROVIDE(Server, new Server("weapon-limit-settings"));
		}

		~WeaponLimitSettingsFixture()
		{
			BabyDI_RELEASE(Server);
		}

		Server* server;
	};
}

TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Weapon limits retain defaults without configured values", "[serveroptions][weaponlimits]")
{
	server->prepareSettings();
	CHECK_FALSE(server->getSettings().exists("swordlimit"));
	CHECK_FALSE(server->getSettings().exists("shieldlimit"));
	CHECK_FALSE(server->getSettings().exists("healswords"));
	CHECK(server->cached.swordPowerLimit.getValue() == 3);
	CHECK(server->cached.shieldPowerLimit.getValue() == 3);
	CHECK_FALSE(server->cached.enableHealingSwords.getValue());
	CHECK(props::Limits::applySwordPower(9) == 3);
	CHECK(props::Limits::applySwordPower(-9) == 0);
	CHECK(props::Limits::applyShieldPower(9) == 3);
}

TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Weapon limits bind values configured before prepareSettings", "[serveroptions][weaponlimits]")
{
	auto& settings = server->getSettings();
	settings.set("swordlimit", int8_t{6});
	settings.set("shieldlimit", uint8_t{5});
	settings.set("healswords", true);
	REQUIRE(settings.get<int8_t>("swordlimit").value() == 6);
	REQUIRE(settings.get<uint8_t>("shieldlimit").value() == 5);
	REQUIRE(settings.get<bool>("healswords").value());

	server->prepareSettings();
	CHECK(server->cached.swordPowerLimit.getValue() == 6);
	CHECK(server->cached.shieldPowerLimit.getValue() == 5);
	CHECK(server->cached.enableHealingSwords.getValue());
	CHECK(props::Limits::applySwordPower(9) == 6);
	CHECK(props::Limits::applySwordPower(-9) == -6);
	CHECK(props::Limits::applySwordPower(4) == 4);
	CHECK(props::Limits::applyShieldPower(9) == 5);
	CHECK(props::Limits::applyShieldPower(4) == 4);
}

TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Weapon limits follow later Settings subscription updates", "[serveroptions][weaponlimits]")
{
	server->prepareSettings();
	auto& settings = server->getSettings();
	settings.set("swordlimit", int8_t{6});
	settings.set("shieldlimit", uint8_t{5});
	settings.set("healswords", true);
	REQUIRE(settings.get<int8_t>("swordlimit").value() == 6);
	REQUIRE(settings.get<uint8_t>("shieldlimit").value() == 5);
	REQUIRE(settings.get<bool>("healswords").value());
	CHECK(server->cached.swordPowerLimit.getValue() == 6);
	CHECK(server->cached.shieldPowerLimit.getValue() == 5);
	CHECK(server->cached.enableHealingSwords.getValue());
	CHECK(props::Limits::applySwordPower(9) == 6);
	CHECK(props::Limits::applySwordPower(-9) == -6);
	CHECK(props::Limits::applyShieldPower(9) == 5);

	settings.set("swordlimit", int8_t{2});
	settings.set("shieldlimit", uint8_t{1});
	settings.set("healswords", false);
	REQUIRE(settings.get<int8_t>("swordlimit").value() == 2);
	REQUIRE(settings.get<uint8_t>("shieldlimit").value() == 1);
	REQUIRE_FALSE(settings.get<bool>("healswords").value());
	CHECK(server->cached.swordPowerLimit.getValue() == 2);
	CHECK(server->cached.shieldPowerLimit.getValue() == 1);
	CHECK_FALSE(server->cached.enableHealingSwords.getValue());
	CHECK(props::Limits::applySwordPower(9) == 2);
	CHECK(props::Limits::applySwordPower(-9) == 0);
	CHECK(props::Limits::applyShieldPower(9) == 1);
}
