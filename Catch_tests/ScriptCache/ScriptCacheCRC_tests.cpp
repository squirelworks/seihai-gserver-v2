// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: issue #24. The CRC the server announces for a cached weapon or class must be
// the CRC32 of the body the client caches, or the client's cache check never matches and every
// login downloads every script again. For a weapon that body is the bytecode DES-encrypted with
// the header's key (the GRESCR01 file a real 6.03r7 wrote in the 2026-11-12 lab run).
#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
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
#include <utilities/ScriptCacheDes.h>
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

	// The header's key field: two GINT5 words, as the 8 key bytes (each word little-endian).
	std::array<uint8_t, 8> keyOf(const std::string& field)
	{
		CString value{field};
		const auto k0 = static_cast<uint32_t>(value.readGInt5());
		const auto k1 = static_cast<uint32_t>(value.readGInt5());
		std::array<uint8_t, 8> key{};
		for (size_t i = 0; i < 4; ++i)
		{
			key[i] = static_cast<uint8_t>(k0 >> (8 * i));
			key[4 + i] = static_cast<uint8_t>(k1 >> (8 * i));
		}
		return key;
	}

	std::vector<uint8_t> fromHex(std::string_view hex)
	{
		std::vector<uint8_t> out;
		for (size_t i = 0; i + 1 < hex.size(); i += 2)
			out.push_back(static_cast<uint8_t>(std::stoi(std::string{hex.substr(i, 2)}, nullptr, 16)));
		return out;
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

TEST_CASE("the script cache DES matches the standard and a real 6.03r7 cache file", "[issue24]")
{
	// FIPS 46 vector (key 133457799BBCDFF1): the client takes each key byte bit-reversed, so pass it reversed.
	const std::array<uint8_t, 8> fipsKey{0xC8, 0x2C, 0xEA, 0x9E, 0xD9, 0x3D, 0xFB, 0x8F};
	const auto fipsPlain = fromHex("0123456789ABCDEF");
	const auto fips = seihai::scriptCacheEncrypt(std::string_view{reinterpret_cast<const char*>(fipsPlain.data()), fipsPlain.size()}, fipsKey);
	CHECK(fips == fromHex("85E813540F0AB405"));

	// Lab run 2026-11-12: a weapon's bytecode, the key words 0xce36ece6 / 0x036d6a2d from its 197, and the CRC32
	// of the GRESCR01 body the real Linux 6.03r7 then stored (bytes after the magic).
	const auto bytecode = fromHex(
		"0000000100000004000000000000000200000020000000016f6e437265617465"
		"64000000000c6f6e506c61796572436861747300000000030000001c6c616263"
		"6f756e740063686174006c616267733200677332206f6b000000000400000036"
		"01f4001d17330ab416f0002314f3013214f3000701f4001d17330ab616f00123"
		"15f0024604f4001ab616f0012315f0033214f30007070a");
	REQUIRE(bytecode.size() == 151);
	const std::array<uint8_t, 8> key{0xe6, 0xec, 0x36, 0xce, 0x2d, 0x6a, 0x6d, 0x03};
	const auto stored = seihai::scriptCacheEncrypt(std::string_view{reinterpret_cast<const char*>(bytecode.data()), bytecode.size()}, key);
	CHECK(stored.size() == 151);
	CHECK(std::equal(stored.end() - 7, stored.end(), bytecode.end() - 7)); // the 7-byte tail is not encrypted
	CHECK(crcOf(stored) == 0x4e195a69u);
}

TEST_CASE_METHOD(ModernServer, "a weapon's PLO_LOADSCRIPT CRC is the CRC32 of the stored (encrypted) bytecode", "[issue24]")
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
	const auto stored = seihai::scriptCacheEncrypt(std::string_view{reinterpret_cast<const char*>(bytecode.data()), bytecode.size()}, keyOf(parts[3]));
	CHECK(readGInt5(parts[4]) == crcOf(stored));
	CHECK(readGInt5(parts[4]) != crcOf(bytecode));
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

TEST_CASE_METHOD(ModernServer, "PLO_WEAPONHASH carries the CRC32 of the bytecode that PLO_WEAPONBYTECODE sends", "[issue26]")
{
	// 4.04r2 to 4.20r8 write the bytes after the name in 132 to weblevels/weapons/<name>.code, and on 193 compare
	// the zlib CRC32 of that file with the GINT5 (Graal_analysis protocol/unknown-ids-2026-11-06.md).
	const Weapon weapon{"CacheTest"sv, "block.png"sv, script};
	const auto& bytecode = weapon.getScript().getClientByteCode();
	REQUIRE_FALSE(bytecode.empty());

	auto packet = weapon.getWeaponHashPacket();
	REQUIRE(packet.length() > 6);
	CHECK(packet.readGUChar() == PLO_WEAPONHASH);
	const auto crc = static_cast<uint32_t>(packet.readGInt5());
	CHECK(packet.readChars(packet.bytesLeft()).toString() == "CacheTest");
	CHECK(crc == crcOf(bytecode));

	// Not the GRESCR01 CRC that 5.07 and later get in PLO_LOADSCRIPT.
	const auto parts = string::fromCSV(weapon.getHeaderWithCRC());
	REQUIRE(parts.size() == 5);
	CHECK(crc != readGInt5(parts[4]));
}
