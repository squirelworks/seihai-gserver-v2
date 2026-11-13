// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// GS1 client scripts translated to GS2 for 4.x and later clients (five-client lab, #27).

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>

#include <compiler/GS2Context.h>

#include <scripting/gs1/GS1ToGS2.h>

using namespace preagonal;
using namespace std::literals;

std::atomic_bool shutdownProgram{false};

namespace
{
	std::string gs2(std::string_view gs1)
	{
		auto t = gs1::translateToGS2(gs1);
		INFO(gs1);
		for (const auto& e : t.errors)
			UNSCOPED_INFO(e);
		REQUIRE(t.ok());
		return t.source;
	}

	bool refused(std::string_view gs1, std::string_view why)
	{
		const auto t = gs1::translateToGS2(gs1);
		return !t.ok() && std::ranges::any_of(t.errors, [why](const std::string& e) { return e.find(why) != std::string::npos; });
	}

	std::vector<uint8_t> compileGS2(const std::string& source)
	{
		GS2Context context;
		auto response = context.compile(source);
		REQUIRE(response.success);
		return {response.bytecode.buffer(), response.bytecode.buffer() + response.bytecode.length()};
	}

	constexpr auto Door = R"(if (playerenters) {
  setimg door.png;
  this.open = 0;
}
if (playertouchsme && this.open == 0) {
  this.open = 1;
  setimg dooropen.png;
  play door.wav;
  timeout = 1;
}
if (timeout) {
  this.open = 0;
  setimg door.png;
}
)"sv;
} // namespace

TEST_CASE("GS1 event tests become GS2 event functions", "[GS1ToGS2]")
{
	CHECK(gs2(Door) == R"(function onPlayerEnters() {
	setimg("door.png");
	this.open = 0;
}
function onPlayerTouchsMe() {
	if ((this.open == 0)) {
		this.open = 1;
		setimg("dooropen.png");
		play("door.wav");
		setTimer(1);
	}
}
function onTimeout() {
	this.open = 0;
	setimg("door.png");
}
)");

	// Repeated tests of one event merge in source order; `a || b` gives each event the body.
	CHECK(gs2("if (created) { x = 1; }\nif (created) { y = 2; }\n") == "function onCreated() {\n\tx = 1;\n\ty = 2;\n}\n");
	CHECK(gs2("if (created || playerenters) hide;\n") == "function onCreated() {\n\thide();\n}\nfunction onPlayerEnters() {\n\thide();\n}\n");

	// Code above the first event test runs before each handler; commands after it are kept too.
	CHECK(gs2("dontblock;\nthis.a = 1;\nif (created) show;\nsetshape 1,32,32;\n") == "dontblock();\nthis.a = 1;\nfunction onCreated() {\n\tshow();\n}\nsetshape(1, 32, 32);\n");

	// Script functions keep their names.
	CHECK(gs2("function hit() { this.hp -= 1; }\nif (washit) hit();\n") == "function hit() {\n\tthis.hp -= 1;\n}\nfunction onWasHit() {\n\thit();\n}\n");
}

