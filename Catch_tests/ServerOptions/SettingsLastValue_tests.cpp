// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include <catch2/catch_all.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <utilities/Settings.h>

using namespace preagonal;

TEST_CASE("Scalar settings preserve missing, singleton and replacement behavior", "[settings][lastvalue]")
{
	Settings settings;
	CHECK_FALSE(settings.get<std::string>("middle"));
	settings.set("middle", std::string{"only"});
	CHECK(settings.get<std::string>("middle").value() == "only");
	CHECK_FALSE(settings.get<std::string>("aaa"));
	CHECK_FALSE(settings.get<std::string>("zzz"));
	settings.set("aaa", std::string{"lower"});
	settings.set("zzz", std::string{"upper"});
	CHECK(settings.get<std::string>("aaa").value() == "lower");
	CHECK(settings.get<std::string>("middle").value() == "only");
	CHECK(settings.get<std::string>("zzz").value() == "upper");
	CHECK_FALSE(settings.get<std::string>("bbb"));
	CHECK_FALSE(settings.get<std::string>("yyy"));
	settings.set(append_list, "middle", std::string{"duplicate"});
	settings.set("middle", std::string{"replacement"});
	CHECK(settings.get<std::string>("middle").value() == "replacement");
	const std::vector<std::string> expected{"replacement"};
	CHECK(settings.get<std::vector<std::string>>("middle").value() == expected);
}

TEST_CASE("Duplicate scalar values select the last entry at every key position", "[settings][lastvalue]")
{
	const bool smaller = GENERATE(false, true);
	const bool greater = GENERATE(false, true);
	CAPTURE(smaller, greater);
	Settings settings;
	if (smaller) settings.set("aaa", std::string{"lower"});
	if (greater) settings.set("zzz", std::string{"upper"});
	settings.set("middle", std::string{"first"});
	settings.set(append_list, "middle", std::string{"last"});
	CHECK(settings.get<std::string>("middle").value() == "last");
	const std::vector<std::string> two{"first", "last"};
	CHECK(settings.get<std::vector<std::string>>("middle").value() == two);
	settings.set(append_list, "middle", std::string{"latest"});
	CHECK(settings.get<std::string>("middle").value() == "latest");
	const std::vector<std::string> three{"first", "last", "latest"};
	CHECK(settings.get<std::vector<std::string>>("middle").value() == three);
}

TEST_CASE("An unrelated greater key does not change the selected scalar value", "[settings][lastvalue]")
{
	Settings settings;
	settings.set("middle", std::string{"first"});
	settings.set(append_list, "middle", std::string{"last"});
	CHECK(settings.get<std::string>("middle").value() == "last");
	settings.set("zzz", std::string{"unrelated"});
	CHECK(settings.get<std::string>("middle").value() == "last");
}

TEST_CASE("Typed scalar settings use the last duplicate value", "[settings][lastvalue]")
{
	SECTION("integer")
	{
		Settings settings;
		settings.set("middle", int32_t{1});
		settings.set(append_list, "middle", int32_t{2});
		CHECK(settings.get<int32_t>("middle").value() == 2);
		settings.set("zzz", std::string{"unrelated"});
		CHECK(settings.get<int32_t>("middle").value() == 2);
	}
	SECTION("boolean")
	{
		Settings settings;
		settings.set("middle", false);
		settings.set(append_list, "middle", true);
		CHECK(settings.get<bool>("middle").value());
		settings.set("zzz", std::string{"unrelated"});
		CHECK(settings.get<bool>("middle").value());
	}
}

TEST_CASE("A setting cache initially binds and subscribes to the last scalar value", "[settings][lastvalue]")
{
	const bool greater = GENERATE(false, true);
	CAPTURE(greater);
	Settings settings;
	if (greater) settings.set("zzz", std::string{"upper"});
	settings.set("middle", int32_t{1});
	settings.set(append_list, "middle", int32_t{2});
	SettingCache<int32_t> cache{"middle", 0};
	settings.track(cache);
	CHECK(settings.get<int32_t>("middle").value() == 2);
	CHECK(cache.getValue() == 2);
	settings.set(append_list, "middle", int32_t{3});
	CHECK(settings.get<int32_t>("middle").value() == 3);
	CHECK(cache.getValue() == 3);
}
