// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: regression tests for account flags read from an account file, which
// an RC's Edit Attributes dialog shows and sends back on save.
#include "catch2/catch_all.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <loader/AccountFlag.h>
#include <scripting/ScriptContainers.h>

using namespace preagonal;

namespace
{
	/// Loads FLAG values into a store as an account does, then lists them the way
	/// the RC props packet and the account save do.
	std::vector<std::string> roundTrip(const std::vector<std::string>& values)
	{
		GameVariableStore store;
		store.defaultLifetime = variables::Lifetime::PERMANENT;
		for (const auto& value : values)
		{
			if (auto variable = deserializeAccountFlag(value); variable.has_value())
				store.add(std::move(variable.value()));
		}

		std::vector<std::string> result;
		for (const auto& [name, variable] : store.store | variables::only_flags | variables::serializable)
		{
			if (auto serialized = store.serializeModern(name); serialized.has_value())
				result.push_back(serialized.value());
		}
		std::ranges::sort(result);
		return result;
	}
} // namespace

SCENARIO("account FLAG lines survive a load")
{
	GIVEN("the flag a real 2.22 RC saved in the lab capture (2026-11-04b)")
	{
		// test1.txt held "FLAG labflag=1"; the loader hands over "labflag=1".
		THEN("it is loaded and listed again as labflag=1")
		{
			REQUIRE(roundTrip({"labflag=1"}) == std::vector<std::string>{"labflag=1"});
		}
	}

	GIVEN("a flag with no value and one with a value")
	{
		THEN("both come back as they were written")
		{
			REQUIRE(roundTrip({"gotsword", "quest=3"}) == std::vector<std::string>{"gotsword", "quest=3"});
		}
	}

	GIVEN("the value alone, as upstream passed it")
	{
		THEN("GameVariable::deserialize rejects it, which is why every flag was dropped")
		{
			REQUIRE_FALSE(GameVariable::deserialize("labflag=1").has_value());
		}
	}
}
