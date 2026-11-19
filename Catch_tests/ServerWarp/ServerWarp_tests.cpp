// Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// The server-warp text a client gets (#114): a name starting with "3 " made 5.12 treat the server as 3D.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <atomic>
#include <string>

#include <misc/ServerWarpText.h>

std::atomic_bool shutdownProgram{false};

using preagonal::serverwarp::stripThreeDPrefix;

TEST_CASE("a lobby name starting with \"3 \" loses the prefix", "[serverwarp]")
{
	bool changed = false;
	CHECK(stripThreeDPrefix("0,3 Test,127.0.0.1,14802", changed) == "0,Test,127.0.0.1,14802");
	CHECK(changed);
	CHECK(stripThreeDPrefix("0,\"3 Lab, west\",127.0.0.1,14802", changed) == "0,\"Lab, west\",127.0.0.1,14802");
	CHECK(changed);
	CHECK(stripThreeDPrefix("0,3 3  Twice,127.0.0.1,1", changed) == "0,Twice,127.0.0.1,1");
	CHECK(changed);
}

TEST_CASE("other names are sent as they are", "[serverwarp]")
{
	bool changed = true;
	CHECK(stripThreeDPrefix("0,Lab Server,127.0.0.1,14802", changed) == "0,Lab Server,127.0.0.1,14802");
	CHECK_FALSE(changed);
	CHECK(stripThreeDPrefix("0,3D World,127.0.0.1,14802", changed) == "0,3D World,127.0.0.1,14802");
	CHECK_FALSE(changed);
	CHECK(stripThreeDPrefix("0,3 ,127.0.0.1,1", changed) == "0,3 ,127.0.0.1,1");
	CHECK_FALSE(changed);
	CHECK(stripThreeDPrefix("", changed).empty());
	CHECK(stripThreeDPrefix("no commas", changed) == "no commas");
	CHECK_FALSE(changed);
}
