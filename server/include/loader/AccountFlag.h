// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: reads one FLAG line of an account file, kept apart from the loader
// so it can be tested on its own.
#ifndef ACCOUNTFLAG_H
#define ACCOUNTFLAG_H

#include <format>
#include <optional>
#include <string_view>

#include <scripting/ScriptContainers.h>

namespace preagonal
{
	/// @brief Turns the value of an account file's FLAG line into a variable.
	/// @param value What follows "FLAG " on the line: "name" or "name=value".
	/// @return The flag, or nothing if the line holds no name.
	inline std::optional<GameVariable> deserializeAccountFlag(std::string_view value)
	{
		// The account loader splits "FLAG name=value" into the section and the value,
		// but GameVariable::deserialize wants the whole line, "FLAG" included.
		// Upstream passed only the value, so every flag was dropped on load: the RC
		// showed none, and the next save of the account (RC or logout) wiped them all.
		return GameVariable::deserialize(std::format("FLAG {}", value));
	}
} // namespace preagonal

#endif // ACCOUNTFLAG_H
