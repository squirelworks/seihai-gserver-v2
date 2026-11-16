// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// scriptstyle alias for default_scriptengine (#97).
#include "catch2/catch_all.hpp"

#include <atomic>
#include <optional>
#include <string>

#include <BabyDI.h>
#include <Server.h>

using namespace preagonal;
using namespace std::string_literals;

std::atomic_bool shutdownProgram{false};

namespace
{
	[[nodiscard]] std::string resolveDefaultScriptEngine(
		const std::optional<std::string>& defaultScriptEngine,
		const std::optional<std::string>& scriptStyle,
		const std::string& fallback = "gs1")
	{
		if (defaultScriptEngine.has_value())
			return *defaultScriptEngine;
		if (scriptStyle.has_value())
			return *scriptStyle;
		return fallback;
	}
}

TEST_CASE("scriptstyle is tracked and aliases default_scriptengine", "[scriptstyle][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("scriptstyle"));
	server->prepareSettings();

	CHECK(server->cached.scriptStyle.key == "scriptstyle");
	CHECK(server->cached.scriptStyle.getValue() == "gs1");

	server->getSettings().set("scriptstyle", "gs2"s);
	CHECK(server->cached.scriptStyle.getValue() == "gs2");
}

TEST_CASE("scriptstyle fills in when default_scriptengine is unset", "[scriptstyle][serveroptions]")
{
	CHECK(resolveDefaultScriptEngine("gs2"s, "gs1"s) == "gs2");
	CHECK(resolveDefaultScriptEngine(std::nullopt, "gs2"s) == "gs2");
	CHECK(resolveDefaultScriptEngine(std::nullopt, std::nullopt) == "gs1");
}
