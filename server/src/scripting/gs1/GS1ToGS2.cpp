// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// Seihai: GS1 client scripts translated to GS2 for clients that only run bytecode.
// A C++ port of the GS1 front end of seihai-core's GS2 compiler (scripting/gs2/compile/gs1.rs, seihai-core #6).
// See include/scripting/gs1/GS1ToGS2.h.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ANTLRInputStream.h>
#include <BailErrorStrategy.h>
#include <BaseErrorListener.h>
#include <CommonTokenStream.h>
#include <GS1Lexer.h>
#include <GS1Parser.h>
#include <Exceptions.h>
#include <tree/TerminalNode.h>

#include <compiler/GS2Context.h>

#include <scripting/gs1/GS1ToGS2.h>

using namespace std::literals;

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::gs1
{
///////////////////////////////////////////////////////////////////////////////

namespace
{

using grammar::GS1Lexer;
using grammar::GS1Parser;
using antlr4::tree::ParseTree;
using antlr4::tree::TerminalNode;

// ---- tables (seihai-core scripting/gs2/compile/tables.rs and scripting/engine/vocab.rs) ----

// Names registered by the 3.153 client, lower case, sorted (seihai-core documentation/gs2-shared-commands.md).
constexpr auto GS2ClientCommands = std::to_array<std::string_view>({
	"addcontrol", "addtiledef", "addtiledef2", "aindexof", "attachplayertoobj", "blockagain", "blockagainlocal",
	"callnpc", "callweapon", "canbecarried", "canbepulled", "canbepushed", "cannotbecarried", "cannotbepulled",
	"cannotbepushed", "carryobject", "changeimgcolors", "changeimgmode", "changeimgpart", "changeimgvis",
	"changeimgzoom", "commandtoserver", "contains", "cursoroff", "cursoron", "destroy", "detachplayer",
	"disabledefmovement", "disablemap", "disablepause", "disableselectweapons", "disableweapons", "dontblock",
	"dontblocklocal", "drawaslight", "drawoverplayer", "drawunderplayer", "echo", "enabledefmovement",
	"enablefeatures", "enablemap", "enablepause", "enableselectweapons", "enableweapons", "explodebomb",
	"extractfilebase", "extractfileext", "extractfilename", "extractfilepath", "findfirstfile", "findimg",
	"findnextfile", "findplayer", "findplayerbyid", "findweapon", "freezeplayer", "getascii", "getboxcenter",
	"getdatablocktypes", "getextension", "getimgheight", "getimgwidth", "getkeycode", "getmapx", "getmapy",
	"getobjectat", "getobjectatmouse", "getobjectbyray", "getstringkeys", "getsurfacetype", "gettextheight",
	"gettextwidth", "getz", "hide", "hideimg", "hideimgs", "hidelocal", "hideplayer", "hidesword", "hitnpc",
	"hitobjects", "hitplayer", "hurt", "isadminguild", "iscursoron", "isobject", "keydown", "keydown2", "keyname",
	"lay", "lay2", "lightscene", "loadmap", "mapshapeanimation", "matrixcreate", "matrixcreatefromeuler",
	"matrixmulpoint", "matrixmultiply", "matrixmulvector", "message", "move", "noplayerkilling", "onwall",
	"onwall2", "onwater", "onwater2", "openurl", "openurl2", "play", "play2", "playlooped", "popdialog",
	"pushdialog", "putleaps", "reloadfile", "removetiledefs", "replaceani", "requestfiledeletion",
	"requestfilerename", "requestfilesmove", "requesttext", "resetfocus", "savelog", "say", "say2", "screenx",
	"screeny", "selectfilefordownload", "selectfileforupload", "sendrpgmessage", "sendtext", "sendtorc",
	"serverwarp", "setani", "setbeltcolor", "setbow", "setcharani", "setchargender", "setcoatcolor",
	"setcoloreffect", "setcontentcontrol", "setcursor2", "seteffect", "seteffectmode", "setfocus", "setfogcolors",
	"setgender", "sethead", "setimg", "setimgpart", "setinteriorrendermode", "setletters", "setmap", "setminimap",
	"setmusicvolume", "setplayerdir", "setshadowdetaillevel", "setshape", "setshape2", "setshield",
	"setshoecolor", "setshootparams", "setskincolor", "setskybandcolors", "setskybandsizes", "setsleevecolor",
	"setsuncolors", "setsword", "setterrainrendermode", "setzoomeffect", "shootarrow", "shootball",
	"shootfireball", "shootfireblast", "shootnuke", "show", "showani", "showani2", "showcharacter", "showimg",
	"showimg2", "showlocal", "showpoly", "showpoly2", "showstats", "showtext", "showtext2", "spyfire", "stopmidi",
	"stopsound", "strequals", "synctimeofday", "take", "take2", "takehorse", "takeplayercarry", "takeplayerhorse",
	"testplayer", "throwcarry", "tiletype", "timereverywhere", "toweapons", "triggeraction", "triggerserver",
	"updateboard", "updateterrain", "vectoradd", "vectorcross", "vectordist", "vectordot", "vectorlen",
	"vectornormalize", "vectororthobasis", "vectorscale", "vectorsub", "worldx", "worldy", "wraptext", "wraptext2",
});

// Built-ins the GS2 compiler turns into opcodes.
constexpr auto OpcodeBuiltins = std::to_array<std::string_view>({
	"sleep", "sin", "cos", "arctan", "abs", "random", "arraylen", "sarraylen", "setarray", "int", "exp", "log",
	"min", "max", "pow", "char", "getangle", "getdir", "vecx", "vecy", "waitfor", "format", "makevar",
});

// GS1 functions with no GS2 counterpart (the opcode notes do not map them and the 3.153 tables do not register them).
constexpr auto GS1FunctionsWithoutGS2 = std::to_array<std::string_view>({
	"playersays", "playersays2", "ascii", "lindexof", "onmapx", "onmapy", "hasweapon", "getnpc",
});

// GS1 events (vocab::EVENTS). Any name starting with `action` is a triggeraction event as well.
constexpr auto GS1Events = std::to_array<std::string_view>({
	"created", "playerenters", "playertouchsme", "playertouchsother", "playerendsreading", "playerchats",
	"playerlaysitem", "timeout", "wasshot", "wasshooted", "washit", "wasthrown", "waspelt", "waspushed",
	"waspulled", "exploded", "compusdied", "compsdead", "shotbyplayer", "shotbybaddy", "weaponfired",
	"weaponselected", "playerdies", "playerhurt", "movementfinished", "playerleaves", "playerlogin",
	"playerlogout",
});

// GS1 built-in numeric variables (vocab::BUILTIN_VARS).
constexpr auto GS1BuiltinVars = std::to_array<std::string_view>({
	"x", "y", "timeout", "playerx", "playery", "playerdir", "playerhearts", "playerrupees", "playerbombs",
	"playerdarts", "playerswordpower", "playershieldpower", "playerfullhearts", "playerhp", "playermaxhp", "vecx",
	"vecy", "tokenscount", "paramscount", "weaponscount", "selectedweapon", "chat", "player.chat",
	"player.account", "player.nick",
});

// Words GS1 event names are made of, for the GS2 handler name (`playertouchsme` is `onPlayerTouchsMe`).
constexpr auto EventWords = std::to_array<std::string_view>({
	"movement", "selected", "finished", "shooted", "reading", "exploded", "timeout", "created", "weapon",
	"player", "touchs", "enters", "leaves", "thrown", "pushed", "pulled", "compus", "action", "chats", "other",
	"baddy", "fired", "comps", "shot", "pelt", "hurt", "dies", "dead", "lays", "item", "ends", "was", "me", "by",
});

template <size_t N>
bool contains(const std::array<std::string_view, N>& table, const std::string_view name)
{
	return std::ranges::find(table, name) != table.end();
}

std::string toLower(std::string_view s)
{
	std::string result{s};
	std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

std::string trim(std::string_view s)
{
	const auto first = s.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
		return {};
	const auto last = s.find_last_not_of(" \t\r\n");
	return std::string{s.substr(first, last - first + 1)};
}

bool isEvent(const std::string_view name)
{
	return contains(GS1Events, name) || (name.starts_with("action"sv) && name.size() > 6);
}

bool isPlainName(const std::string_view s)
{
	return !s.empty() && std::ranges::all_of(s, [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '.'; });
}

bool gs2HasCommand(const std::string_view name)
{
	return std::ranges::binary_search(GS2ClientCommands, name)
		|| name == "setlevel"sv || name == "setlevel2"sv || name == "join"sv || contains(OpcodeBuiltins, name);
}

std::optional<std::string_view> renamedCommand(const std::string_view name)
{
	if (name == "setgif"sv) return "setimg"sv;
	if (name == "setgifpart"sv) return "setimgpart"sv;
	return std::nullopt;
}

std::string_view commandHint(const std::string_view name)
{
	if (name == "setstring"sv || name == "addstring"sv || name == "insertstring"sv || name == "replacestring"sv
		|| name == "removestring"sv || name == "deletestring"sv)
		return " (GS2 uses list methods such as add and delete)"sv;
	if (name == "tokenize"sv || name == "tokenize2"sv)
		return " (GS2 uses the tokenize method)"sv;
	return ""sv;
}

// The GS2 name of a GS1 built-in variable. `timeout` is `this.timeout`, but a write to it is `setTimer` (see
// Translator::assignment); the player numbers are the same word on the `player` object.
std::string gs2Name(const std::string_view name)
{
	const auto lower = toLower(name);
	if (lower == "timeout"sv)
		return "this.timeout";
	static constexpr auto playerNumbers = std::to_array<std::string_view>({
		"playerx", "playery", "playerdir", "playerhearts", "playerrupees", "playerbombs", "playerdarts",
		"playerswordpower", "playershieldpower", "playerfullhearts",
	});
	if (contains(playerNumbers, lower))
		return "player." + lower.substr(6);
	return std::string{name};
}

// A plain script variable: no prefix, and not a built-in or an event.
bool isScriptVar(const std::string_view name)
{
	const auto lower = toLower(name);
	return lower.find('.') == std::string::npos && gs2Name(name) == name && !contains(GS1BuiltinVars, lower) && !isEvent(lower);
}

// GS1 built-in variables that are read-only under their GS2 names.
bool builtinWithoutGS2Write(const std::string_view lower)
{
	return lower == "playerhp"sv || lower == "playermaxhp"sv || lower == "weaponscount"sv;
}

// The list a GS1 count variable counts.
std::optional<std::string_view> countedList(const std::string_view lower)
{
	if (lower == "tokenscount"sv) return "tokens"sv;
	if (lower == "paramscount"sv) return "params"sv;
	return std::nullopt;
}

// The GS2 property a GS1 `setcharprop` code sets on the NPC itself.
std::optional<std::string> charProp(const std::string_view code)
{
	if (code == "#c"sv) return "this.chat";
	if (code == "#n"sv) return "this.nick";
	if (code == "#1"sv) return "this.swordimg";
	if (code == "#2"sv) return "this.shieldimg";
	if (code == "#3"sv) return "this.headimg";
	if (code == "#8"sv) return "this.bodyimg";

	auto indexed = [&](std::string_view prefix, std::string_view base, int lo, int hi) -> std::optional<std::string>
	{
		if (!code.starts_with(prefix))
			return std::nullopt;
		const auto digits = code.substr(prefix.size());
		if (digits.empty() || !std::ranges::all_of(digits, [](unsigned char c) { return std::isdigit(c); }))
			return std::nullopt;
		const int i = std::stoi(std::string{digits});
		if (i < lo || i > hi)
			return std::nullopt;
		return std::format("{}[{}]", base, i);
	};
	if (auto r = indexed("#C"sv, "this.colors"sv, 0, 4)) return r;
	return indexed("#P"sv, "this.attr"sv, 1, 30);
}

// GS2 handler name for a GS1 event: `created` is `onCreated`, `playertouchsme` is `onPlayerTouchsMe`.
std::string gs2EventFunction(const std::string_view event)
{
	std::string result = "on";
	std::string_view rest = event;
	while (!rest.empty())
	{
		std::string_view word;
		for (const auto w : EventWords)
		{
			if (rest.starts_with(w) && w.size() > word.size())
				word = w;
		}
		const bool known = !word.empty();
		if (!known)
			word = rest;
		result += static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
		result += word.substr(1);
		if (!known)
			break;
		rest = rest.substr(word.size());
	}
	return result;
}

std::string quote(const std::string_view text)
{
	std::string result = "\"";
	for (const char c : text)
	{
		if (c == '"' || c == '\\')
			result += '\\';
		result += c;
	}
	result += '"';
	return result;
}

// ---- parse-tree helpers ----

std::optional<size_t> tokenType(ParseTree* node)
{
	if (auto* t = dynamic_cast<TerminalNode*>(node); t != nullptr)
		return t->getSymbol()->getType();
	return std::nullopt;
}

// Follow a chain of rules that each have one rule child, down to a variable at most.
ParseTree* descend(ParseTree* node)
{
	while (node != nullptr && node->children.size() == 1 && dynamic_cast<TerminalNode*>(node->children[0]) == nullptr
		&& dynamic_cast<GS1Parser::IdentifierAccessContext*>(node) == nullptr)
		node = node->children[0];
	return node;
}

template <typename T>
void collect(ParseTree* node, std::vector<T*>& out)
{
	if (node == nullptr)
		return;
	if (auto* n = dynamic_cast<T*>(node); n != nullptr)
		out.push_back(n);
	for (auto* child : node->children)
		collect<T>(child, out);
}

template <typename T>
bool any(ParseTree* node)
{
	std::vector<T*> found;
	collect<T>(node, found);
	return !found.empty();
}

// A bare name: `playerenters`, with no index, call or operator around it.
std::optional<std::string> bareName(ParseTree* node)
{
	auto* access = dynamic_cast<GS1Parser::IdentifierAccessContext*>(descend(node));
	if (access == nullptr || access->identifier_value().size() != 1)
		return std::nullopt;
	auto* value = dynamic_cast<GS1Parser::IdentifierValueContext*>(access->identifier_value(0));
	if (value == nullptr || !value->expression().empty())
		return std::nullopt;
	if (any<GS1Parser::Messagecode_stringContext>(value))
		return std::nullopt;
	return toLower(value->getText());
}

bool containsEvent(ParseTree* node)
{
	std::vector<GS1Parser::Compound_identifierContext*> names;
	collect(node, names);
	return std::ranges::any_of(names, [](auto* n) { return isEvent(toLower(n->getText())); });
}

bool callsFunction(ParseTree* node)
{
	return any<GS1Parser::Builtin_functionContext>(node);
}

class ErrorCollector : public antlr4::BaseErrorListener
{
public:
	std::vector<std::string>& errors;
	std::string_view stage;

	ErrorCollector(std::vector<std::string>& errors, std::string_view stage)
		: errors(errors), stage(stage)
	{
	}

	void syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t line, size_t column, const std::string& msg, std::exception_ptr) override
	{
		errors.push_back(std::format("{} error at line {}:{}: {}", stage, line, column, msg));
	}
};

// ---- the translator ----

struct EventFunction
{
	std::string name;
	std::string body;
	bool canReturn = false;
};

struct Item
{
	enum class Kind { Code, Function, Event } kind;
	std::string text;
	size_t event = 0;
};

class Translator
{
public:
	std::vector<std::string> errors;

	std::string program(GS1Parser::ProgramContext* program);

private:
	std::set<std::string> m_flags;
	int m_withDepth = 0;
	int m_temps = 0;

	void err(ParseTree* at, std::string message);

	// Statements.
	std::string block(GS1Parser::BlockContext* block, int indent);
	std::string blockBody(GS1Parser::BlockContext* block, int indent);
	std::string statement(GS1Parser::StatementContext* stmt, int indent);
	std::string command(GS1Parser::StatementBuiltInCommandContext* cmd, int indent);
	std::optional<std::string> stringCommand(GS1Parser::StatementBuiltInCommandContext* cmd, const std::string& name, int indent);
	std::string assignment(GS1Parser::StatementAssignmentContext* stmt, int indent);
	std::string withStatement(GS1Parser::StatementWithContext* stmt, int indent);

	// Expressions.
	std::string expr(ParseTree* node);
	std::string arg(GS1Parser::BuiltInCommandExpressionContext* a);
	std::optional<std::string> rawText(GS1Parser::BuiltInCommandExpressionContext* a);
	std::string call(GS1Parser::BuiltInFunctionCallContext* call);
	std::string text(GS1Parser::CompoundStringContext* str);
	std::optional<std::string> messageCode(GS1Parser::MessageCodeContext* code);
	std::string readName(GS1Parser::IdentifierAccessContext* access);
	std::string writeName(GS1Parser::IdentifierAccessContext* access);
	std::optional<std::pair<std::string, std::vector<ParseTree*>>> nameAndIndex(GS1Parser::IdentifierAccessContext* access);
	std::string name(const std::string& name) const;
	std::string in(GS1Parser::ExpressionInContext* in);
	std::string arrayLiteral(GS1Parser::ArrayLiteralContext* list);
	std::optional<std::string> listProblem(GS1Parser::ArrayLiteralContext* list);

	// Lifting.
	struct Arms
	{
		enum class Kind { NotEvent, Events, Complex } kind = Kind::NotEvent;
		std::vector<std::pair<std::string, std::optional<std::string>>> events;
	};
	Arms eventArms(GS1Parser::ExpressionContext* cond);
	void playerFlags(ParseTree* node);
};

void Translator::err(ParseTree* at, std::string message)
{
	if (auto* rule = dynamic_cast<antlr4::ParserRuleContext*>(at); rule != nullptr && rule->getStart() != nullptr)
		errors.push_back(std::format("line {}: {}", rule->getStart()->getLine(), message));
	else errors.push_back(std::move(message));
}

std::string indentBy(int indent)
{
	return std::string(static_cast<size_t>(indent), '\t');
}

//----------------------------

// Plain names a statement sets or clears as player flags: `set name`, `unset name`, `setstring name,value` with no
// `this.` or `client.` prefix. In GS1 a plain name reads the object's variable and then the flag, so after
// `set gotkey;` the test `if (gotkey)` reads the flag; GS2 never looks a plain name up in the player's flags.
void Translator::playerFlags(ParseTree* node)
{
	std::vector<GS1Parser::StatementBuiltInCommandContext*> commands;
	collect(node, commands);
	for (auto* cmd : commands)
	{
		const auto name = toLower(trim(cmd->COMMAND()->getText()));
		if (name != "set"sv && name != "unset"sv && name != "setstring"sv)
			continue;
		if (cmd->builtInCommandExpression().empty())
			continue;
		if (const auto raw = rawText(cmd->builtInCommandExpression(0)); raw.has_value())
		{
			auto flag = toLower(trim(raw.value()));
			flag = trim(flag.substr(0, flag.find('=')));
			if (!flag.empty() && flag.find('.') == std::string::npos && flag.find('#') == std::string::npos)
				m_flags.insert(flag);
		}
	}
}

// Top-level `if (playerenters)` / `if (created || timeout)` / `if (event && guard)`.
Translator::Arms Translator::eventArms(GS1Parser::ExpressionContext* cond)
{
	Arms result;
	auto complexOrNot = [&]()
	{
		result.kind = containsEvent(cond) ? Arms::Kind::Complex : Arms::Kind::NotEvent;
		result.events.clear();
		return result;
	};

	auto* ternary = dynamic_cast<GS1Parser::ExpressionTernaryContext*>(cond);
	if (ternary == nullptr || !ternary->expression().empty())
		return complexOrNot();

	auto* orExpr = dynamic_cast<GS1Parser::ExpressionLogicOrContext*>(ternary->logicalOrExpression());
	if (orExpr == nullptr)
		return complexOrNot();

	const auto terms = orExpr->logicalAndExpression();
	if (terms.size() > 1)
	{
		for (auto* term : terms)
		{
			const auto name = bareName(term);
			if (!name.has_value() || !isEvent(name.value()))
				return complexOrNot();
			result.events.emplace_back(name.value(), std::nullopt);
		}
		result.kind = Arms::Kind::Events;
		return result;
	}

	auto* andExpr = dynamic_cast<GS1Parser::ExpressionLogicAndContext*>(terms[0]);
	if (andExpr == nullptr)
		return complexOrNot();

	// `a && event && b`: one event among the terms, the rest a guard in source order. A term before the event that
	// calls a function would run in every event in GS1, so that stays complex.
	const auto andTerms = andExpr->equalityExpression();
	std::optional<size_t> eventAt;
	for (size_t i = 0; i < andTerms.size(); ++i)
	{
		const auto name = bareName(andTerms[i]);
		if (name.has_value() && isEvent(name.value()))
		{
			if (eventAt.has_value())
				return complexOrNot();
			eventAt = i;
		}
		else if (containsEvent(andTerms[i]))
			return complexOrNot();
	}
	if (!eventAt.has_value())
		return complexOrNot();
	for (size_t i = 0; i < eventAt.value(); ++i)
	{
		if (callsFunction(andTerms[i]))
			return complexOrNot();
	}

	std::string guard;
	for (size_t i = 0; i < andTerms.size(); ++i)
	{
		if (i == eventAt.value())
			continue;
		if (!guard.empty())
			guard += " && ";
		guard += expr(andTerms[i]);
	}
	result.kind = Arms::Kind::Events;
	result.events.emplace_back(bareName(andTerms[eventAt.value()]).value(), guard.empty() ? std::nullopt : std::optional{guard});
	return result;
}

// Lifts top-level GS1 into GS2 functions. Several `if (created)` blocks (or any repeated event test) become one
// `onCreated`, their bodies in source order: GS1 runs every block whose test is true, top to bottom, and only one event
// is true per run. A block that can `return` is not merged, since the return would skip the blocks after it.
std::string Translator::program(GS1Parser::ProgramContext* program)
{
	playerFlags(program);

	std::vector<Item> items;
	std::vector<EventFunction> events;
	std::map<std::string, size_t> eventIndex;
	bool seenEvent = false;

	auto topLevel = [&](GS1Parser::StatementContext* stmt, ParseTree* node)
	{
		// GS1 runs a statement outside every event test in every event, in source order. It becomes GS2 code outside
		// functions, which the client runs before each event's handler. After the first event test only commands and
		// calls are taken (they set state); an assignment, `if` or loop there could see a different order.
		if (seenEvent && (stmt == nullptr || (stmt->builtinCommandStatement() == nullptr && stmt->userFunctionStatement() == nullptr)))
		{
			err(node, "a top-level statement after an event test is not lowered (GS2 runs code outside functions before the handler); move it above the first event test");
			return;
		}
		auto code = stmt != nullptr ? statement(stmt, 0) : block(dynamic_cast<GS1Parser::BlockContext*>(node), 0) + "\n";
		if (code.empty())
			return;
		if (!items.empty() && items.back().kind == Item::Kind::Code)
			items.back().text += code;
		else items.push_back(Item{.kind = Item::Kind::Code, .text = std::move(code)});
	};

	for (auto* blk : program->block())
	{
		if (blk->TOKEN_BRACE_LEFT() != nullptr)
		{
			topLevel(nullptr, blk);
			continue;
		}

		auto* stmt = blk->statement(0);
		if (stmt == nullptr || (stmt->children.size() == 1 && tokenType(stmt->children[0]) == GS1Parser::END))
			continue;

		if (auto* fn = dynamic_cast<GS1Parser::StatementFunctionDefinitionContext*>(stmt->functionDefinition()); fn != nullptr)
		{
			auto fnName = fn->compound_identifier()->getText();
			items.push_back(Item{.kind = Item::Kind::Function, .text = std::format("function {}() {}\n", fnName, block(fn->block(), 0))});
			continue;
		}

		auto* ifStmt = dynamic_cast<GS1Parser::StatementIfContext*>(stmt->ifStatement());
		if (ifStmt == nullptr)
		{
			topLevel(stmt, stmt);
			continue;
		}

		const auto arms = eventArms(ifStmt->expression());
		if (arms.kind == Arms::Kind::NotEvent)
		{
			topLevel(stmt, stmt);
			continue;
		}
		if (arms.kind == Arms::Kind::Complex)
		{
			err(ifStmt, "event test is too complex to lower; split it into one if per event");
			continue;
		}

		seenEvent = true;
		if (ifStmt->block().size() > 1)
		{
			err(ifStmt, "an event test with else has no single GS2 function");
			continue;
		}

		const auto base = blockBody(ifStmt->block(0), 1);
		const bool canReturn = any<GS1Parser::FlowReturnContext>(ifStmt->block(0));
		for (const auto& [event, guard] : arms.events)
		{
			auto body = guard.has_value() ? std::format("\tif ({}) {{\n{}\t}}\n", guard.value(), blockBody(ifStmt->block(0), 2)) : base;
			auto fnName = gs2EventFunction(event);
			if (const auto it = eventIndex.find(fnName); it != eventIndex.end())
			{
				auto& merged = events[it->second];
				if (merged.canReturn)
				{
					err(ifStmt, std::format("`{}` blocks are not merged because an earlier one can return", fnName));
					continue;
				}
				merged.body += body;
				merged.canReturn = merged.canReturn || canReturn;
			}
			else
			{
				eventIndex.emplace(fnName, events.size());
				items.push_back(Item{.kind = Item::Kind::Event, .event = events.size()});
				events.push_back(EventFunction{.name = std::move(fnName), .body = std::move(body), .canReturn = canReturn});
			}
		}
	}

	std::string result;
	for (const auto& item : items)
	{
		if (item.kind == Item::Kind::Event)
		{
			const auto& fn = events[item.event];
			result += std::format("function {}() {{\n{}}}\n", fn.name, fn.body);
		}
		else result += item.text;
	}
	return result;
}

//----------------------------

std::string Translator::block(GS1Parser::BlockContext* blk, int indent)
{
	return "{\n" + blockBody(blk, indent + 1) + indentBy(indent) + "}";
}

std::string Translator::blockBody(GS1Parser::BlockContext* blk, int indent)
{
	std::string result;
	for (auto* stmt : blk->statement())
		result += statement(stmt, indent);
	return result;
}

std::string Translator::statement(GS1Parser::StatementContext* stmt, int indent)
{
	const auto pad = indentBy(indent);

	if (auto* s = dynamic_cast<GS1Parser::StatementIfContext*>(stmt->ifStatement()); s != nullptr)
	{
		auto result = std::format("{}if ({}) {}", pad, expr(s->expression()), block(s->block(0), indent));
		if (s->block().size() > 1)
			result += " else " + block(s->block(1), indent);
		return result + "\n";
	}

	if (auto* s = dynamic_cast<GS1Parser::StatementForContext*>(stmt->forStatement()); s != nullptr)
	{
		// for (init; cond; step): the parts are optional, so read them in order from the children.
		std::string parts[3];
		size_t part = 0;
		for (auto* child : s->children)
		{
			if (tokenType(child) == GS1Parser::END)
			{
				++part;
				continue;
			}
			if (part > 2)
				break;
			if (auto* a = dynamic_cast<GS1Parser::StatementAssignmentContext*>(child); a != nullptr)
			{
				auto text = assignment(a, 0);
				while (!text.empty() && (text.back() == '\n' || text.back() == ';'))
					text.pop_back();
				parts[part] = text;
			}
			else if (dynamic_cast<GS1Parser::ExpressionContext*>(child) != nullptr)
				parts[part] = expr(child);
		}
		return std::format("{}for ({}; {}; {}) {}\n", pad, parts[0], parts[1], parts[2], block(s->block(), indent));
	}

	if (auto* s = dynamic_cast<GS1Parser::StatementWhileContext*>(stmt->whileStatement()); s != nullptr)
		return std::format("{}while ({}) {}\n", pad, expr(s->expression()), block(s->block(), indent));

	if (auto* s = dynamic_cast<GS1Parser::StatementWithContext*>(stmt->withStatement()); s != nullptr)
		return withStatement(s, indent);

	if (stmt->functionDefinition() != nullptr)
	{
		err(stmt, "a function inside a block is not lowered");
		return {};
	}

	if (auto* flow = stmt->flowStatement(); flow != nullptr)
	{
		if (dynamic_cast<GS1Parser::FlowReturnContext*>(flow) != nullptr) return pad + "return;\n";
		if (dynamic_cast<GS1Parser::FlowBreakContext*>(flow) != nullptr) return pad + "break;\n";
		return pad + "continue;\n";
	}

	if (auto* s = dynamic_cast<GS1Parser::StatementBuiltInCommandContext*>(stmt->builtinCommandStatement()); s != nullptr)
		return command(s, indent);

	if (auto* s = dynamic_cast<GS1Parser::StatementUserFunctionCallContext*>(stmt->userFunctionStatement()); s != nullptr)
		return std::format("{}{}();\n", pad, s->compound_identifier()->getText());

	if (auto* s = dynamic_cast<GS1Parser::StatementAssignmentContext*>(stmt->assignmentStatement()); s != nullptr)
		return assignment(s, indent);

	if (auto* e = stmt->expression(); e != nullptr)
	{
		// `a[i]++` is `a[i] = a[i] + 1`: the element read once and written once.
		if (auto* postfix = dynamic_cast<GS1Parser::ExpressionPostfixContext*>(descend(e)); postfix != nullptr && postfix->children.size() == 2)
		{
			const bool inc = tokenType(postfix->children[1]) == GS1Parser::OP_INC;
			if (auto* access = dynamic_cast<GS1Parser::IdentifierAccessContext*>(descend(postfix->primaryExpression())); access != nullptr)
			{
				const auto target = writeName(access);
				if (target.ends_with(']'))
				{
					if (callsFunction(access))
					{
						err(stmt, "`++`/`--` on an array element whose index calls a function is not lowered; put the index in a variable first");
						return {};
					}
					return std::format("{}{} = {} {} 1;\n", pad, target, target, inc ? '+' : '-');
				}
				return std::format("{}{}{};\n", pad, target, inc ? "++" : "--");
			}
		}
		return std::format("{}{};\n", pad, expr(e));
	}

	// A lone `;`.
	return {};
}

std::string Translator::assignment(GS1Parser::StatementAssignmentContext* stmt, int indent)
{
	const auto pad = indentBy(indent);
	auto* access = dynamic_cast<GS1Parser::IdentifierAccessContext*>(stmt->identifier_access());
	if (access == nullptr)
	{
		err(stmt, "assignment target is not a variable");
		return {};
	}

	const auto opType = tokenType(stmt->assignment_operator()->children[0]).value_or(GS1Parser::OP_ASSIGN);

	// `timeout = t;` starts the timer. Real 5.24r4 and 6.03r7 clients run onTimeout after `setTimer(t)` and never
	// after `this.timeout = t` (lab probe, 2026-11-13).
	if (const auto parts = nameAndIndex(access); parts.has_value() && parts->second.empty() && toLower(parts->first) == "timeout"sv)
	{
		if (opType != GS1Parser::OP_ASSIGN)
		{
			err(stmt, "only `timeout = value` is lowered (as `setTimer(value)`)");
			return {};
		}
		return std::format("{}setTimer({});\n", pad, expr(stmt->expression()));
	}

	const auto target = writeName(access);
	const bool element = target.ends_with(']');

	auto* valueNode = stmt->expression();
	auto* list = dynamic_cast<GS1Parser::ArrayLiteralContext*>(descend(valueNode));

	if (opType == GS1Parser::OP_ASSIGN)
	{
		if (list != nullptr && element)
		{
			err(stmt, "a list stored into an array element is not lowered");
			return {};
		}

		// `name = {a, f(), b}` where the order the elements are read in could matter. GS2 pushes a literal's elements
		// right to left and GS1 reads them left to right, so each element that is not a constant is first stored in a
		// `temp.` variable, in source order, and the literal is built from those.
		if (list != nullptr && listProblem(list).has_value())
		{
			std::vector<GS1Parser::ArrayLiteralContext*> nested;
			for (auto* e : list->expression())
				collect(e, nested);
			if (nested.empty())
			{
				std::string result;
				std::vector<std::string> elements;
				for (auto* e : list->expression())
				{
					auto* lit = descend(e);
					if (dynamic_cast<GS1Parser::LiteralContext*>(lit) != nullptr || (dynamic_cast<GS1Parser::CompoundStringContext*>(lit) != nullptr && !any<GS1Parser::Messagecode_stringContext>(lit)))
					{
						elements.push_back(expr(e));
						continue;
					}
					auto temp = std::format("temp.__gs1list{}", m_temps++);
					result += std::format("{}{} = {};\n", pad, temp, expr(e));
					elements.push_back(std::move(temp));
				}
				std::string joined;
				for (const auto& e : elements)
					joined += (joined.empty() ? "" : ", ") + e;
				return result + std::format("{}{} = {{{}}};\n", pad, target, joined);
			}
		}
		return std::format("{}{} = {};\n", pad, target, expr(valueNode));
	}

	std::string_view op;
	switch (opType)
	{
		case GS1Parser::OP_ASSIGN_ADD: op = "+"sv; break;
		case GS1Parser::OP_ASSIGN_SUB: op = "-"sv; break;
		case GS1Parser::OP_ASSIGN_MUL: op = "*"sv; break;
		case GS1Parser::OP_ASSIGN_DIV: op = "/"sv; break;
		case GS1Parser::OP_ASSIGN_MOD: op = "%"sv; break;
		default: op = "^"sv; break;
	}

	// On an element, a compound assignment is `a[i] = a[i] op v`: the GS1 interpreter reads the element, works the
	// number out and stores it. The index is then read twice, so it must not call anything. `^=` is spelled out too.
	if (element || op == "^"sv)
	{
		if (element && callsFunction(access))
		{
			err(stmt, std::format("`{}=` on an array element whose index calls a function is not lowered; put the index in a variable first", op));
			return {};
		}
		return std::format("{}{} = {} {} ({});\n", pad, target, target, op, expr(valueNode));
	}
	return std::format("{}{} {}= {};\n", pad, target, op, expr(valueNode));
}

// `with (target) body`. The NPC server's notes say that inside `with` the target's `this.` variables and properties are
// read and written, plain script variables keep working, and script functions still run as the original NPC. GS2
// resolves a plain name on the `with` object first and runs a bare call against it, so a plain script variable is
// `thiso.name`, and a body whose meaning would still change is refused.
std::string Translator::withStatement(GS1Parser::StatementWithContext* stmt, int indent)
{
	auto* body = stmt->block();

	// return, sleep, waitfor, or a break/continue that leaves the block.
	std::function<bool(ParseTree*, bool)> escapes = [&](ParseTree* node, bool inLoop) -> bool
	{
		if (node == nullptr)
			return false;
		if (dynamic_cast<GS1Parser::FlowReturnContext*>(node) != nullptr)
			return true;
		if ((dynamic_cast<GS1Parser::FlowBreakContext*>(node) != nullptr || dynamic_cast<GS1Parser::FlowContinueContext*>(node) != nullptr) && !inLoop)
			return true;
		if (auto* cmd = dynamic_cast<GS1Parser::StatementBuiltInCommandContext*>(node); cmd != nullptr && toLower(trim(cmd->COMMAND()->getText())) == "sleep"sv)
			return true;
		if (auto* fn = dynamic_cast<GS1Parser::BuiltInFunctionCallContext*>(node); fn != nullptr && toLower(trim(fn->FUNCTION()->getText())) == "waitfor"sv)
			return true;
		const bool loop = dynamic_cast<GS1Parser::StatementForContext*>(node) != nullptr || dynamic_cast<GS1Parser::StatementWhileContext*>(node) != nullptr;
		return std::ranges::any_of(node->children, [&](ParseTree* c) { return escapes(c, inLoop || loop); });
	};
	if (escapes(body, false))
	{
		err(stmt, "`return`, `sleep`, `waitfor`, or a `break`/`continue` that leaves a `with` block, is not lowered");
		return {};
	}

	// GS1 switches `player`, `client.` and `level.` with the object; GS2 does not.
	std::vector<GS1Parser::Compound_identifierContext*> names;
	collect(body, names);
	for (auto* n : names)
	{
		const auto lower = toLower(n->getText());
		if (lower.starts_with("player"sv) || lower.starts_with("client."sv) || lower.starts_with("level."sv))
		{
			err(stmt, std::format("`{}` inside a GS1 `with` is not lowered (GS1 switches it with the object, GS2 does not)", n->getText()));
			return {};
		}
	}

	if (m_withDepth > 0)
	{
		std::vector<GS1Parser::StatementAssignmentContext*> writes;
		collect(body, writes);
		for (auto* w : writes)
		{
			if (const auto n = bareName(w->identifier_access()); n.has_value() && isScriptVar(n.value()))
			{
				err(stmt, std::format("writing the script variable `{}` inside nested GS1 `with` blocks is not lowered", n.value()));
				return {};
			}
		}
	}

	if (any<GS1Parser::StatementUserFunctionCallContext>(body))
	{
		err(stmt, "a call to one of the script's own functions inside a GS1 `with` is not lowered (GS1 runs it as the original NPC, GS2 looks it up on the `with` object first)");
		return {};
	}

	std::vector<GS1Parser::StatementBuiltInCommandContext*> commands;
	collect(body, commands);
	for (auto* cmd : commands)
	{
		const auto name = toLower(trim(cmd->COMMAND()->getText()));
		if (name == "set"sv || name == "unset"sv || name == "setplayerprop"sv)
		{
			err(stmt, "a GS1 command on the player inside `with` is not lowered (GS1 swaps the player there, GS2's `player` stays the same)");
			return {};
		}
	}

	const auto target = expr(stmt->expression());
	++m_withDepth;
	auto result = std::format("{}with ({}) {}\n", indentBy(indent), target, block(body, indent));
	--m_withDepth;
	return result;
}

std::string Translator::command(GS1Parser::StatementBuiltInCommandContext* cmd, int indent)
{
	const auto pad = indentBy(indent);
	const auto name = toLower(trim(cmd->COMMAND()->getText()));
	const auto args = cmd->builtInCommandExpression();

	if (name == "setarray"sv)
	{
		const auto var = args.empty() ? std::nullopt : rawText(args[0]);
		if (!var.has_value() || !isPlainName(var.value()))
		{
			err(cmd, "`setarray` needs a plain variable name");
			return {};
		}
		return std::format("{}setarray({}, {});\n", pad, this->name(var.value()), args.size() > 1 ? arg(args[1]) : "0");
	}
	if (name == "sleep"sv)
		return std::format("{}sleep({});\n", pad, args.empty() ? "0" : arg(args[0]));

	if (auto s = stringCommand(cmd, name, indent); s.has_value())
		return s.value();

	// `setplayerprop #c,text;` is `player.chat = text;`. 5.24r4 and 6.03r7 have no `setplayerprop`, and send the chat
	// for `player.chat = ...` (lab probe, 2026-11-13). The other properties are not checked on a real client yet.
	if (name == "setplayerprop"sv)
	{
		const auto prop = args.empty() ? std::nullopt : rawText(args[0]);
		if (!prop.has_value() || toLower(trim(prop.value())) != "#c"sv || args.size() != 2)
		{
			err(cmd, "only `setplayerprop #c,text` is lowered (as `player.chat = text`)");
			return {};
		}
		return std::format("{}player.chat = {};\n", pad, arg(args[1]));
	}

	const auto gs2 = std::string{renamedCommand(name).value_or(name)};
	if (!gs2HasCommand(gs2))
	{
		err(cmd, std::format("GS1 command `{}` has no GS2 equivalent{}", gs2, commandHint(gs2)));
		return {};
	}

	std::string list;
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (i > 0)
			list += ", ";
		list += arg(args[i]);
	}
	return std::format("{}{}({});\n", pad, gs2, list);
}

// GS1 commands that become GS2 statements rather than a call by name.
std::optional<std::string> Translator::stringCommand(GS1Parser::StatementBuiltInCommandContext* cmd, const std::string& name, int indent)
{
	const auto pad = indentBy(indent);
	const auto args = cmd->builtInCommandExpression();
	auto textName = [&](size_t i) -> std::optional<std::string>
	{
		if (i >= args.size())
			return std::nullopt;
		const auto raw = rawText(args[i]);
		if (!raw.has_value())
			return std::nullopt;
		auto t = toLower(trim(raw.value()));
		return isPlainName(t) ? std::optional{t} : std::nullopt;
	};
	auto value = [&](size_t i) -> std::string { return i < args.size() ? arg(args[i]) : "\"\""s; };
	auto local = [](const std::string& n) { return n.starts_with("this."sv) || n.starts_with("client."sv); };

	// A player flag goes to the server: `triggeraction(0, 0, "gr.flagset", "name[=value]")` or `"gr.flagdel"`. Real
	// 5.24r4 and 6.03r7 clients have no `set` or `unset` function and send no flag for `player.name = ...` (lab probe,
	// 2026-11-13), but `triggeraction` is a client command (3.153 tables). The server applies the pair with the rules
	// of PLI_FLAGSET / PLI_FLAGDEL and sends the flag back, so a later `player.name` read sees it (one round trip).
	auto flagCall = [&](std::string_view action, const std::string& pair)
	{ return std::format("{}triggeraction(0, 0, \"{}\", {});\n", pad, action, pair); };

	// `set name;` / `unset name;`: the flag is named as text. A `client.` or `this.` name stays on the client
	// (`client.` is sent as a flag by the client itself).
	if (name == "set"sv || name == "unset"sv)
	{
		const auto raw = args.size() == 1 ? rawText(args[0]) : std::nullopt;
		if (!raw.has_value())
		{
			err(cmd, std::format("`{}` needs one flag name", name));
			return std::string{};
		}
		auto flag = trim(raw.value());
		std::optional<std::string> flagValue;
		if (const auto eq = flag.find('='); eq != std::string::npos)
		{
			flagValue = flag.substr(eq + 1);
			flag = trim(flag.substr(0, eq));
		}
		const auto lower = toLower(flag);
		if (!isPlainName(lower))
		{
			err(cmd, std::format("`{} {}`: only a plain flag name is lowered", name, flag));
			return std::string{};
		}
		if (local(lower))
		{
			if (name == "unset"sv)
				return std::format("{}{} = \"\";\n", pad, this->name(lower));
			return std::format("{}{} = {};\n", pad, this->name(lower), flagValue.has_value() ? quote(flagValue.value()) : "true"s);
		}
		if (name == "unset"sv)
			return flagCall("gr.flagdel"sv, quote(flag));
		return flagCall("gr.flagset"sv, quote(flagValue.has_value() ? flag + "=" + flagValue.value() : flag));
	}

	if (name == "setstring"sv)
	{
		const auto var = textName(0);
		if (!var.has_value())
		{
			err(cmd, "`setstring` needs a plain string name");
			return std::string{};
		}
		if (local(var.value()))
			return std::format("{}{} = {};\n", pad, this->name(var.value()), value(1));

		// Any other name is a player flag. An empty value removes it, as PLI_FLAGSET does.
		const auto flag = trim(rawText(args[0]).value_or(var.value()));
		return flagCall("gr.flagset"sv, std::format("{} @ ({})", quote(flag + "="), value(1)));
	}

	if (name == "addstring"sv || name == "insertstring"sv || name == "replacestring"sv || name == "removestring"sv || name == "deletestring"sv)
	{
		const auto var = textName(0);
		if (!var.has_value())
		{
			err(cmd, std::format("`{}` needs a plain string name", name));
			return std::string{};
		}
		if (!local(var.value()))
		{
			err(cmd, std::format("`{}` on `{}`: only `this.` and `client.` lists are lowered (a plain GS1 string is a player flag)", name, var.value()));
			return std::string{};
		}
		const auto method = name == "addstring"sv ? "add"sv : name == "insertstring"sv ? "insert"sv : name == "replacestring"sv ? "replace"sv : name == "removestring"sv ? "remove"sv : "delete"sv;
		std::string list;
		for (size_t i = 1; i < args.size(); ++i)
			list += (i > 1 ? ", " : "") + arg(args[i]);
		return std::format("{}{}.{}({});\n", pad, this->name(var.value()), method, list);
	}

	if (name == "tokenize"sv || name == "tokenize2"sv)
	{
		const auto seps = name == "tokenize2"sv ? std::format("\" ,\" @ {}", value(1)) : ""s;
		return std::format("{}tokens = ({}).tokenize({});\n", pad, value(0), seps);
	}

	if (name == "setcharprop"sv)
	{
		const auto code = args.empty() ? ""s : trim(rawText(args[0]).value_or(""));
		const auto target = charProp(code);
		if (!target.has_value())
		{
			err(cmd, std::format("`setcharprop {}` has no GS2 property here", code));
			return std::string{};
		}
		return std::format("{}{} = {};\n", pad, target.value(), value(1));
	}

	return std::nullopt;
}

//----------------------------

std::string Translator::arg(GS1Parser::BuiltInCommandExpressionContext* a)
{
	// Item, carry, colour, direction and baddy names go as the name, as GS2 commands take them.
	if (auto* special = a->special_literal(); special != nullptr)
		return quote(special->getText());
	return expr(a->expression());
}

// The text of an argument written as plain words: a string with no message codes, a variable name, or a code.
std::optional<std::string> Translator::rawText(GS1Parser::BuiltInCommandExpressionContext* a)
{
	if (auto* special = a->special_literal(); special != nullptr)
		return special->getText();

	auto* node = descend(a->expression());
	if (auto* str = dynamic_cast<GS1Parser::CompoundStringContext*>(node); str != nullptr)
	{
		if (!str->messagecode_string().empty())
			return std::nullopt;
		std::string result;
		for (auto* s : str->STRING())
			result += s->getText();
		return trim(result);
	}
	if (auto* access = dynamic_cast<GS1Parser::IdentifierAccessContext*>(node); access != nullptr)
		return access->getText();
	if (auto* primary = dynamic_cast<GS1Parser::PrimaryExpressionContext*>(node); primary != nullptr && primary->messagecode_string() != nullptr)
		return primary->messagecode_string()->getText();
	if (auto* code = dynamic_cast<GS1Parser::MessageCodeContext*>(node); code != nullptr)
		return code->getText();
	if (auto* lit = dynamic_cast<GS1Parser::LiteralContext*>(node); lit != nullptr)
		return lit->getText();
	return std::nullopt;
}

std::string Translator::expr(ParseTree* node)
{
	if (node == nullptr)
		return "0";

	if (auto* t = dynamic_cast<TerminalNode*>(node); t != nullptr)
	{
		switch (t->getSymbol()->getType())
		{
			case GS1Parser::LITERAL: return t->getText();
			case GS1Parser::OP_ADD: return " + ";
			case GS1Parser::OP_SUB: return " - ";
			case GS1Parser::OP_MUL: return " * ";
			case GS1Parser::OP_DIV: return " / ";
			case GS1Parser::OP_MOD: return " % ";
			case GS1Parser::OP_POW: return " ^ ";
			default: return t->getText();
		}
	}

	if (auto* e = dynamic_cast<GS1Parser::ExpressionTernaryContext*>(node); e != nullptr)
	{
		if (!e->expression().empty())
		{
			err(e, "`?:` is not lowered (the GS2 compiler has no conditional expression)");
			return "0";
		}
		return expr(e->logicalOrExpression());
	}

	auto joined = [&](ParseTree* n, std::string_view op) -> std::string
	{
		std::vector<std::string> parts;
		for (auto* child : n->children)
		{
			if (dynamic_cast<TerminalNode*>(child) == nullptr)
				parts.push_back(expr(child));
		}
		if (parts.size() == 1)
			return parts[0];
		std::string result = "(";
		for (size_t i = 0; i < parts.size(); ++i)
			result += (i > 0 ? std::string{op} : ""s) + parts[i];
		return result + ")";
	};

	if (auto* e = dynamic_cast<GS1Parser::ExpressionLogicOrContext*>(node); e != nullptr)
		return joined(e, " || "sv);
	if (auto* e = dynamic_cast<GS1Parser::ExpressionLogicAndContext*>(node); e != nullptr)
		return joined(e, " && "sv);

	if (auto* e = dynamic_cast<GS1Parser::ExpressionEqualityContext*>(node); e != nullptr)
	{
		if (e->children.size() == 1)
			return expr(e->children[0]);
		const auto op = tokenType(e->children[1]) == GS1Parser::OP_NOTEQ ? " != "sv : " == "sv;
		return std::format("({}{}{})", expr(e->children[0]), op, expr(e->children[2]));
	}

	if (auto* e = dynamic_cast<GS1Parser::ExpressionRelationalContext*>(node); e != nullptr)
	{
		if (e->children.size() == 1)
			return expr(e->children[0]);
		return std::format("({} {} {})", expr(e->children[0]), e->children[1]->getText(), expr(e->children[2]));
	}

	if (auto* e = dynamic_cast<GS1Parser::ExpressionInContext*>(node); e != nullptr)
		return in(e);

	if (dynamic_cast<GS1Parser::ExpressionAdditiveContext*>(node) != nullptr
		|| dynamic_cast<GS1Parser::ExpressionMultiplicativeContext*>(node) != nullptr
		|| dynamic_cast<GS1Parser::ExpressionExponentiationContext*>(node) != nullptr)
	{
		if (node->children.size() == 1)
			return expr(node->children[0]);
		std::string result = "(";
		for (auto* child : node->children)
			result += expr(child);
		return result + ")";
	}

	if (auto* e = dynamic_cast<GS1Parser::ExpressionUnaryContext*>(node); e != nullptr)
	{
		const auto operand = expr(e->unaryExpression());
		switch (tokenType(e->children[0]).value_or(0))
		{
			case GS1Parser::OP_SUB: return std::format("(-{})", operand);
			case GS1Parser::OP_LOGICALNOT: return std::format("(!{})", operand);
			default: return operand;
		}
	}

	if (auto* e = dynamic_cast<GS1Parser::ExpressionPostfixContext*>(node); e != nullptr)
	{
		if (e->children.size() == 1)
			return expr(e->children[0]);
		auto* access = dynamic_cast<GS1Parser::IdentifierAccessContext*>(descend(e->primaryExpression()));
		if (access == nullptr)
		{
			err(e, "`++`/`--` needs a variable");
			return "0";
		}
		return writeName(access) + e->children[1]->getText();
	}

	if (auto* e = dynamic_cast<GS1Parser::PrimaryExpressionContext*>(node); e != nullptr)
	{
		if (e->expression() != nullptr)
			return "(" + expr(e->expression()) + ")";
		if (auto* code = dynamic_cast<GS1Parser::MessageCodeContext*>(e->messagecode_string()); code != nullptr)
			return messageCode(code).value_or(quote(code->getText()));
		if (e->children.size() == 1)
			return expr(e->children[0]);
		return "0";
	}

	if (auto* e = dynamic_cast<GS1Parser::LiteralContext*>(node); e != nullptr)
	{
		if (e->LITERAL() != nullptr)
			return e->LITERAL()->getText();
		const auto reserved = toLower(e->getText());
		if (reserved == "pi"sv)
			return "3.14159265358979";
		return "65535"; // allstats, allfeatures
	}

	if (auto* e = dynamic_cast<GS1Parser::ArrayLiteralContext*>(node); e != nullptr)
		return arrayLiteral(e);

	if (auto* e = dynamic_cast<GS1Parser::BuiltInFunctionCallContext*>(node); e != nullptr)
		return call(e);

	if (auto* e = dynamic_cast<GS1Parser::CompoundStringContext*>(node); e != nullptr)
		return text(e);

	if (auto* e = dynamic_cast<GS1Parser::IdentifierAccessContext*>(node); e != nullptr)
		return readName(e);

	if (auto* e = dynamic_cast<GS1Parser::MessageCodeContext*>(node); e != nullptr)
		return messageCode(e).value_or(quote(e->getText()));

	if (dynamic_cast<GS1Parser::Special_literalContext*>(node) != nullptr)
		return quote(node->getText());

	// Rules with one child, such as unaryExpression -> postfixExpression.
	if (node->children.size() == 1)
		return expr(node->children[0]);

	err(node, std::format("`{}` is not lowered", node->getText()));
	return "0";
}

// `x in <a, b>`, `x in |a, b|`, `x in {a, b, c}` and `x in array`.
std::string Translator::in(GS1Parser::ExpressionInContext* e)
{
	const auto values = e->additiveExpression();
	if (values.size() == 1 && e->OP_IN() == nullptr)
		return expr(values[0]);
	if (values.size() != 1)
	{
		err(e, "`a, b in ...` is not lowered");
		return "0";
	}

	if (auto* range = dynamic_cast<GS1Parser::RangeLiteralContext*>(e->range_literal()); range != nullptr)
	{
		// GS1 `|a, b|` takes both ends; `<` or `>` leaves that end out. GS2's `in <a, b>` takes both ends.
		const bool inclusive = tokenType(range->children.front()) == GS1Parser::TOKEN_PIPE && tokenType(range->children.back()) == GS1Parser::TOKEN_PIPE;
		if (!inclusive)
		{
			err(e, "a GS1 range that leaves an end out (`<a, b>`) is not lowered; GS2's range takes both ends");
			return "0";
		}
		return std::format("({} in <{}, {}>)", expr(values[0]), expr(range->expression(0)), expr(range->expression(1)));
	}

	auto* right = e->primaryExpression();
	if (auto* list = dynamic_cast<GS1Parser::ArrayLiteralContext*>(descend(right)); list != nullptr)
	{
		// `x in {a, b, c}` is `x == a || x == b || x == c`, which is what the GS1 interpreter does.
		if (callsFunction(values[0]))
		{
			err(e, "`x in {list}` where x calls a function is not lowered; put x in a variable first");
			return "0";
		}
		const auto x = expr(values[0]);
		std::string result;
		for (auto* item : list->expression())
			result += (result.empty() ? "" : " || ") + std::format("{} == {}", x, expr(item));
		return result.empty() ? "false" : "(" + result + ")";
	}
	return std::format("({} in {})", expr(values[0]), expr(right));
}

// Why a GS1 array literal cannot be pushed in GS2's right-to-left order, or nothing when it can: no element calls
// anything, or exactly one does and the rest are constants.
std::optional<std::string> Translator::listProblem(GS1Parser::ArrayLiteralContext* list)
{
	const auto items = list->expression();
	size_t effects = 0, constants = 0;
	for (auto* item : items)
	{
		std::vector<GS1Parser::ArrayLiteralContext*> nested;
		collect(item, nested);
		if (!nested.empty())
			return "GS1 array elements that are lists are not lowered";
		if (callsFunction(item))
			++effects;
		auto* lit = descend(item);
		if (dynamic_cast<GS1Parser::LiteralContext*>(lit) != nullptr || (dynamic_cast<GS1Parser::CompoundStringContext*>(lit) != nullptr && !any<GS1Parser::Messagecode_stringContext>(lit)))
			++constants;
	}
	if (effects == 0 || (effects == 1 && constants + 1 == items.size()))
		return std::nullopt;
	return "GS1 array elements with calls are lowered only when one element calls and the others are constants (GS1 reads them left to right, GS2 pushes them right to left); put the values in variables first";
}

std::string Translator::arrayLiteral(GS1Parser::ArrayLiteralContext* list)
{
	if (const auto problem = listProblem(list); problem.has_value())
	{
		err(list, problem.value());
		return "0";
	}
	std::string result;
	for (auto* item : list->expression())
		result += (result.empty() ? "" : ", ") + expr(item);
	return "{" + result + "}";
}

// A built-in function call. GS1-only built-ins are lowered to the GS2 methods that do the same.
std::string Translator::call(GS1Parser::BuiltInFunctionCallContext* fn)
{
	const auto name = toLower(trim(fn->FUNCTION()->getText()));
	std::vector<std::string> args;
	for (auto* a : fn->expression())
		args.push_back(expr(a));

	auto arity = [&](size_t n) -> bool
	{
		if (args.size() == n)
			return true;
		err(fn, std::format("`{}` expects {} argument(s), got {}", name, n, args.size()));
		return false;
	};

	if (name == "strlen"sv)
		return arity(1) ? std::format("({}).length()", args[0]) : "0";
	if (name == "startswith"sv)
		return arity(2) ? std::format("({}).starts({})", args[0], args[1]) : "0";
	if (name == "indexof"sv)
		return arity(2) ? std::format("({}).pos({})", args[0], args[1]) : "0";
	if (name == "strcontains"sv)
		return arity(2) ? std::format("(({}).pos({}) >= 0)", args[0], args[1]) : "0";
	if (name == "strtofloat"sv)
		return arity(1) ? std::format("(({}) + 0)", args[0]) : "0";
	// GS1 `getplayer(account)` is the player with that account; the 3.153 client registers `findplayer` for it.
	if (name == "getplayer"sv)
		return arity(1) ? std::format("findplayer({})", args[0]) : "0";

	if (contains(GS1FunctionsWithoutGS2, name))
	{
		err(fn, std::format("GS1 function `{}` has no GS2 equivalent", name));
		return "0";
	}
	if (name == "format"sv && args.empty())
	{
		err(fn, "`format` expects at least 1 argument, got 0");
		return "0";
	}

	std::string list;
	for (const auto& a : args)
		list += (list.empty() ? "" : ", ") + a;
	return std::format("{}({})", name, list);
}

// GS1 text with message codes, as GS2 joins: `"hi #n"` is `"hi " @ player.nick`.
std::string Translator::text(GS1Parser::CompoundStringContext* str)
{
	std::vector<std::string> parts;
	std::string literal;
	for (auto* child : str->children)
	{
		if (auto* t = dynamic_cast<TerminalNode*>(child); t != nullptr)
		{
			if (t->getSymbol()->getType() == GS1Parser::STRING)
				literal += t->getText();
			continue;
		}
		if (auto* code = dynamic_cast<GS1Parser::MessageCodeContext*>(child); code != nullptr)
		{
			if (auto value = messageCode(code); value.has_value())
			{
				if (!literal.empty())
					parts.push_back(quote(std::exchange(literal, {})));
				parts.push_back(std::move(value.value()));
			}
			else literal += code->getText();
		}
	}
	if (!literal.empty() || parts.empty())
		parts.push_back(quote(literal));

	// GS1 trims the text it builds: the space after a command's name is not part of its argument.
	auto trimPart = [](std::string& part, bool front)
	{
		if (part.size() < 2 || part.front() != '"')
			return;
		auto inner = part.substr(1, part.size() - 2);
		const auto at = front ? inner.find_first_not_of(" \t\r\n") : inner.find_last_not_of(" \t\r\n");
		if (at == std::string::npos)
			inner.clear();
		else inner = front ? inner.substr(at) : inner.substr(0, at + 1);
		part = "\"" + inner + "\"";
	};
	trimPart(parts.front(), true);
	trimPart(parts.back(), false);
	if (parts.size() > 1 && parts.front() == "\"\"")
		parts.erase(parts.begin());
	if (parts.size() > 1 && parts.back() == "\"\"")
		parts.pop_back();
	if (parts.size() == 1)
		return parts[0];

	std::string result = "(";
	for (size_t i = 0; i < parts.size(); ++i)
		result += (i > 0 ? " @ " : "") + parts[i];
	return result + ")";
}

// One message code as a GS2 value, or nothing when the code stays text (a `#` that starts no code GS2 has a value
// for stays text, as in GS1). Codes with no GS2 lowering are errors.
std::optional<std::string> Translator::messageCode(GS1Parser::MessageCodeContext* code)
{
	const auto token = code->MESSAGECODE()->getText();
	const auto args = code->expression();
	const auto c = token.size() > 1 ? token[1] : '\0';
	const auto digits = token.size() > 2;

	auto needArgs = [&](size_t n) -> bool
	{
		if (args.size() == n)
			return true;
		err(code, std::format("`{}` needs {} argument(s)", token, n));
		return false;
	};

	if (args.empty())
	{
		if (c == 'c' && !digits) return "player.chat";
		if (c == 'n' && !digits) return "player.nick";
		if (c == 'a' && !digits) return "player.account";
		if ((c == 'w' || c == 'L' || c == 'F') && !digits)
		{
			err(code, std::format("message code `{}` has no GS2 lowering", token));
			return "\"\"";
		}
		if (c == 'P' && digits)
		{
			err(code, std::format("message code `{}` has no GS2 lowering", token));
			return "\"\"";
		}
		return std::nullopt;
	}

	switch (c)
	{
		case 'v':
			return needArgs(1) ? std::optional{"(" + expr(args[0]) + ")"} : std::optional{"\"\""s};
		case 's':
		{
			if (!needArgs(1))
				return "\"\"";
			const auto n = toLower(args[0]->getText());
			for (const auto prefix : {"this."sv, "client."sv, "clientr."sv, "serverr."sv})
			{
				if (n.starts_with(prefix) && isPlainName(n))
					return n;
			}
			err(code, std::format("`#s({})`: only `this.`, `client.`, `clientr.` and `serverr.` strings are lowered (a plain GS1 string is a player flag)", args[0]->getText()));
			return "\"\"";
		}
		case 't':
			return needArgs(1) ? std::optional{"tokens[" + expr(args[0]) + "]"} : std::optional{"\"\""s};
		case 'p':
			return needArgs(1) ? std::optional{"params[" + expr(args[0]) + "]"} : std::optional{"\"\""s};
		case 'T':
			return needArgs(1) ? std::optional{"(" + expr(args[0]) + ").trim()"} : std::optional{"\"\""s};
		case 'e':
			return needArgs(3) ? std::optional{std::format("({}).substring({}, {})", expr(args[2]), expr(args[0]), expr(args[1]))} : std::optional{"\"\""s};
		default:
			err(code, std::format("message code `{}` has no GS2 lowering", token));
			return "\"\"";
	}
}

// A variable's name and index. An element must have no field after its index (`npcs[i].x`), and a name built from
// message codes is not lowered.
std::optional<std::pair<std::string, std::vector<ParseTree*>>> Translator::nameAndIndex(GS1Parser::IdentifierAccessContext* access)
{
	const auto values = access->identifier_value();
	std::string fullName;
	std::vector<ParseTree*> index;
	for (size_t i = 0; i < values.size(); ++i)
	{
		auto* value = dynamic_cast<GS1Parser::IdentifierValueContext*>(values[i]);
		if (value == nullptr)
			return std::nullopt;
		if (any<GS1Parser::Messagecode_stringContext>(value->compound_identifier()))
		{
			err(access, std::format("a variable name built from message codes (`{}`) is not lowered", access->getText()));
			return std::nullopt;
		}
		if (!value->expression().empty() && i + 1 < values.size())
		{
			err(access, std::format("a field after an index (`{}`) is not lowered", access->getText()));
			return std::nullopt;
		}
		fullName += (i > 0 ? "." : "") + value->compound_identifier()->getText();
		for (auto* e : value->expression())
			index.push_back(e);
	}
	if (index.size() > 1)
	{
		err(access, std::format("a two-part index (`{}`) is not lowered", access->getText()));
		return std::nullopt;
	}
	const auto lower = toLower(fullName);
	if (!index.empty() && (lower == "board"sv || lower == "tiles"sv))
	{
		err(access, std::format("`{}[..]` has no fixture-checked GS2 encoding; it is not lowered", fullName));
		return std::nullopt;
	}
	return std::make_pair(fullName, index);
}

// A variable name as GS2 names it. Inside a GS1 `with`, a plain script variable stays the NPC's own: `thiso.name`.
std::string Translator::name(const std::string& n) const
{
	if (m_withDepth == 1 && isScriptVar(n))
		return "thiso." + n;
	return gs2Name(n);
}

// A read of a GS1 name. Names whose GS2 form differs from a plain variable are read as a real 5.24r4 client reads them
// (seihai-core lab probe, 2026-11-13).
std::string Translator::readName(GS1Parser::IdentifierAccessContext* access)
{
	const auto parts = nameAndIndex(access);
	if (!parts.has_value())
		return "0";
	const auto& [n, index] = parts.value();
	const auto lower = toLower(n);

	if (index.empty())
	{
		if (lower.find('.') == std::string::npos && isEvent(lower))
		{
			err(access, std::format("`{}` is an event flag and has no GS2 value; the handler is {}", n, gs2EventFunction(lower)));
			return "0";
		}
		if (const auto list = countedList(lower); list.has_value())
			return std::format("{}.size()", list.value());
		if (lower == "playerhp"sv) return "player.hearts";
		if (lower == "playermaxhp"sv) return "player.fullhearts";
		if (lower == "weaponscount"sv) return "player.weapons.size()";
		if (lower == "selectedweapon"sv) return "selectedweapon";
		if (m_withDepth == 0 && isScriptVar(n) && m_flags.contains(lower))
			return "player." + n;
		return name(n);
	}
	return std::format("{}[{}]", name(n), expr(index[0]));
}

// A write to a GS1 name.
std::string Translator::writeName(GS1Parser::IdentifierAccessContext* access)
{
	const auto parts = nameAndIndex(access);
	if (!parts.has_value())
		return "temp.__gs1invalid";
	const auto& [n, index] = parts.value();
	const auto lower = toLower(n);

	if (index.empty())
	{
		if (isScriptVar(n) && m_flags.contains(lower))
			err(access, std::format("`{0}` is a player flag the script sets; a plain `{0} = ...` would shadow it with a script variable, which has no faithful GS2 form (`set` and `setstring` are lowered)", n));
		else if (builtinWithoutGS2Write(lower))
			err(access, std::format("writing the GS1 variable `{}` has no client-checked GS2 form; it is not lowered", n));
		else if (countedList(lower).has_value())
			err(access, std::format("`{}` cannot be written", n));
		return name(n);
	}
	return std::format("{}[{}]", name(n), expr(index[0]));
}

} // namespace

