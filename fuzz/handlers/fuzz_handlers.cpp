// Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 or later (see LICENSE.md).
//
// libFuzzer target (#87): the real packet handlers. Where fuzz/fuzz_process_buffer
// stops at framing and decryption, this one hands already-split packets to
// PlayerClient, PlayerRC and PlayerNC the way the socket loop does, with a client that
// is logged in and standing in a level.
//
// Input layout:
//   byte 0  bits 0-1: 0 or 3 game client, 1 RC, 2 NC; bits 2-7: version pick
//   then, repeated: [id] [length] [length bytes of body]
// The server is made once per process, in the working directory, from a world with
// two small levels. Run it from an empty scratch folder (see fuzz/README.md): RC
// handlers can write files under servers/fuzz/.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

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

std::atomic_bool shutdownProgram{false};

using namespace preagonal;

namespace
{
	// handlePacket is protected; these call it as the socket loop does, and set the
	// version the login would have set.
	struct FuzzClient final : PlayerClient
	{
		using PlayerClient::PlayerClient;
		using PlayerClient::handlePacket;
		void setVersionId(int v) { m_versionId = v; }
	};
	struct FuzzRC final : PlayerRC
	{
		using PlayerRC::PlayerRC;
		using PlayerRC::handlePacket;
		void setVersionId(int v) { m_versionId = v; }
	};
	struct FuzzNC final : PlayerNC
	{
		using PlayerNC::PlayerNC;
		using PlayerNC::handlePacket;
		void setVersionId(int v) { m_versionId = v; }
	};

	constexpr int ClientVersions[] = {
		CLVER_1_39, CLVER_1_411, CLVER_2_22, CLVER_2_31, CLVER_3_153,
		CLVER_4_042, CLVER_4_208, CLVER_5_07, CLVER_5_12, CLVER_6_037,
	};

	void writeFile(const std::filesystem::path& path, const std::string& bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << bytes;
	}

	Server* heldServer()
	{
		static Server* server = []
		{
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;

			BabyDI_RELEASE(ITranslationManager);
			BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());
			// main() provides these; the handlers expect them.
			BabyDI_RELEASE(GuildManager);
			BabyDI_PROVIDE(GuildManager, new GuildManager());
			BabyDI_RELEASE(Server);
			auto* s = new Server("fuzz");
			BabyDI_PROVIDE(Server, s);

			const auto root = std::filesystem::current_path() / "fuzz-world";
			const std::string level =
				"GLEVNW01\n"
				"LINK fuzz2.nw 0 0 2 2 30 30\n"
				"SIGN 10 10\nHello\nSIGNEND\n"
				"CHEST 20 20 greenrupee 0\n"
				"NPC - 30 30\nif (created) { setimg door.png; }\n//#CLIENTSIDE\nif (playerenters) { message hi; }\nNPCEND\n";
			writeFile(root / "fuzz.nw", level);
			writeFile(root / "fuzz2.nw", "GLEVNW01\n");
			s->getFileSystem().bindSingleFile(root / "fuzz.nw");
			s->getFileSystem().bindSingleFile(root / "fuzz2.nw");
			return s;
		}();
		return server;
	}

	template <typename P>
	std::shared_ptr<P> makePlayer(Server* server, int type, int version, const char* name)
	{
		auto p = std::make_shared<P>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
		p->setType(type);
		p->setVersionId(version);
		p->account.name = name;
		server->addPlayer(p, p->getId());
		return p;
	}

	template <typename P>
	void feed(Server* server, const std::shared_ptr<P>& p, const uint8_t* data, size_t size)
	{
		size_t pos = 0;
		for (int count = 0; count < 64 && pos + 2 <= size; ++count)
		{
			const uint8_t id = data[pos];
			const size_t len = data[pos + 1];
			pos += 2;
			const size_t take = std::min(len, size - pos);

			// RAWDATA only announces the size of the next packet.
			if (id == PLI_RAWDATA)
			{
				pos += take;
				continue;
			}

			CString body;
			if (take > 0)
				body.write(reinterpret_cast<const char*>(data + pos), static_cast<int>(take));
			pos += take;

			p->handlePacket(id, body);
		}
		server->deletePlayer(p);
	}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 1 || size > 64 * 1024)
		return 0;

	auto* server = heldServer();
	const uint8_t kind = data[0] & 3;
	const uint8_t pick = data[0] >> 2;
	++data;
	--size;

	switch (kind)
	{
		case 1:
		{
			auto rc = makePlayer<FuzzRC>(server, PLTYPE_RC2, RCVER_2, "FuzzRC");
			// Every right but folder access, so the RC handlers run without touching files
			// outside the scratch world.
			rc->account.adminRights = 0xFFFFFFFFu;
			feed(server, rc, data, size);
			break;
		}
		case 2:
		{
			auto nc = makePlayer<FuzzNC>(server, PLTYPE_NC, NCVER_2_1, "FuzzNC");
			nc->account.adminRights = 0xFFFFFFFFu;
			feed(server, nc, data, size);
			break;
		}
		default:
		{
			const int version = ClientVersions[pick % std::size(ClientVersions)];
			const int type = version < CLVER_2_0 ? PLTYPE_CLIENT : (version < CLVER_4_0211 ? PLTYPE_CLIENT2 : PLTYPE_CLIENT3);
			auto client = makePlayer<FuzzClient>(server, type, version, "Fuzz");
			client->warp("fuzz.nw", LocalPixelPosition{static_cast<int16_t>(30 * 16), static_cast<int16_t>(30 * 16)});
			feed(server, client, data, size);
			break;
		}
	}
	return 0;
}
