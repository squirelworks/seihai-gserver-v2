// Seihai project, 2026-11-26: see CHANGES-SEIHAI.md.
#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <scripting/GS2ScriptManager.h>

using namespace preagonal;

namespace
{
	// Every function_<id>_<n> name in the bytecode, sorted.
	std::vector<std::string> lambdaNames(const CompilerResponse& response)
	{
		std::string_view text(reinterpret_cast<const char*>(response.bytecode.buffer()), response.bytecode.length());
		std::vector<std::string> names;
		for (auto at = text.find("function_"); at != std::string_view::npos; at = text.find("function_", at + 1))
		{
			auto end = at;
			while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
				++end;
			std::string name{ text.substr(at, end - at) };
			if (std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	uint32_t lambdaId(const std::string& name)
	{
		auto first = name.find('_') + 1;
		return static_cast<uint32_t>(std::stoul(name.substr(first, name.find('_', first) - first)));
	}
} // namespace

TEST_CASE("Each GS2 compile numbers its lambdas from the server's compile count", "[GS2ScriptManager]")
{
	const std::string src =
		"function onCreated() {\n"
		"  this.f = function() { return 1; };\n"
		"  this.g = function() { return 2; };\n"
		"}\n";

	GS2ScriptManager manager;
	auto first = lambdaNames(manager.compileScript(src).get());
	auto second = lambdaNames(manager.compileScript(src).get());

	REQUIRE(first.size() == 2);
	REQUIRE(second.size() == 2);

	// One number for every lambda of a compile, then _1, _2 in source order.
	CHECK(lambdaId(first[0]) == lambdaId(first[1]));
	CHECK(first[0].ends_with("_1"));
	CHECK(first[1].ends_with("_2"));

	// The next compile takes a later number, so the two never share a name.
	CHECK(lambdaId(second[0]) > lambdaId(first[0]));
}
