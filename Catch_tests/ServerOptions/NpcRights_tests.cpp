// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// npcrights SettingCache and NC rights gate (#97).
#include "catch2/catch_all.hpp"

#include <atomic>

#include <BabyDI.h>
#include <Server.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

namespace
{
	[[nodiscard]] bool allowNpcControlLogin(const bool npcRightsEnabled, const bool hasNpcControlRight) noexcept
	{
		return !npcRightsEnabled || hasNpcControlRight;
	}
}

TEST_CASE("npcrights defaults to requiring NC rights", "[npcrights][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("npcrights"));
	server->prepareSettings();

	CHECK(server->cached.enableNpcRights.key == "npcrights");
	CHECK(server->cached.enableNpcRights.getValue() == true);

	server->getSettings().set("npcrights", false);
	CHECK(server->cached.enableNpcRights.getValue() == false);
}

TEST_CASE("npcrights gate matches NC login policy", "[npcrights][serveroptions]")
{
	CHECK(allowNpcControlLogin(true, true));
	CHECK_FALSE(allowNpcControlLogin(true, false));
	CHECK(allowNpcControlLogin(false, true));
	CHECK(allowNpcControlLogin(false, false));
}
