// Modified by the Seihai project, 2026-11-15: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// The bytes one file chunk is queued as, for clients before 2.1 and from 2.1.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <string>
#include <string_view>

#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>
#include <player/FileSendLayout.h>

using namespace preagonal;

std::atomic_bool shutdownProgram{false};

namespace
{
	struct FileBody
	{
		bool hasModTime = false;
		unsigned int modTime = 0;
		std::string name;
		std::string data;
	};

	FileBody readOldBody(CString body)
	{
		body.setRead(0);
		REQUIRE(static_cast<unsigned char>(body.readGUChar()) == PLO_FILE);
		FileBody out;
		const int nameLength = body.readGUChar();
		out.name = body.readChars(nameLength).toString();
		out.data = body.readChars(body.bytesLeft()).toString();
		REQUIRE(body.bytesLeft() == 0);
		return out;
	}

	FileBody readNewBody(CString body)
	{
		body.setRead(0);
		REQUIRE(static_cast<unsigned char>(body.readGUChar()) == PLO_FILE);
		FileBody out;
		out.hasModTime = true;
		out.modTime = body.readGUInt5();
		const int nameLength = body.readGUChar();
		out.name = body.readChars(nameLength).toString();
		out.data = body.readChars(body.bytesLeft()).toString();
		REQUIRE(body.bytesLeft() == 0);
		return out;
	}

	// The envelope a pre-2.1 client is queued: RAWDATA, the FILE body length,
	// a line feed, then that body and nothing after it.
	FileBody readEnvelope(const CString& packet)
	{
		CString env = packet;
		env.setRead(0);
		REQUIRE(static_cast<unsigned char>(env.readGUChar()) == PLO_RAWDATA);
		const int count = env.readGInt();
		REQUIRE(env.readChar() == '\n');
		REQUIRE(count == env.bytesLeft());
		CString body = env.readChars(count);
		REQUIRE(env.bytesLeft() == 0);
		REQUIRE(body.length() == count);
		return readOldBody(body);
	}
}

TEST_CASE("A client before 2.1 gets the file bytes with no counted line feed", "[filesend]")
{
	const std::string data = "abc";
	const auto chunk = queueFileChunk(CLVER_2_052, true, 1700000000, "sword1.png", data);
	REQUIRE_FALSE(chunk.raw);
	const auto body = readEnvelope(chunk.packet);
	REQUIRE(body.name == "sword1.png");
	REQUIRE(body.data == data);
	REQUIRE(body.data.find('\n') == std::string::npos);
}

TEST_CASE("2.00 is still the pre-2.1 file body", "[filesend]")
{
	const auto chunk = queueFileChunk(CLVER_2_0, true, 42, "head.gif", "xyz");
	REQUIRE_FALSE(chunk.raw);
	const auto body = readEnvelope(chunk.packet);
	REQUIRE(body.name == "head.gif");
	REQUIRE(body.data == "xyz");
}

TEST_CASE("A line feed that is part of the file stays, and no second one is counted", "[filesend]")
{
	const std::string data = "a\n";
	const auto chunk = queueFileChunk(CLVER_1_39, true, 7, "note.txt", data);
	REQUIRE_FALSE(chunk.raw);
	const auto body = readEnvelope(chunk.packet);
	REQUIRE(body.data == data);
}

TEST_CASE("A zero-byte file still has a FILE body", "[filesend]")
{
	const auto chunk = queueFileChunk(CLVER_2_052, true, 9, "empty.txt", {});
	REQUIRE_FALSE(chunk.raw);
	const auto body = readEnvelope(chunk.packet);
	REQUIRE(body.name == "empty.txt");
	REQUIRE(body.data.empty());
}

TEST_CASE("From 2.1 the chunk is raw and carries the modtime", "[filesend]")
{
	const auto at21 = queueFileChunk(CLVER_2_1, true, 12345, "body.png", "PNG");
	REQUIRE(at21.raw);
	const auto body21 = readNewBody(at21.packet);
	REQUIRE(body21.modTime == 12345);
	REQUIRE(body21.name == "body.png");
	REQUIRE(body21.data == "PNG");

	const auto at217 = queueFileChunk(CLVER_2_17, true, 99, "pics1.png", "Q");
	REQUIRE(at217.raw);
	const auto body217 = readNewBody(at217.packet);
	REQUIRE(body217.modTime == 99);
	REQUIRE(body217.name == "pics1.png");
	REQUIRE(body217.data == "Q");
}

TEST_CASE("A non-client keeps the modtime on a raw chunk", "[filesend]")
{
	const auto chunk = queueFileChunk(CLVER_1_39, false, 55, "level.nw", "board");
	REQUIRE(chunk.raw);
	const auto body = readNewBody(chunk.packet);
	REQUIRE(body.modTime == 55);
	REQUIRE(body.name == "level.nw");
	REQUIRE(body.data == "board");
}