//----------------------------

GS2Translation translateToGS2(const std::string_view gs1Source)
{
	GS2Translation result;

	// The NPC-server keeps clientside lines apart with \xa7.
	std::string source{gs1Source};
	std::ranges::replace(source, '\xa7', '\n');

	ErrorCollector lexErrors{result.errors, "GS1 lexing"};
	ErrorCollector parseErrors{result.errors, "GS1 parsing"};

	antlr4::ANTLRInputStream input;
	input.load(source.data(), source.length(), true);

	GS1Lexer lexer{&input};
	lexer.removeErrorListeners();
	lexer.addErrorListener(&lexErrors);

	antlr4::CommonTokenStream tokens{&lexer};
	tokens.fill();
	if (!result.errors.empty())
		return result;

	GS1Parser parser{&tokens};
	parser.removeErrorListeners();
	parser.addErrorListener(&parseErrors);
	parser.setErrorHandler(std::make_shared<antlr4::BailErrorStrategy>());

	GS1Parser::ProgramContext* program = nullptr;
	try
	{
		program = parser.program();
	}
	catch (const antlr4::ParseCancellationException&)
	{
		if (result.errors.empty())
			result.errors.emplace_back("GS1 parsing failed");
		return result;
	}
	catch (const std::exception& e)
	{
		result.errors.emplace_back(std::format("GS1 parsing failed: {}", e.what()));
		return result;
	}
	if (!result.errors.empty() || program == nullptr)
		return result;

	Translator translator;
	result.source = translator.program(program);
	result.errors = std::move(translator.errors);
	return result;
}

ClientByteCode compileClientScript(const std::string_view clientSource)
{
	ClientByteCode result;

	std::string source{clientSource};
	std::ranges::replace(source, '\xa7', '\n');
	const auto first = source.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return result;

	// Already GS2.
	if (const auto view = std::string_view{source}.substr(first); view.size() >= 6 && toLower(view.substr(0, 6)) == "//#gs2"sv)
		result.gs2Source = source;
	else
	{
		auto translation = translateToGS2(source);
		if (!translation.ok())
		{
			result.errors = std::move(translation.errors);
			return result;
		}
		result.gs2Source = std::move(translation.source);
	}

	GS2Context context;
	auto response = context.compile(result.gs2Source);
	if (!response.success)
	{
		for (const auto& error : response.errors)
			result.errors.push_back(std::format("GS2 compiler: {}", error.msg()));
		if (result.errors.empty())
			result.errors.emplace_back("GS2 compiler: failed");
		return result;
	}
	result.bytecode.assign(response.bytecode.buffer(), response.bytecode.buffer() + response.bytecode.length());
	return result;
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal::gs1
