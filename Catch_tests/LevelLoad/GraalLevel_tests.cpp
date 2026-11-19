// Added by the Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
// GR-V1.05 levels: the one-byte layer count, extra-layer empty tiles, and malformed
// files that used to end the process.
#include "catch2/catch_all.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <BabyDI.h>
#include <Server.h>
#include <filesystem/File.h>
#include <filesystem/FileSystem.h>
#include <level/Level.h>
#include <loader/LevelLoader.h>
#include <utilities/Log.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

#include <atomic>

std::atomic_bool shutdownProgram{false};

using namespace preagonal;

namespace
{
	const auto Root = std::filesystem::temp_directory_path() / "seihai-level-load";

	// One server for the binary; the file watcher thread makes tearing it down racy.
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
			BabyDI_RELEASE(Server);
			auto* s = new Server("level-load");
			BabyDI_PROVIDE(Server, s);
			return s;
		}();
		return server;
	}

	// 13-bit codes, least significant bit first, one 64x64 board.
	std::string packBoard(const std::vector<uint16_t>& codes)
	{
		std::string out;
		uint32_t buffer = 0;
		uint32_t bits = 0;
		for (const auto code : codes)
		{
			buffer |= static_cast<uint32_t>(code & 0x1FFF) << bits;
			bits += 13;
			while (bits >= 8)
			{
				out.push_back(static_cast<char>(buffer & 0xFF));
				buffer >>= 8;
				bits -= 8;
			}
		}
		if (bits > 0)
			out.push_back(static_cast<char>(buffer & 0xFF));
		return out;
	}

	std::filesystem::path writeLevel(const std::string& name, const std::string& bytes)
	{
		std::filesystem::create_directories(Root);
		const auto path = Root / name;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << bytes;
		out.close();
		heldServer()->getFileSystem().bindSingleFile(path);
		return path;
	}

	std::string tail(const std::string& links)
	{
		// Links, end of baddies, NPCs, chests; signs run to the end of the file.
		return links + "#\n" + std::string("\xff\xff\xff", 3) + "\n" + "#\n" + "#\n";
	}
} // namespace

TEST_CASE("readPackedIntegral<1> reads one byte", "[file][level]")
{
	std::filesystem::create_directories(Root);
	const auto path = Root / "packed.bin";
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << "\x22\x21\x20\x20\x20\x20\x20";
	}
	fs::File file{path};
	REQUIRE(file.opened());
	CHECK(file.readPackedIntegral<1>() == 2);
	CHECK(file.getStreamPosition() == 1);
	CHECK(file.readPackedIntegral<1>() == 1);
	CHECK(file.getStreamPosition() == 2);
	CHECK(file.readPackedIntegral<5>() == 0);
	CHECK(file.getStreamPosition() == 7);
}

TEST_CASE("a two-layer GR-V1.05 level loads both boards in place", "[level]")
{
	std::vector<uint16_t> base(4096, 5);
	base[4095] = 9;
	std::vector<uint16_t> layer(4096, 4095);
	layer[0] = 7;
	layer[4095] = 8;

	const auto path = writeLevel("twolayers.graal", "GR-V1.05" + std::string(1, static_cast<char>(2 + 32)) + packBoard(base) + packBoard(layer) + tail(""));

	const auto data = LevelLoader::loadStaticData(path);
	REQUIRE(data != nullptr);

	const auto board = data->tiles.getLayer(0);
	REQUIRE(board.has_value());
	CHECK(board.value()->at(0) == 5);
	CHECK(board.value()->at(4095) == 9);

	// 4095 in the file is an empty cell on an extra layer; clients draw 0xFFFF as empty.
	const auto extra = data->tiles.getLayer(1);
	REQUIRE(extra.has_value());
	CHECK(extra.value()->at(0) == 7);
	CHECK(extra.value()->at(1) == std::numeric_limits<uint16_t>::max());
	CHECK(extra.value()->at(4094) == std::numeric_limits<uint16_t>::max());
	CHECK(extra.value()->at(4095) == 8);
}

TEST_CASE("a link to a name the OS cannot check is skipped, not fatal", "[level]")
{
	const std::vector<uint16_t> base(4096, 5);
	const std::string longName(5000, 'a');
	const auto path = writeLevel("longlink.graal", "GR-V1.05" + std::string(1, static_cast<char>(1 + 32)) + packBoard(base) + tail(longName + ".nw 0 0 1 1 30 30\n"));

	const auto data = LevelLoader::loadStaticData(path);
	REQUIRE(data != nullptr);
	CHECK(data->links.empty());
	CHECK_FALSE(heldServer()->getFileSystem().has(fs::FileCategory::LEVEL, longName));
}

TEST_CASE("short NPC, chest and sign lines are skipped", "[level]")
{
	const std::vector<uint16_t> base(4096, 5);
	// After the links and baddies: a one-byte NPC line, then a two-byte chest line,
	// then a one-byte sign line.
	const std::string bytes = "GR-V1.05" + std::string(1, static_cast<char>(1 + 32)) + packBoard(base)
		+ "#\n" + std::string("\xff\xff\xff", 3) + "\n"
		+ "x\n#\n"
		+ "ab\n#\n"
		+ "s\n";
	const auto path = writeLevel("shortlines.graal", bytes);

	const auto data = LevelLoader::loadStaticData(path);
	REQUIRE(data != nullptr);
	CHECK(data->npcs.empty());
	CHECK(data->chests.empty());
	CHECK(data->signs.empty());
}
