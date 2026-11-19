// Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).

#ifndef SEIHAI_SERVERWARPTEXT_H
#define SEIHAI_SERVERWARPTEXT_H

#include <string>
#include <string_view>

namespace preagonal::serverwarp
{
	/// @brief Seihai (#114): the text of a PLO_SERVERWARP (178), `0,name,ip,port` with the name quoted when it holds
	/// a comma or a quote, with any leading "3 " taken off the name. A 5.12 client reads a server name that starts
	/// with "3 " as a 3D server and drops the connection about 15 seconds later; a display name must not change how
	/// the client treats the server. `changed` says whether the name was trimmed, so the caller can log it.
	/// Text not in that shape comes back as it is.
	inline std::string stripThreeDPrefix(const std::string_view text, bool& changed)
	{
		changed = false;
		const auto comma = text.find(',');
		if (comma == std::string_view::npos)
			return std::string{text};

		auto nameStart = comma + 1;
		const bool quoted = nameStart < text.size() && text[nameStart] == '"';
		if (quoted)
			++nameStart;

		auto nameEnd = nameStart;
		while (nameEnd + 1 < text.size() && text[nameEnd] == '3' && text[nameEnd + 1] == ' ')
		{
			nameEnd += 2;
			while (nameEnd < text.size() && text[nameEnd] == ' ')
				++nameEnd;
		}
		if (nameEnd == nameStart)
			return std::string{text};

		// Keep a name: "3 " alone stays as it is.
		const auto rest = text.substr(nameEnd);
		if (rest.empty() || rest.front() == ',' || (quoted && rest.front() == '"'))
			return std::string{text};

		changed = true;
		std::string out{text.substr(0, nameStart)};
		out += rest;
		return out;
	}
} // namespace preagonal::serverwarp

#endif
