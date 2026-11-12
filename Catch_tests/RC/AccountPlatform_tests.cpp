// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: the account file's PLATFORM spelling (GwenTea2 lab 2026-11-12, "win" became "wind").
#include "catch2/catch_all.hpp"

#include <Account.h>

using namespace preagonal;

SCENARIO("the account file writes one spelling for Windows")
{
	GIVEN("the OSTYPE a real 2.22 sends at login")
	{
		THEN("the file writes win, as a 4.x login token says")
		{
			REQUIRE(Account::filePlatformName("wind") == "win");
		}
	}

	GIVEN("the platforms other clients report")
	{
		THEN("they are written as sent")
		{
			REQUIRE(Account::filePlatformName("win") == "win");
			REQUIRE(Account::filePlatformName("mac") == "mac");
			REQUIRE(Account::filePlatformName("linux") == "linux");
			REQUIRE(Account::filePlatformName("") == "");
		}
	}

	GIVEN("a new account")
	{
		THEN("its default platform is already the file spelling")
		{
			REQUIRE(Account::filePlatformName(Account{}.platform) == Account{}.platform);
		}
	}
}
