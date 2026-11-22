// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include <catch2/catch_all.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <filesystem/FileSystem.h>

namespace
{
	struct BindDirectoryFixture
	{
		BindDirectoryFixture()
			: previous(std::filesystem::current_path()),
			  root(std::filesystem::absolute(std::filesystem::temp_directory_path() /
				("seihai-bind-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))))
		{
			REQUIRE(std::filesystem::create_directory(root));
			for (const auto& directory : {"tree/leaf", "extra/leaf", "leaf"})
				std::filesystem::create_directories(root / directory);
			for (const auto& file : {"tree/leaf/wanted.txt", "tree/sibling.txt", "extra/leaf/second.txt", "extra/sibling2.txt", "leaf/decoy.txt"})
			{
				std::ofstream out(root / file);
				out << "fixture";
				REQUIRE(out.good());
			}
			std::filesystem::current_path(root);
		}

		~BindDirectoryFixture()
		{
			// The test's local FileSystem has already stopped its watchers.
			std::error_code error;
			std::filesystem::current_path(previous, error);
			if (!error)
				std::filesystem::remove_all(root, error);
		}

		std::filesystem::path previous;
		std::filesystem::path root;
	};
}

TEST_CASE_METHOD(BindDirectoryFixture, "Directory bind preserves a path instead of binding its components", "[filesystem][bind]")
{
	const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
	constexpr std::array<std::string_view, 10> names{
		"const path", "mutable path", "temporary path", "path range",
		"string", "string range", "multiple strings", "wide string", "string literal", "sharefolder split range"
	};
	CAPTURE(names[mode]);
	// All components are pre-owned under this fixture's CWD. Never pass an
	// absolute path to the faulty overload: that would enumerate a root.
	std::filesystem::path directory{"tree/leaf"};
	REQUIRE(directory.is_relative());
	REQUIRE_FALSE(directory.has_root_path());
	preagonal::fs::FileSystem files;
	switch (mode)
	{
		case 0:
		{
			const std::filesystem::path constant{directory};
			files.bind(constant);
			break;
		}
		case 1: files.bind(directory); break;
		case 2: files.bind(std::filesystem::path{"tree/leaf"}); break;
		case 3: files.bind(std::vector<std::filesystem::path>{"tree/leaf", "extra/leaf"}); break;
		case 4: files.bind(std::string{"tree/leaf"}); break;
		case 5: files.bind(std::vector<std::string>{"tree/leaf", "extra/leaf"}); break;
		case 6: files.bind(std::string{"tree/leaf"}, std::string{"extra/leaf"}); break;
		case 7: files.bind(std::wstring{L"tree/leaf"}); break;
		case 8: files.bind("tree/leaf"); break;
		case 9:
		{
			const std::string list{"tree/leaf,extra/leaf"};
			auto folders = preagonal::string::split(list, std::string_view{","});
			files.bind(folders);
			break;
		}
	}
	files.waitUntilFilesSearched();
	CHECK(files.has("wanted.txt"));
	CHECK(files.has("second.txt") == (mode == 3 || mode == 5 || mode == 6 || mode == 9));
	CHECK_FALSE(files.has("sibling.txt"));
	CHECK_FALSE(files.has("sibling2.txt"));
	CHECK_FALSE(files.has("decoy.txt"));
}
