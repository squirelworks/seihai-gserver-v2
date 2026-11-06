// Modified by the Seihai project, 2026-11-06: see CHANGES-SEIHAI.md.
// New file: tests for resource packages (.gpak, .gpake) and their key packet.
#include "catch2/catch_all.hpp"

#include <string>

#include <misc/GpakPackages.h>

using namespace preagonal;

namespace
{
	// The key of the lab's sample.gpake, the package the real 6.03r7 and 4.20r8
	// clients decrypted and drew (2026-11-06).
	constexpr GpakKey sampleKey{ 0x13, 0x34, 0x57, 0x79, 0x9B, 0xBC, 0xDF, 0xF1 };
}

SCENARIO("gpakKeyPacket matches the PLO_UNKNOWN104 the real clients took")
{
	GIVEN("sample.gpake and its key")
	{
		THEN("the bytes are the lab's packet: 104, name, a=1, GINT5 lo, GINT5 hi")
		{
			const std::string want{ "\x88\x2c" "sample.gpake" "\x21" "\x27\x6a\x7c\x88\x33" "\x2f\x2e\x9e\x99\x3b" };
			REQUIRE(gpakKeyPacket("Sample.gpake", sampleKey).toString() == want);
		}

		THEN("a folder in the name is left out")
		{
			REQUIRE(gpakKeyPacket("gpaks/sample.gpake", sampleKey).toString() == gpakKeyPacket("sample.gpake", sampleKey).toString());
		}
	}
}

SCENARIO("gpakKeyFromText reads config/gpakkeys.txt")
{
	const std::string text =
		"# Keys for the encrypted packages\n"
		"#   name.gpake KEY\n"
		"other.gpake 0000000000000000\r\n"
		"  Sample.GPAKE   133457799bbcdff1\n"
		"broken.gpake 12345\n";

	THEN("a package's key is found by name in any case")
	{
		REQUIRE(gpakKeyFromText(text, "sample.gpake") == sampleKey);
		REQUIRE(gpakKeyFromText(text, "other.gpake") == GpakKey{});
	}

	THEN("a missing package, a comment or a bad key gives no key")
	{
		REQUIRE_FALSE(gpakKeyFromText(text, "missing.gpake").has_value());
		REQUIRE_FALSE(gpakKeyFromText(text, "name.gpake").has_value());
		REQUIRE_FALSE(gpakKeyFromText(text, "broken.gpake").has_value());
	}
}

SCENARIO("only the clients that load a package get one")
{
	THEN("file names are sorted by extension")
	{
		REQUIRE(gpakKind("art.gpake") == GpakKind::Encrypted);
		REQUIRE(gpakKind("gpaks/Art.GPAK") == GpakKind::Plain);
		REQUIRE(gpakKind("art.png") == GpakKind::None);
		REQUIRE(gpakKind("gpak") == GpakKind::None);
	}

	THEN("encrypted packages go to 4.20r8 and later, plain ones to 5.x and later")
	{
		REQUIRE(clientLoadsGpak(CLVER_6_037, GpakKind::Encrypted));
		REQUIRE(clientLoadsGpak(CLVER_6_037, GpakKind::Plain));
		REQUIRE(clientLoadsGpak(CLVER_4_208, GpakKind::Encrypted));
		REQUIRE_FALSE(clientLoadsGpak(CLVER_4_208, GpakKind::Plain));
		REQUIRE_FALSE(clientLoadsGpak(CLVER_4_042, GpakKind::Encrypted));
		REQUIRE_FALSE(clientLoadsGpak(CLVER_2_22, GpakKind::Encrypted));
		REQUIRE_FALSE(clientLoadsGpak(CLVER_2_22, GpakKind::Plain));
		REQUIRE_FALSE(clientLoadsGpak(CLVER_IPHONE_1_1, GpakKind::Encrypted));
	}

	THEN("every client still gets files that are not packages")
	{
		REQUIRE(clientLoadsGpak(CLVER_1_411, GpakKind::None));
	}
}
