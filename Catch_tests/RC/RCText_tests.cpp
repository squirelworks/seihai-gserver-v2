// Modified by the Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: regression tests for the tokenized text sent to an RC or NPC-Control.
#include "catch2/catch_all.hpp"

#include <string>
#include <vector>

#include <CString.h>

#include <player/RCText.h>

using namespace preagonal;

namespace
{
	std::string viaGs2lib(const std::string& text)
	{
		return CString(text).gtokenize().toString();
	}

	std::vector<std::string> rcLines(const std::string& tokenized)
	{
		// The RC (and gs2lib) read the text back with guntokenize: one line per item.
		std::vector<std::string> out;
		const std::string text = CString(tokenized).guntokenize().toString();
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find('\n', start);
			if (end == std::string::npos)
			{
				if (start < text.size()) out.push_back(text.substr(start));
				break;
			}
			out.push_back(text.substr(start, end - start));
			start = end + 1;
		}
		return out;
	}
} // namespace

SCENARIO("rcTokenizeLines quotes each line the way the RC tokenizes")
{
	GIVEN("folder config lines from the lab world (2026-11-13, 2004 RC)")
	{
		const std::vector<std::string> lines{ "head    heads/*", "body    bodies/*", "file    levels/*.nw" };

		THEN("a line with a space or a slash is quoted, so the RC keeps it as one line")
		{
			REQUIRE(rcTokenizeLines(lines) == "\"head    heads/*\",\"body    bodies/*\",\"file    levels/*.nw\"");
		}

		THEN("the RC's reader gets the same lines back")
		{
			REQUIRE(rcLines(rcTokenizeLines(lines)) == lines);
		}
	}

	GIVEN("server options with spaces, a comment and an empty line")
	{
		const std::vector<std::string> lines{ "name = My Server", "", "# comment, with a comma", "maxplayers=128", "staff=labrc" };

		THEN("it matches gs2lib's gtokenize of the same text")
		{
			REQUIRE(rcTokenizeLines(lines) == viaGs2lib("name = My Server\n\n# comment, with a comma\nmaxplayers=128\nstaff=labrc\n"));
		}

		THEN("a plain line stays bare and the empty line stays empty")
		{
			REQUIRE(rcTokenizeLines(lines) == "\"name = My Server\",,\"# comment, with a comma\",maxplayers=128,staff=labrc");
		}

		THEN("the RC's reader gets the same lines back")
		{
			REQUIRE(rcLines(rcTokenizeLines(lines)) == lines);
		}
	}

	GIVEN("lines with quotes, a leading quote, a backslash and a byte above 126")
	{
		const std::vector<std::string> lines{ "echo(\"lab, class\");", "\"quoted", "a\\b", "caf\xe9" };

		THEN("it matches gs2lib's gtokenize of the same text")
		{
			REQUIRE(rcTokenizeLines(lines) == viaGs2lib("echo(\"lab, class\");\n\"quoted\na\\b\ncaf\xe9\n"));
		}
	}
}

SCENARIO("rcTokenizeText splits a script at line feeds")
{
	GIVEN("an NPC script as the 2004 RC set it in the lab")
	{
		const std::string script = "// lab npc script\r\nif (created) {\r\n  this.lab = 1;\r\n}\r\n";

		THEN("each line is one item, carriage returns dropped, the final line feed adds nothing")
		{
			REQUIRE(rcTokenizeText(script) == "\"// lab npc script\",\"if (created) {\",\"  this.lab = 1;\",}");
		}

		THEN("it matches gs2lib's gtokenize")
		{
			REQUIRE(rcTokenizeText(script) == viaGs2lib(script));
		}
	}

	GIVEN("empty text")
	{
		THEN("the result is empty")
		{
			REQUIRE(rcTokenizeText("").empty());
		}
	}
}
