// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// startap overrides defaultaccount AP for new accounts (#97).
#include "catch2/catch_all.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <BabyDI.h>
#include <Server.h>
#include <utilities/Log.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;

namespace
{
	struct WorkingDirectory
	{
		explicit WorkingDirectory(const std::filesystem::path& next)
			: previous(std::filesystem::current_path())
		{
			std::filesystem::current_path(next);
		}
		~WorkingDirectory()
		{
			std::error_code error;
			std::filesystem::current_path(previous, error);
		}
		std::filesystem::path previous;
	};

	void writeFile(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path);
		out << text;
	}
}

TEST_CASE("startap is tracked on ExternalServerCachedSettings", "[startap][serveroptions]")
{
	BabyDI_RELEASE(Server);
	auto* server = BabyDI_PROVIDE(Server, new Server("startap-cache"));
	server->prepareSettings();

	CHECK(server->cached.startAP.key == "startap");
	CHECK(server->cached.startAP.getValue() == 50);

	server->getSettings().set("startap", static_cast<uint8_t>(75));
	CHECK(server->cached.startAP.getValue() == 75);
}

TEST_CASE("startap overrides defaultaccount AP for new accounts", "[startap][serveroptions]")
{
	const auto root = std::filesystem::temp_directory_path() / "seihai-startap";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "accounts");
	std::filesystem::create_directories(root / "config");

	writeFile(root / "accounts" / "(defaultaccount).txt",
		"GRACC001\n"
		"NICK Newbie\n"
		"LEVEL start.nw\n"
		"AP 50\n");

	writeFile(root / "config" / "serveroptions.txt",
		"startap = 77\n"
		"heartlimit = 3\n");

	log::networkdump.disabled = true;
	log::npc.disabled = true;
	log::rc.disabled = true;
	log::script.disabled = true;
	log::server.disabled = true;

	WorkingDirectory cwd{root};
	BabyDI_RELEASE(ITranslationManager);
	BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
	BabyDI_RELEASE(Server);
	auto* server = new Server("startap-load");
	BabyDI_PROVIDE(Server, server);
	server->loadServerFileSystem();
	server->prepareSettings();
	server->loadSettings();

	Account account;
	REQUIRE(server->getAccountLoader().loadAccount("brandnew", account));
	CHECK(account.character.ap == 77);
}
