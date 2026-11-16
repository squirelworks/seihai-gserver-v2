// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// translatedlanguages / defaultlanguage stubs (#97).
#include "catch2/catch_all.hpp"

#include <atomic>
#include <string>
#include <vector>

#include <BabyDI.h>
#include <Server.h>

using namespace preagonal;
using namespace std::string_literals;

std::atomic_bool shutdownProgram{false};

TEST_CASE("translation keys are tracked", "[translation][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("translation-keys"));
	server->prepareSettings();

	CHECK(server->cached.defaultLanguage.key == "defaultlanguage");
	CHECK(server->cached.translatedLanguages.key == "translatedlanguages");
	CHECK(server->cached.defaultLanguage.getValue() == "en");
	CHECK(server->cached.translatedLanguages.getValue().empty());

	server->getSettings().set("defaultlanguage", "de"s);
	server->getSettings().set("translatedlanguages", "en,de"s);
	CHECK(server->cached.defaultLanguage.getValue() == "de");
	REQUIRE(server->cached.translatedLanguages.getValue().size() == 2);
	CHECK(server->cached.translatedLanguages.getValue()[0] == "en");
	CHECK(server->cached.translatedLanguages.getValue()[1] == "de");
}
