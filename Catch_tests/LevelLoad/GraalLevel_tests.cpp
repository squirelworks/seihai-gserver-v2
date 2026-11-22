// Added by the Seihai project, 2026-11-19, 2026-11-20, 2026-11-22: see CHANGES-SEIHAI.md.
// GR-V1.05 levels: the one-byte layer count, extra-layer empty tiles, and malformed
// files that used to end the process.
#include "catch2/catch_all.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <system_error>
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

TEST_CASE("a sign keeps a Latin-1 letter as #K(code) with the byte value", "[level][sign]")
{
	heldServer();
	// 0xE9 is e acute. `char` is signed, so it used to be written as #K(-23).
	const LevelSign sign{LocalWholeTilePosition{1, 2}, "caf\xe9 \xc4\n"};

	// Sign codes are GCHARs (code + 32): `#K(` is 86 10 69, `)` is 70, and the
	// digits 0 to 9 are 52 to 61 (`-` would be 64).
	const auto code = [](std::initializer_list<int> codes)
	{
		std::string out;
		for (const int c : codes)
			out.push_back(static_cast<char>(c + 32));
		return out;
	};
	const auto k233 = code({86, 10, 69, 54, 55, 55, 70});
	const auto k196 = code({86, 10, 69, 53, 61, 58, 70});
	CHECK(sign.encodedText.find(k233) != std::string::npos);
	CHECK(sign.encodedText.find(k196) != std::string::npos);
	CHECK(sign.encodedText.find(static_cast<char>(64 + 32)) == std::string::npos);
}

TEST_CASE("saveLevel reports obstruction and round-trips a valid level", "[level][save]")
{
	auto* server = heldServer();
	auto& fileSystem = server->getFileSystem();
	fileSystem.reset();

	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	std::filesystem::path testRoot;
	for (unsigned int attempt = 0; attempt < 64 && testRoot.empty(); ++attempt)
	{
		const auto candidate = std::filesystem::temp_directory_path() / ("seihai-level-save-" + std::to_string(nonce) + "-" + std::to_string(attempt));
		std::error_code error;
		if (std::filesystem::create_directory(candidate, error))
			testRoot = candidate;
		else
			REQUIRE_FALSE((error && error != std::errc::file_exists));
	}
	REQUIRE_FALSE(testRoot.empty());

	struct ScopedTestRoot
	{
		fs::FileSystem& fileSystem;
		std::filesystem::path root;

		~ScopedTestRoot() noexcept
		{
			try
			{
				fileSystem.reset();
				std::error_code error;
				std::filesystem::remove_all(root, error);
			}
			catch (...)
			{
			}
		}
	};
	ScopedTestRoot cleanup{fileSystem, testRoot};

	const std::string prefix = testRoot.filename().string();
	const std::string sourceName = prefix + ".nw";
	const auto sourcePath = testRoot / sourceName;
	std::vector<uint16_t> base(4096, 5);
	base[4095] = 9;
	{
		std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
		REQUIRE(source.is_open());
		source << "GR-V1.05" << static_cast<char>(1 + 32) << packBoard(base) << tail("");
		source.close();
		REQUIRE(source.good());
	}

	fileSystem.bind(testRoot);
	REQUIRE(fileSystem.findi(fs::FileCategory::LEVEL, sourceName) == sourcePath);
	auto managedDirectories = fileSystem.getManagedDirectories(fs::FileCategory::LEVEL);
	const auto firstManagedDirectory = managedDirectories.begin();
	REQUIRE(firstManagedDirectory != managedDirectories.end());
	REQUIRE(*firstManagedDirectory == testRoot);

	const auto level = LevelLoader::loadLevel(sourceName);
	REQUIRE(level != nullptr);

	// Positive control: write a valid level, bind it, and read its board back.
	const std::string savedName = prefix + "-roundtrip.nw";
	REQUIRE(fileSystem.findi(fs::FileCategory::LEVEL, savedName).empty());
	REQUIRE(level->saveLevel(MapPosition{}, savedName));
	const auto savedPath = testRoot / savedName;
	REQUIRE(std::filesystem::is_regular_file(savedPath));
	fileSystem.bindSingleFile(savedPath);
	const auto savedData = LevelLoader::loadStaticData(savedName);
	REQUIRE(savedData != nullptr);
	const auto savedBoard = savedData->tiles.getLayer(0);
	REQUIRE(savedBoard.has_value());
	REQUIRE(savedBoard.value()->at(0) == 5);
	REQUIRE(savedBoard.value()->at(4095) == 9);

	fileSystem.reset();
	const std::string blockedName = prefix + "-blocked.nw";
	const auto blockedPath = testRoot / blockedName;
	REQUIRE(std::filesystem::create_directory(blockedPath));
	fileSystem.bind(testRoot);
	REQUIRE(fileSystem.findi(fs::FileCategory::LEVEL, blockedName).empty());
	auto obstructionDirectories = fileSystem.getManagedDirectories(fs::FileCategory::LEVEL);
	const auto obstructionParent = obstructionDirectories.begin();
	REQUIRE(obstructionParent != obstructionDirectories.end());
	REQUIRE(*obstructionParent == testRoot);
	REQUIRE(std::filesystem::is_directory(blockedPath));
	CHECK_FALSE(level->saveLevel(MapPosition{}, blockedName));
	REQUIRE(std::filesystem::is_directory(blockedPath));

	// Remove only the empty obstruction this test created, then verify a fresh output path.
	std::error_code removeError;
	const bool removed = std::filesystem::remove(blockedPath, removeError);
	REQUIRE_FALSE(removeError);
	REQUIRE(removed);
	const std::string retryName = prefix + "-retry.nw";
	REQUIRE(level->saveLevel(MapPosition{}, retryName));
	const auto retryPath = testRoot / retryName;
	REQUIRE(std::filesystem::is_regular_file(retryPath));
	fileSystem.bindSingleFile(retryPath);
	const auto retryData = LevelLoader::loadStaticData(retryName);
	REQUIRE(retryData != nullptr);
	const auto retryBoard = retryData->tiles.getLayer(0);
	REQUIRE(retryBoard.has_value());
	REQUIRE(retryBoard.value()->at(0) == 5);
	REQUIRE(retryBoard.value()->at(4095) == 9);
}
