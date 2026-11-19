// Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// libFuzzer target over the real client packet handlers (#87). Where
// fuzz_process_buffer stops at the framing with a stub handler, this one feeds
// decoded packets to PlayerClient::handlePacket on a Server with no socket, as
// Catch_tests/ShortPackets does for empty bodies. Built from the main project with
// -DFUZZ_HANDLERS=ON (Clang, with SANITIZE=ON); see fuzz/README.md.
//
// RC and NC handlers too: two more picks of the first byte make the player a
// PlayerRC or PlayerNC with every admin right (but no folder rights, so the file
// browser stays inside the scratch world). Game clients stand in a small level.
//
// Input: one byte picking the client type and version, then records of
//   [id byte] [body length byte] [body]
// A record that runs past the end takes what is left. Ids are taken mod 224;
// RAWDATA (it sizes the next packet, which has no meaning here) is skipped.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <CSocket.h>
#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>

#include <BabyDI.h>
#include <Server.h>
#include <player/PlayerClient.h>
#include <player/PlayerNC.h>
#include <player/PlayerRC.h>
#include <utilities/Log.h>
#include <utilities/manager/GuildManager.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

namespace
{
	// handlePacket is protected; the socket loop calls it the same way.
	struct FuzzClient : PlayerClient
	{
		using PlayerClient::PlayerClient;
		using PlayerClient::handlePacket;
		void setVersionForFuzz(int v) { m_versionId = v; }
	};
	struct FuzzRC : PlayerRC
	{
		using PlayerRC::PlayerRC;
		using PlayerRC::handlePacket;
		void setVersionForFuzz(int v) { m_versionId = v; }
	};
	struct FuzzNC : PlayerNC
	{
		using PlayerNC::PlayerNC;
		using PlayerNC::handlePacket;
		void setVersionForFuzz(int v) { m_versionId = v; }
	};

	void writeFile(const std::filesystem::path& path, const std::string& bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << bytes;
	}

	Server* theServer()
	{
		static Server* server = [] {
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;
			BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
			auto* server = BabyDI_PROVIDE(Server, new Server("fuzz"));
			// main.cpp provides the guild manager next to the server.
			BabyDI_PROVIDE(GuildManager, new GuildManager());

			// A small level for game clients to stand in: a link, a sign, a chest and an NPC.
			const auto root = std::filesystem::current_path() / "fuzz-world";
			writeFile(root / "fuzz.nw",
				"GLEVNW01\n"
				"LINK fuzz2.nw 0 0 2 2 30 30\n"
				"SIGN 10 10\nHello\nSIGNEND\n"
				"CHEST 20 20 greenrupee 0\n"
				"NPC - 30 30\nif (created) { setimg door.png; }\n//#CLIENTSIDE\nif (playerenters) { message hi; }\nNPCEND\n");
			writeFile(root / "fuzz2.nw", "GLEVNW01\n");
			server->getFileSystem().bindSingleFile(root / "fuzz.nw");
			server->getFileSystem().bindSingleFile(root / "fuzz2.nw");

			// The account loader falls back to this file; without a LEVEL, an RC Add
			// Account stops on forceSaveAccount's debug assert.
			const std::string defaultAccount = "GRACC001\nLEVEL fuzz.nw\nX 30\nY 30\n";
			writeFile(std::filesystem::current_path() / "accounts" / "(defaultaccount).txt", defaultAccount);
			writeFile(std::filesystem::current_path() / "servers" / "fuzz" / "accounts" / "(defaultaccount).txt", defaultAccount);
			return server;
		}();
		return server;
	}

	// The client generations the handlers branch on, each with a version of that era.
	struct Kind
	{
		int type;
		int version;
	};
	constexpr Kind kKinds[] = {
		{PLTYPE_CLIENT, CLVER_1_39},	{PLTYPE_CLIENT, CLVER_2_0},	{PLTYPE_CLIENT, CLVER_2_22},
		{PLTYPE_CLIENT2, CLVER_4_042},	{PLTYPE_CLIENT2, CLVER_5_07}, {PLTYPE_CLIENT3, CLVER_6_037},
	};
}

template <typename P>
static void feed(Server* server, const std::shared_ptr<P>& player, const uint8_t* data, size_t size)
{
	size_t at = 1;
	while (at + 2 <= size)
	{
		const uint8_t id = data[at] % 224;
		size_t len = data[at + 1];
		at += 2;
		if (len > size - at)
			len = size - at;
		if (id == PLI_RAWDATA)
		{
			at += len;
			continue;
		}
		CString body;
		if (len > 0)
			body.write(reinterpret_cast<const char*>(data + at), static_cast<int>(len));
		at += len;
		player->handlePacket(id, body);
	}
	server->deletePlayer(player);
}

template <typename P>
static std::shared_ptr<P> makePlayer(Server* server, int type, int version, const char* name)
{
	auto player = std::make_shared<P>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
	player->setType(type);
	player->setVersionForFuzz(version);
	player->account.name = name;
	server->addPlayer(player, player->getId());
	return player;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 1 || size > 64 * 1024)
		return 0;

	Server* server = theServer();
	const size_t pick = data[0] % (std::size(kKinds) + 2);
	if (pick == std::size(kKinds))
	{
		auto rc = makePlayer<FuzzRC>(server, PLTYPE_RC2, RCVER_2, "FuzzRC");
		rc->account.adminRights = 0xFFFFFFFFu;
		feed(server, rc, data, size);
	}
	else if (pick == std::size(kKinds) + 1)
	{
		auto nc = makePlayer<FuzzNC>(server, PLTYPE_NC, NCVER_2_1, "FuzzNC");
		nc->account.adminRights = 0xFFFFFFFFu;
		feed(server, nc, data, size);
	}
	else
	{
		const Kind& kind = kKinds[pick];
		auto client = makePlayer<FuzzClient>(server, kind.type, kind.version, "Fuzz");
		client->warp("fuzz.nw", LocalPixelPosition{static_cast<int16_t>(30 * 16), static_cast<int16_t>(30 * 16)});
		feed(server, client, data, size);
	}
	return 0;
}
