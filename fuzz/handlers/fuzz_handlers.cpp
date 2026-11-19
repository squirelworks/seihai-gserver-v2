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
// Input: one byte picking the client type and version, then records of
//   [id byte] [body length byte] [body]
// A record that runs past the end takes what is left. Ids are taken mod 224;
// RAWDATA (it sizes the next packet, which has no meaning here) is skipped.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <CSocket.h>
#include <CString.h>
#include <IEnums.h>

#include <BabyDI.h>
#include <Server.h>
#include <player/PlayerClient.h>
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 1 || size > 64 * 1024)
		return 0;

	Server* server = theServer();
	auto client = std::make_shared<FuzzClient>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
	const Kind& kind = kKinds[data[0] % std::size(kKinds)];
	client->setType(kind.type);
	client->setVersionForFuzz(kind.version);
	client->account.name = "Fuzz";
	server->addPlayer(client, client->getId());

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
		client->handlePacket(id, body);
	}

	server->deletePlayer(client);
	return 0;
}