TEST_CASE("GS1 commands become GS2 calls and statements", "[GS1ToGS2]")
{
	CHECK(gs2("if (created) { noplayerkilling; lay greenrupee; setcoatcolor white; setgif x.gif; }") ==
		"function onCreated() {\n\tnoplayerkilling();\n\tlay(\"greenrupee\");\n\tsetcoatcolor(\"white\");\n\tsetimg(\"x.gif\");\n}\n");
	CHECK(gs2("if (created) showimg 1,light.png,x,y;") == "function onCreated() {\n\tshowimg(1, \"light.png\", x, y);\n}\n");
	CHECK(gs2("if (created) { setstring this.msg,ok; setstring client.n,#a; }") ==
		"function onCreated() {\n\tthis.msg = \"ok\";\n\tclient.n = player.account;\n}\n");
	// 5.24r4 and 6.03r7 have no set/unset and send a flag only for `client.` (lab probe, 2026-11-13).
	CHECK(gs2("if (created) { set client.seen; unset client.seen; set this.open; }") ==
		"function onCreated() {\n\tclient.seen = true;\n\tclient.seen = \"\";\n\tthis.open = true;\n}\n");
	// A player flag goes to the server as a trigger (seihai-core #30); the server applies it and sends it back.
	CHECK(gs2("if (created) { set gotKey; unset gotKey; setstring quest,done; setstring quest,; }") ==
		"function onCreated() {\n\ttriggeraction(0, 0, \"gr.flagset\", \"gotKey\");\n\ttriggeraction(0, 0, \"gr.flagdel\", \"gotKey\");\n"
		"\ttriggeraction(0, 0, \"gr.flagset\", \"quest=\" @ (\"done\"));\n"
		"\ttriggeraction(0, 0, \"gr.flagset\", \"quest=\" @ (\"\"));\n}\n");
	CHECK(gs2("if (created) setstring server.count,#v(playerrupees);") ==
		"function onCreated() {\n\ttriggeraction(0, 0, \"gr.flagset\", \"server.count=\" @ ((player.rupees)));\n}\n");
	CHECK(gs2("if (created) { set gotkey; if (gotkey) say2 yes; }") ==
		"function onCreated() {\n\ttriggeraction(0, 0, \"gr.flagset\", \"gotkey\");\n\tif (player.gotkey) {\n\t\tsay2(\"yes\");\n\t}\n}\n");
	CHECK(gs2("if (playerchats) tokenize #c;") == "function onPlayerChats() {\n\ttokens = (player.chat).tokenize();\n}\n");
	CHECK(gs2("if (created) setcharprop #3,head3.png;") == "function onCreated() {\n\tthis.headimg = \"head3.png\";\n}\n");
	CHECK(gs2("if (created) setplayerprop #c,Hello #n;") == "function onCreated() {\n\tplayer.chat = (\"Hello \" @ player.nick);\n}\n");
	CHECK(refused("if (created) setplayerprop #n,Bob;", "only `setplayerprop #c,text`"));
	CHECK(gs2("if (created) { addstring this.list,a; deletestring this.list,0; }") ==
		"function onCreated() {\n\tthis.list.add(\"a\");\n\tthis.list.delete(0);\n}\n");

	CHECK(refused("if (created) addstring list,a;", "only `this.` and `client.` lists"));
	CHECK(refused("if (created) setcharprop #m,1;", "has no GS2 property"));
}

TEST_CASE("GS1 names, message codes and functions", "[GS1ToGS2]")
{
	CHECK(gs2("if (created) { playerx = playery + 1; timeout = 0.05; }") ==
		"function onCreated() {\n\tplayer.x = (player.y + 1);\n\tsetTimer(0.05);\n}\n");
	CHECK(refused("if (created) timeout += 1;", "only `timeout = value`"));
	CHECK(gs2("if (created) message You have #v(playerrupees) rupees, #n;") ==
		"function onCreated() {\n\tmessage((\"You have \" @ (player.rupees) @ \" rupees, \" @ player.nick));\n}\n");
	CHECK(gs2("if (playerchats && strequals(#c,open)) say2 #T(#t(0))#bnext;") ==
		"function onPlayerChats() {\n\tif (strequals(player.chat, \"open\")) {\n\t\tsay2(((tokens[0]).trim() @ \"#bnext\"));\n\t}\n}\n");
	CHECK(gs2("if (created) { a = strlen(#a); b = playerhp; c = tokenscount; }") ==
		"function onCreated() {\n\ta = (player.account).length();\n\tb = player.hearts;\n\tc = tokens.size();\n}\n");
	CHECK(gs2("if (created) with (getplayer(Bob)) { hide; i = 1; }") ==
		"function onCreated() {\n\twith (findplayer(\"Bob\")) {\n\t\thide();\n\t\tthiso.i = 1;\n\t}\n}\n");


	// GS2 pushes a list's elements right to left, so values that call something are read first, in GS1's order.
	CHECK(gs2("if (created) a = {random(1,2), 5, random(3,4)};") ==
		"function onCreated() {\n\ttemp.__gs1list0 = random(1, 2);\n\ttemp.__gs1list1 = random(3, 4);\n\ta = {temp.__gs1list0, 5, temp.__gs1list1};\n}\n");

	// `in`, loops and element updates.
	CHECK(gs2("if (created) { for (i = 0; i < 3; i++) b[i] += 2; if (x in |1,5|) show; if (y in {1,2}) hide; }") ==
		"function onCreated() {\n\tfor (i = 0; (i < 3); i++) {\n\t\tb[i] = b[i] + (2);\n\t}\n\tif ((x in <1, 5>)) {\n\t\tshow();\n\t}\n\tif ((y == 1 || y == 2)) {\n\t\thide();\n\t}\n}\n");
}

