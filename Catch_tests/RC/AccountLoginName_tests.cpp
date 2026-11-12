// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: account names a client may log in with (GwenTea2 lab 2026-11-12, a 1.39 logged in with commas).
#include "catch2/catch_all.hpp"

#include <string>

#include <Account.h>

using namespace preagonal;

SCENARIO("a login's account name is checked")
{
	GIVEN("names a listserver hands out")
	{
		THEN("they are allowed")
		{
			REQUIRE(Account::isLoginNameAllowed("seihaitest"));
			REQUIRE(Account::isLoginNameAllowed("guest"));
			REQUIRE(Account::isLoginNameAllowed("Lab User 2"));
			REQUIRE(Account::isLoginNameAllowed("x222"));
		}
	}

	GIVEN("the name a 1.39 sent when its account box could not be cleared")
	{
		THEN("it is refused")
		{
			REQUIRE_FALSE(Account::isLoginNameAllowed("seihaitest,lab420,fresh2x139"));
		}
	}

	GIVEN("names that would leave the accounts folder or break a file name")
	{
		THEN("they are refused")
		{
			REQUIRE_FALSE(Account::isLoginNameAllowed(""));
			REQUIRE_FALSE(Account::isLoginNameAllowed("../config/serveroptions"));
			REQUIRE_FALSE(Account::isLoginNameAllowed("a/b"));
			REQUIRE_FALSE(Account::isLoginNameAllowed("a\\b"));
			REQUIRE_FALSE(Account::isLoginNameAllowed(".."));
			REQUIRE_FALSE(Account::isLoginNameAllowed(std::string("a\nb")));
			REQUIRE_FALSE(Account::isLoginNameAllowed(std::string("a\0b", 3)));
		}
	}
}
