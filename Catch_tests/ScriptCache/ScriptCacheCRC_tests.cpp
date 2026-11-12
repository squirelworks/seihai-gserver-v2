// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: issue #24. The CRC the server announces for a cached weapon or class must be
// the CRC32 of the body the client caches (the bytecode it was sent), or the client's cache
// check never matches and every login downloads every script again.
#include <catch2/catch_all.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <BabyDI.h>
#include <CString.h>
#include <IEnums.h>
#include <Server.h>
#include <object/Weapon.h>
#include <scripting/ScriptClass.h>
#include <utilities/Log.h>
#include <utilities/StringUtils.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;
using namespace std::string_literals;
using namespace std::string_view_literals;

std::atomic_bool shutdownProgram{false};

namespace
{
	struct ModernServer
	{
		ModernServer()
		{
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;

			BabyDI_RELEASE(ITranslationManager);
			BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());

			BabyDI_RELEASE(Server);
			server = BabyDI_PROVIDE(Server, new Server("test"));
			server->Generation = ServerGeneration::MODERN;
			server->getSettings().set("serverside", true);
			server->getSettings().set("default_scriptengine", "gs2"s);
			server->loadNPCServer();
		}

		Server* server = nullptr;
	};

	uint32_t crcOf(const ScriptByteCode& body)
	{
		auto crc = crc32(0L, Z_NULL, 0);
		return static_cast<uint32_t>(crc32(crc, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size())));
	}

	uint32_t readGInt5(const std::string& field)
	{
		CString value{field};
		return static_cast<uint32_t>(value.readGInt5());
	}

	constexpr std::string_view script = R"(function onCreated() {
  this.y = 2;
}
//#CLIENTSIDE
function onCreated() {
  this.x = 1;
  echo("cache test");
}
)";
} // namespace

TEST_CASE_METHOD(ModernServer, "a weapon's PLO_LOADSCRIPT CRC is the CRC32 of the bytecode PLO_WEAPONBYTECODE2 carries", "[issue24]")
{
	const Weapon weapon{"CacheTest"sv, "block.png"sv, script};
	const auto& bytecode = weapon.getScript().getClientByteCode();
	REQUIRE_FALSE(bytecode.empty());

	// type,name,cacheflag,key,crc
	const auto parts = string::fromCSV(weapon.getHeaderWithCRC());
	REQUIRE(parts.size() == 5);
	CHECK(parts[0] == "weapon");
	CHECK(parts[1] == "CacheTest");
	CHECK(parts[2] == "1");
	CHECK(parts[3].size() == 10);
	CHECK(readGInt5(parts[4]) == crcOf(bytecode));
}

TEST_CASE_METHOD(ModernServer, "a class's checksum is the CRC32 of the bytecode its class packet carries", "[issue24]")
{
	const ScriptClass scriptClass{"cachetestclass"sv, script};
	const auto& bytecode = scriptClass.getScript().getClientByteCode();
	REQUIRE_FALSE(bytecode.empty());

	// PLI_UPDATECLASS sends the CRC of the client's cached body; the server compares it with this.
	CHECK(scriptClass.getCheckSum() == crcOf(bytecode));

	// The 5.07+ class packet: id, GCHAR header length, header (class,name,cacheflag,key,crc), bytecode.
	auto packet = scriptClass.getClassPacket(CLVER_6_037);
	REQUIRE(packet.length() > 2);
	CHECK(packet.readGUChar() == PLO_LOADSCRIPT);
	const auto headerLength = packet.readGUChar();
	const auto parts = string::fromCSV(packet.readChars(headerLength).toString());
	REQUIRE(parts.size() == 5);
	CHECK(readGInt5(parts[4]) == crcOf(bytecode));
	const auto body = packet.readChars(packet.bytesLeft());
	CHECK(static_cast<size_t>(body.length()) == bytecode.size());
}
