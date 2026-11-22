// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include "catch2/catch_all.hpp"

#include <cstdint>
#include <string>
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
TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Activated sword powers preserve property roundtrip and cursor", "[serveroptions][weaponlimits][propertycodec]")
{
	server->prepareSettings();
	server->getSettings().set("swordlimit", int8_t{127});
	server->getSettings().set("healswords", true);
	REQUIRE(server->cached.swordPowerLimit.getValue() == 127);
	REQUIRE(server->cached.enableHealingSwords.getValue());
	const auto power = static_cast<int8_t>(GENERATE(-3, -1, 0, 3, 4, 6, 97, 98, 127));
	CAPTURE(power);
	const std::string image = power == 0 ? "" : "edge.png";
	const props::PropertySwordPower original{image, power};
	auto wire = original.serialize();
	wire << "!";
	props::PropertySwordPower decoded;
	decoded.deserialize(wire);
	REQUIRE(decoded.power.has_value());
	CHECK(decoded.power.value() == power);
	CHECK(decoded.image == image);
	CHECK(wire.bytesLeft() == 1);
	CHECK(wire.readChar() == '!');
}

TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Activated shield powers preserve property roundtrip and cursor", "[serveroptions][weaponlimits][propertycodec]")
{
	server->prepareSettings();
	server->getSettings().set("shieldlimit", uint8_t{255});
	REQUIRE(server->cached.shieldPowerLimit.getValue() == 255);
	const auto power = static_cast<uint8_t>(GENERATE(0, 3, 4, 6, 213, 214, 223, 224, 245, 246, 255));
	CAPTURE(power);
	const std::string image = power == 0 ? "" : "edge.png";
	const props::PropertyShieldPower original{image, power};
	auto wire = original.serialize();
	wire << "!";
	props::PropertyShieldPower decoded;
	decoded.deserialize(wire);
	REQUIRE(decoded.power.has_value());
	CHECK(decoded.power.value() == power);
	CHECK(decoded.image == image);
	CHECK(wire.bytesLeft() == 1);
	CHECK(wire.readChar() == '!');
}
TEST_CASE_METHOD(WeaponLimitSettingsFixture, "Compact positive weapon powers preserve canonical images and cursor", "[serveroptions][weaponlimits][propertycodec]")
{
	server->prepareSettings();
	SECTION("sword")
	{
		auto wire = props::PropertySwordPower{int8_t{3}}.serialize();
		wire << "!";
		props::PropertySwordPower decoded;
		decoded.deserialize(wire);
		REQUIRE(decoded.power.has_value());
		CHECK(decoded.power.value() == 3);
		CHECK(decoded.image == "sword3." + std::string(props::Limits::defaultImageExtension()));
		CHECK(wire.bytesLeft() == 1);
		CHECK(wire.readChar() == '!');
	}
	SECTION("shield")
	{
		auto wire = props::PropertyShieldPower{uint8_t{3}}.serialize();
		wire << "!";
		props::PropertyShieldPower decoded;
		decoded.deserialize(wire);
		REQUIRE(decoded.power.has_value());
		CHECK(decoded.power.value() == 3);
		CHECK(decoded.image == "shield3." + std::string(props::Limits::defaultImageExtension()));
		CHECK(wire.bytesLeft() == 1);
		CHECK(wire.readChar() == '!');
	}
}
