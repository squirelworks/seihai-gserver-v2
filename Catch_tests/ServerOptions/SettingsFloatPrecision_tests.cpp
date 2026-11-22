// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include <catch2/catch_all.hpp>

#include <optional>
#include <string>
#include <type_traits>

#include <utilities/Settings.h>

using namespace preagonal;

TEST_CASE("Floating settings retain the requested float or double type and precision", "[settings][precision]")
{
	Settings settings;
	CHECK_FALSE(settings.get<float>("missing"));
	CHECK_FALSE(settings.get<double>("missing"));
	settings.set("precise", std::string{"16777217"});
	const auto raw = settings.get<std::string>("precise");
	REQUIRE(raw.has_value());
	CHECK(raw.value() == "16777217");
	CHECK((std::is_same_v<decltype(settings.get<float>("precise")), std::optional<float>>));
	CHECK((std::is_same_v<decltype(settings.get<double>("precise")), std::optional<double>>));
	const auto precise = settings.get<double>("precise");
	REQUIRE(precise.has_value());
	CHECK(precise.value() == 16777217.0);
	const auto rounded = settings.get<float>("precise");
	REQUIRE(rounded.has_value());
	CHECK(rounded.value() == 16777216.0f);
}

TEST_CASE("Double setting caches preserve precision on binding and later updates", "[settings][precision]")
{
	const bool configuredBeforeBinding = GENERATE(false, true);
	CAPTURE(configuredBeforeBinding);
	Settings settings;
	if (configuredBeforeBinding)
		settings.set("precise", std::string{"16777217"});
	SettingCache<double> precise{"precise", 0.25};
	SettingCache<float> rounded{"precise", 0.25f};
	settings.track(precise);
	settings.track(rounded);
	if (!configuredBeforeBinding)
	{
		CHECK(precise.getValue() == 0.25);
		CHECK(rounded.getValue() == 0.25f);
		settings.set("precise", std::string{"16777217"});
	}
	CHECK(settings.get<std::string>("precise").value() == "16777217");
	CHECK(precise.getValue() == 16777217.0);
	CHECK(rounded.getValue() == 16777216.0f);
	settings.set("precise", std::string{"16777219"});
	CHECK(settings.get<std::string>("precise").value() == "16777219");
	CHECK(precise.getValue() == 16777219.0);
	CHECK(rounded.getValue() == 16777220.0f);
}
