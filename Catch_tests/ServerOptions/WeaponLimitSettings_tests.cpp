// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include "catch2/catch_all.hpp"

#include <cstdint>
#include <tuple>

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
TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Signed sword limits have ordered effective bounds", "[serveroptions][weaponlimits]")
{
	const auto [configured, maximum, healingMinimum] = GENERATE(Catch::Generators::table<int8_t, int8_t, int8_t>({
		{-128, 3, -3}, {-1, 3, -3}, {0, 0, 0}, {6, 6, -6}, {127, 127, -127}
	}));
	const bool healing = GENERATE(false, true);
	const bool beforeBinding = GENERATE(false, true);
	CAPTURE(configured, healing, beforeBinding);

	auto& settings = server->getSettings();
	if (!beforeBinding)
		server->prepareSettings();
	settings.set("swordlimit", configured);
	settings.set("healswords", healing);
	if (beforeBinding)
		server->prepareSettings();

	REQUIRE(settings.get<int8_t>("swordlimit").value() == configured);
	REQUIRE(settings.get<bool>("healswords").value() == healing);
	CHECK(server->cached.swordPowerLimit.getValue() == configured);
	CHECK(server->cached.enableHealingSwords.getValue() == healing);
	CHECK(props::Limits::applySwordPower(int8_t{-128}) == (healing ? healingMinimum : 0));
	CHECK(props::Limits::applySwordPower(int8_t{127}) == maximum);
	CHECK(props::Limits::applySwordPower(int8_t{0}) == 0);

	settings.set("swordlimit", int8_t{2});
	REQUIRE(settings.get<int8_t>("swordlimit").value() == 2);
	CHECK(server->cached.swordPowerLimit.getValue() == 2);
	CHECK(props::Limits::applySwordPower(int8_t{127}) == 2);
	CHECK(props::Limits::applySwordPower(int8_t{-128}) == (healing ? -2 : 0));
}