TEST_CASE("GS1 with no faithful GS2 form is refused", "[GS1ToGS2]")
{
	CHECK(refused("if (created) putnpc a.png,x.txt,1,2;", "has no GS2 equivalent"));
	CHECK(refused("if (created) a = b ? 1 : 2;", "`?:` is not lowered"));
	CHECK(refused("if (created) show; else hide;", "with else"));
	CHECK(refused("if (created) show;\nthis.a = 1;\n", "after an event test"));
	CHECK(refused("if (created && timeout) show;", "too complex"));
	CHECK(refused("if (created) a = timeout;", "event flag"));
	CHECK(refused("if (created) { set gotkey; gotkey = 1; }", "player flag"));
	CHECK(refused("if (playerchats) hide;\nif (playerchats && playersays(hi)) show;", "playersays"));
	CHECK(refused("if (created) with (getplayer(Bob)) { playerx = 1; }", "inside a GS1 `with`"));
	CHECK(refused("if (created) message #s(name);", "#s(name)"));
}

TEST_CASE("GS1 client scripts compile to the gs2-parser bytecode of their GS2 form", "[GS1ToGS2]")
{
	const auto compiled = gs1::compileClientScript(Door);
	REQUIRE(compiled.ok());
	CHECK(compiled.errors.empty());
	CHECK(compiled.bytecode == compileGS2(gs2(Door)));

	// The NPC-server's \xa7 line separators.
	std::string mangled{Door};
	std::ranges::replace(mangled, '\n', '\xa7');
	CHECK(gs1::compileClientScript(mangled).bytecode == compiled.bytecode);

	// A script marked GS2 is compiled as written.
	const auto gs2Script = "//#GS2\nfunction onCreated() { setimg(\"door.png\"); }\n"s;
	CHECK(gs1::compileClientScript(gs2Script).bytecode == compileGS2(gs2Script));

	// A script that cannot be lowered has no bytecode, and says why.
	// The stock -gr_movement weapon (bin/servers/default/weapons) now compiles: its gr.x / gr.y strings are flags.
	const auto grMovement = gs1::compileClientScript(R"(//#CLIENTSIDE
if (playerenters || timeout) {
  if (!gr.off) {
    if (!gr.x.off) setstring gr.x,#v(int((playerx+0.25)*2)/2);
    if (!gr.y.off) setstring gr.y,#v(int((playery+0.25)*2)/2);
  }
  timeout=0.05;
}
)");
	for (const auto& e : grMovement.errors)
		UNSCOPED_INFO(e);
	CHECK(grMovement.ok());
	CHECK(compileGS2(gs2("if (created) { set gotkey; unset gotkey; setstring quest,done; }")).size() > 0);

	const auto refusedScript = gs1::compileClientScript("if (created) putnpc a.png,x.txt,1,2;");
	CHECK_FALSE(refusedScript.ok());
	CHECK_FALSE(refusedScript.errors.empty());

	CHECK_FALSE(gs1::compileClientScript("  \n").ok());
}
