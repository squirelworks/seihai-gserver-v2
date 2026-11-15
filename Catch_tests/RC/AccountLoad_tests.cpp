// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
// Account and NPC files often store whole numbers with a decimal point, and a saved
// status can carry hidden, sword-hidden, dead or paused. Those load and save without them.
#include "catch2/catch_all.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <IEnums.h>

#include <BabyDI.h>
#include <Server.h>
#include <filesystem/File.h>
#include <object/NPC.h>
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

	// The file watcher started by loadServerFileSystem is still handling the account
	// save when the test ends. Destroying the server then races that thread, so the
	// server is left in place for the rest of the process.
	struct ServerHold
	{
		Server* server = nullptr;
		ServerHold()
		{
			BabyDI_RELEASE(ITranslationManager);
			BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
			BabyDI_RELEASE(Server);
			server = new Server("account-load");
			BabyDI_PROVIDE(Server, server);
			server->loadServerFileSystem();
		}
	};
} // namespace

TEST_CASE("account and NPC loaders accept decimals and drop transient status bits", "[account][npc]")
{
	const auto root = std::filesystem::temp_directory_path() / "seihai-account-load";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "accounts");
	std::filesystem::create_directories(root / "npcs");

	const auto dropped = static_cast<uint8_t>(PLSTATUS_HIDDEN | PLSTATUS_HIDESWORD | PLSTATUS_DEAD | PLSTATUS_PAUSED);
	const auto kept = static_cast<uint8_t>(PLSTATUS_ALLOWWEAPONS);
	const auto combined = static_cast<uint8_t>(dropped | kept);

	writeFile(root / "accounts" / "statustest.txt",
		"GRACC001\n"
		"NICK Floaty\n"
		"LEVEL start.nw\n"
		"MAXHP 99.50\n"
		"HP 0.00\n"
		"GRALATS 15.90\n"
		"ARROWS 4.20\n"
		"ONSECS 8.50\n"
		"KILLS 3.10\n"
		"DEATHS 2.90\n"
		"STATUS " + std::to_string(combined) + "\n");

	writeFile(root / "npcs" / "npcfloaty.txt",
		"GRNPC001\n"
		"NAME floaty\n"
		"ARROWS 4.20\n"
		"GLOVEP 2.90\n"
		"SWORDP -1.20\n"
		"BOMBS 3.00\n"
		"GRALATS 15.90\n");

	log::networkdump.disabled = true;
	log::npc.disabled = true;
	log::rc.disabled = true;
	log::script.disabled = true;
	log::server.disabled = true;

	WorkingDirectory cwd{root};
	ServerHold held;
	Server* server = held.server;

	Account account;
	REQUIRE(server->getAccountLoader().loadAccount("statustest", account));
	CHECK(account.maxHitpoints == server->cached.maxHeartLimit.getValue());
	CHECK(account.character.hitpointsInHalves == 1);
	CHECK(account.character.gralats == 15);
	CHECK(account.character.arrows == 4);
	CHECK(account.onlineSeconds == 8);
	CHECK(account.kills == 3);
	CHECK(account.deaths == 2);
	CHECK(account.status == kept);
	CHECK((account.status & dropped) == 0);

	account.status = combined;
	REQUIRE(server->getAccountLoader().forceSaveAccount(account));

	Account again;
	REQUIRE(server->getAccountLoader().loadAccount("statustest", again));
	CHECK(again.status == kept);
	CHECK((again.status & dropped) == 0);

	// The path loader reads the header before the body and does not clear eof after rewinding,
	// so the body parse is driven from a fresh file.
	fs::File npcFile{std::filesystem::path{"npcs/npcfloaty.txt"}};
	REQUIRE(npcFile.opened());
	auto npc = std::make_shared<NPC>(1, NPCStorageType::DATABASE);
	server->getNPCLoader().loadNPC(npcFile, npc, server->getServerStartTime());
	CHECK(npc->character.arrows == 4);
	CHECK(npc->character.glovePower == 2);
	CHECK(npc->character.swordPower == -1);
	CHECK(npc->character.bombs == 3);
	CHECK(npc->character.gralats == 15);
}
