// Modified by the Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: the tokenized text the server sends to an RC or NPC-Control for a
// multi-line text box (server options, folder config, NPC attributes, NPC and
// class scripts, NPC flags), kept apart from PlayerRC/PlayerNC so it can be tested.
#ifndef RCTEXT_H
#define RCTEXT_H

#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace preagonal
{
	/// @brief Tokenizes one line the way Graal's tokenizer (and the RC) does.
	/// A line is quoted when it starts with a quote, or holds a byte outside 33..126
	/// (a space included), a comma or a slash; inside quotes `\` and `"` are doubled.
	/// An empty line stays empty.
	inline void rcTokenizeLine(std::string& out, std::string_view line)
	{
		std::string clean;
		clean.reserve(line.size());
		for (const char c : line)
			if (c != '\r') clean += c;

		bool complex = !clean.empty() && clean.front() == '"';
		for (const char c : clean)
		{
			const auto u = static_cast<unsigned char>(c);
			if (u < 33 || u > 126 || c == ',' || c == '/')
			{
				complex = true;
				break;
			}
		}

		if (!complex)
		{
			out += clean;
			return;
		}

		out += '"';
		for (const char c : clean)
		{
			if (c == '\\') out += "\\\\";
			else if (c == '"') out += "\"\"";
			else out += c;
		}
		out += '"';
	}

	/// @brief Tokenizes a list of lines for an RC text box: each line as rcTokenizeLine,
	/// joined with commas.
	///
	/// string::toCSV quotes only on '"', ',' and '\', so a line such as "name = My Server"
	/// or "head    heads/*" went out bare. The RC's tokenizer also splits an unquoted item
	/// at spaces, so it showed every word on a line of its own, and its next set wrote
	/// that back: in the lab (2026-11-13, 2004 RC) serveroptions.txt went from 572 to
	/// 2476 lines and foldersconfig.txt became one word per line.
	template <std::ranges::input_range R>
	std::string rcTokenizeLines(R&& lines)
	{
		std::string out;
		bool first = true;
		for (const auto& line : lines)
		{
			if (!first) out += ',';
			first = false;
			rcTokenizeLine(out, std::string_view{line});
		}
		return out;
	}

	/// @brief Splits text at line feeds and tokenizes it with rcTokenizeLines. A final
	/// line feed does not add an empty line.
	inline std::string rcTokenizeText(std::string_view text)
	{
		std::vector<std::string_view> lines;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find('\n', start);
			if (end == std::string_view::npos)
			{
				if (start < text.size())
					lines.push_back(text.substr(start));
				break;
			}
			lines.push_back(text.substr(start, end - start));
			start = end + 1;
		}
		return rcTokenizeLines(lines);
	}
} // namespace preagonal

#endif // RCTEXT_H
