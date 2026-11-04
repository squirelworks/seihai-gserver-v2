// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: regression tests for the folder rights text sent to an RC.
#include "catch2/catch_all.hpp"

#include <string>
#include <vector>

#include <player/RCFolderRights.h>
#include <utilities/StringUtils.h>

using namespace preagonal;

SCENARIO("rcFolderRightsText quotes folder rights the way the RC sends them")
{
	GIVEN("the folder rights a real 2.22 RC set in the lab capture (2026-11-04)")
	{
		// RC_PLAYERRIGHTSSET carried "rw levels/*" (quoted because of the space).
		const std::vector<std::string> folders{ "rw levels/*" };

		THEN("the reply carries the same quoted text, not the bare line")
		{
			REQUIRE(rcFolderRightsText(folders) == "\"rw levels/*\"");
		}
	}

	GIVEN("several folder lines")
	{
		const std::vector<std::string> folders{ "rw levels/*", "r maps/*", "rw ganis/*.gani" };

		THEN("each line is quoted and the lines are joined with commas")
		{
			REQUIRE(rcFolderRightsText(folders) == "\"rw levels/*\",\"r maps/*\",\"rw ganis/*.gani\"");
		}

		THEN("the server's own CSV reader gets the same lines back")
		{
			REQUIRE(string::fromCSV(rcFolderRightsText(folders)) == folders);
		}
	}

	GIVEN("no folder rights")
	{
		THEN("the text is empty")
		{
			REQUIRE(rcFolderRightsText({}).empty());
		}
	}
}
