// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// aptime0..aptime5 band index and SettingCache wiring (#96).
#include "catch2/catch_all.hpp"

#include <atomic>
#include <cstdint>

#include <BabyDI.h>
#include <Server.h>
#include <utilities/APSystem.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

TEST_CASE("apSystemThresholdIndex covers aptime0..aptime5", "[apsystem][serveroptions]")
{
	STATIC_REQUIRE(APSystemThresholdCount == 6);

	CHECK(apSystemThresholdIndex(0) == 0);
	CHECK(apSystemThresholdIndex(19) == 0);
	CHECK(apSystemThresholdIndex(20) == 1);
	CHECK(apSystemThresholdIndex(39) == 1);
	CHECK(apSystemThresholdIndex(40) == 2);
	CHECK(apSystemThresholdIndex(59) == 2);
	CHECK(apSystemThresholdIndex(60) == 3);
	CHECK(apSystemThresholdIndex(79) == 3);
	CHECK(apSystemThresholdIndex(80) == 4);
	CHECK(apSystemThresholdIndex(89) == 4);
	CHECK(apSystemThresholdIndex(90) == 5);
	CHECK(apSystemThresholdIndex(100) == 5);
}

TEST_CASE("aptime5 is tracked on ExternalServerCachedSettings", "[apsystem][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("aptime5"));
	server->prepareSettings();

	CHECK(server->cached.apSystemThresholdSeconds.size() == APSystemThresholdCount);
	CHECK(server->cached.apSystemThresholdSeconds[5].key == "aptime5");
	CHECK(server->cached.apSystemThresholdSeconds[5].getValue() == 1200);

	server->getSettings().set("aptime5", static_cast<uint16_t>(1800));
	CHECK(server->cached.apSystemThresholdSeconds[5].getValue() == 1800);
}
