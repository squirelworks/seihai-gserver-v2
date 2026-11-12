// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: regression tests for the built-in NPC-server's GS1 fixes from the
// 2026-10-31 lab note (docs/lab/2026-10-31-npcserver-gs1.md) and issue #33.
#include <catch2/catch_all.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <CSocket.h>

#include <BabyDI.h>
#include <Server.h>
#include <npcserver/NPCServer.h>
#include <object/NPC.h>
#include <object/Player.h>
#include <player/PlayerClient.h>
#include <scripting/IScriptEngine.h>
#include <scripting/ScriptSystem.h>
#include <scripting/gs1/GS1Visitor.h>
#include <scripting/gs1/ScriptEngineGS1.h>
#include <utilities/Log.h>
#include <utilities/manager/ITranslationManager.h>
#include <utilities/manager/TranslationManagerClassic.h>

using namespace preagonal;
using namespace std::string_literals;
using namespace std::string_view_literals;

std::atomic_bool shutdownProgram{false};

namespace
{
	struct GS1Fixture
	{
		GS1Fixture()
		{
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;

			BabyDI_RELEASE(ITranslationManager);
			BabyDI_PROVIDE(ITranslationManager, new TranslationManagerClassic());

			BabyDI_RELEASE(Server);
			server = BabyDI_PROVIDE(Server, new Server("test"));
			server->getSettings().set("serverside", true);
			server->loadNPCServer();

			npcServer = server->getNPCServer();
			engine = std::dynamic_pointer_cast<gs1::ScriptEngineGS1>(npcServer->scripting.getScriptEngine(gs1::ScriptEngineGS1::EngineName));

			const auto player = std::dynamic_pointer_cast<Player>(npcServer->getPlayerNPCServer());
			player->constructScriptParameters();

			const auto npc = npcServer->addNPC("door.png"sv, ""sv, nullptr, TilePosition{20.0f, 30.0f}, NPCTYPE_OBJECT);
			npc->name = "Test";
			testNPC = npc->id;
		}

		/// Compiles a server-side GS1 script and runs its created event on the test NPC.
		/// Returns the error text, empty when it compiled and ran.
		std::string run(std::string_view script, PlayerID initiator = NPCServerPlayerID)
		{
			result = engine->compileScript(ScriptCompilationTarget::SERVER, "test_script", script);
			if (const auto failure = std::get_if<1>(&result); failure != nullptr)
				return failure->second.empty() ? "compile failed"s : failure->second;

			auto& context = std::get<ScriptExecutionContext>(result);
			auto wrapper = std::any_cast<gs1::GS1ScriptWrapper>(context.script.get());
			REQUIRE(wrapper != nullptr);
			wrapper->variables.defaultLifetime = variables::Lifetime::NORMAL;

			ScriptEvent created{.type = ScriptEventType::CREATED, .initiator = source::FromPlayer(initiator)};
			const auto contextPtr = std::shared_ptr<ScriptExecutionContext>(&context, [](ScriptExecutionContext*) {});
			if (!engine->execute(created, source::FromNPC(testNPC), contextPtr))
				return "execute failed"s;
			return {};
		}

		NPCPtr npc() const { return server->getNPC(testNPC); }

		NPCID testNPC = NPCID_GEN_DATABASE_LOCALN;
		Server* server = nullptr;
		std::shared_ptr<NPCServer> npcServer;
		std::shared_ptr<gs1::ScriptEngineGS1> engine;
		CompiledScriptResult result;
	};
} // namespace

TEST_CASE_METHOD(GS1Fixture, "message with a one-letter word before =", "[GS1][issue33]")
{
	for (const auto& [script, expected] : {
			 std::pair{"message P=1;"sv, "P=1"s},
			 std::pair{"message Q=2;"sv, "Q=2"s},
			 std::pair{"message A P=1;"sv, "A P=1"s},
			 std::pair{"message hp 3;"sv, "hp 3"s},
			 std::pair{"message AB=1;"sv, "AB=1"s},
			 std::pair{"message H=#v(3);"sv, "H=3"s},
			 std::pair{"if (created) { message P=1; }"sv, "P=1"s},
			 std::pair{"if (created) {message P=1;}"sv, "P=1"s},
			 std::pair{"if (created) message P=1;"sv, "P=1"s},
			 std::pair{"if (created) {\n  if (strequals(t41,t41)) message P=1;\n}"sv, "P=1"s},
			 std::pair{"if (created) {\n  if (strequals(t40,t41)) message Z=0;\n  if (strequals(t41,t41)) message P=1;\n  if (strequals(t42,t41)) message Q=2;\n}"sv, "P=1"s},
			 std::pair{"if (created) {\n  if (strequals(t41,t41)) message A P=1;\n}"sv, "A P=1"s},
			 std::pair{"if (created) {\n  message P=1;\n  this.x = 2;\n}"sv, "P=1"s},
		 })
	{
		CAPTURE(script);
		npc()->character.chatMessage.clear();
		REQUIRE(run(script).empty());
		CHECK(npc()->character.chatMessage == expected);
	}
}

TEST_CASE_METHOD(GS1Fixture, "message X= inside the full micro-test script", "[GS1][issue33]")
{
	// The lab's chat-driven script (seihai-servertools docs/lab/probe/microtests.gs1), cut to the message lines, with created for playerchats.
	constexpr std::string_view head = R"(
if (created) { showcharacter; setcharprop #n,Probe; }
if (created) {
  if (strequals(#c,t1)) message A=#a N=#n C=#c;
  if (strequals(#c,t2)) { setstring server.labx,#a; message S=#s(server.labx); }
  if (strequals(#c,t5)) { hide; message V=#v(visible); }
  if (strequals(#c,t8)) { message X=#v(playerx) Y=#v(playery) R=#v(playerrupees) H=#v(playerhearts); }
  if (strequals(#c,t11)) { setcharprop #c,npc chat set #n; }
  if (strequals(#c,t29)) { hurt 1; message hurt done; }
  if (strequals(#c,t30)) { message P=#v(playerhearts); }
  if (strequals(#c,t40)) message H=#v(playerhearts);
  if (strequals(#c,t41)) message P=1;
  if (strequals(#c,t42)) message A P=#v(playerhearts);
  if (strequals(#c,t43)) message hp #v(playerhearts);
  if (strequals(#c,t44)) message Q=2;
}
)";
	const auto player = npcServer->getPlayerNPCServer();
	for (const auto& [chat, expected] : {std::pair{"t41"s, "P=1"s}, std::pair{"t44"s, "Q=2"s}, std::pair{"t43"s, "hp 3"s}})
	{
		CAPTURE(chat);
		player->account.character.hitpointsInHalves = 6;
		player->account.character.chatMessage = chat;
		npc()->character.chatMessage.clear();
		REQUIRE(run(head).empty());
		CHECK(npc()->character.chatMessage == expected);
	}
}

TEST_CASE_METHOD(GS1Fixture, "visible tests the VISIBLE bit, so hide and show toggle", "[GS1][issue33]")
{
	// visFlags also carries CREATED after the created event, so visFlags != HIDDEN was always true and the lab's
	// blinker (if (visible) hide; else show; in each timeout) hid once and never showed again. The flag is read
	// when an event starts, so each step is its own event here.
	constexpr std::string_view blinker = "if (created) { if (visible) { this.seen = 1; hide; } else { this.seen = 0; show; } }";
	const auto& store = npc()->scripting.variables;

	REQUIRE(run(blinker).empty());
	CHECK((npc()->visFlags & PROPID(NPCVisFlags::CREATED)) != 0);
	CHECK(store.getValue<double>("seen").value_or(-1.0) == 1.0);
	CHECK((npc()->visFlags & PROPID(NPCVisFlags::VISIBLE)) == 0);

	REQUIRE(run(blinker).empty());
	CHECK(store.getValue<double>("seen").value_or(-1.0) == 0.0);
	CHECK((npc()->visFlags & PROPID(NPCVisFlags::VISIBLE)) != 0);

	REQUIRE(run(blinker).empty());
	CHECK(store.getValue<double>("seen").value_or(-1.0) == 1.0);
}

TEST_CASE_METHOD(GS1Fixture, "bare shootball is accepted", "[GS1][issue33]")
{
	// A command's argument error is written to the NPC log, so the log tells whether shootball was refused.
	const auto logPath = std::filesystem::temp_directory_path() / "seihai-npcservergs1-npclog.txt";
	log::npc.filename = logPath;
	log::npc.mirrorToCli = false;
	log::npc.disabled = false;
	log::npc.reload();
	log::npc.clear();
	const auto scriptLog = [&logPath]()
	{
		log::npc.close();
		std::ifstream in(logPath);
		std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		log::npc.reload();
		log::npc.clear();
		return text;
	};

	// The negative control: shootfireball still needs its direction.
	REQUIRE(run("if (created) { shootfireball; }").empty());
	CHECK_THAT(scriptLog(), Catch::Matchers::ContainsSubstring("invalid arguments: shootfireball"));

	REQUIRE(run("if (created) { shootball; }").empty());
	CHECK(scriptLog().find("shootball") == std::string::npos);

	REQUIRE(run("if (created) { shootball 2; }").empty());
	CHECK(scriptLog().find("shootball") == std::string::npos);

	REQUIRE(run("if (created) { shootball 1,2; }").empty());
	CHECK_THAT(scriptLog(), Catch::Matchers::ContainsSubstring("invalid arguments: shootball"));

	log::npc.disabled = true;
}

TEST_CASE_METHOD(GS1Fixture, "#a, #g and #G name the player in an NPC event, not the NPC", "[GS1][issue33]")
{
	// The NPC is the current source in its own event; the codes must use the player who started it, as #a does.
	const auto client = std::make_shared<PlayerClient>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
	client->setType(PLTYPE_CLIENT2);
	client->account.name = "labtest";
	client->setNick("Lab (Testers)", true);
	server->addPlayer(client, client->getId());
	npcServer->playerLogin(client);

	const auto error = run("if (created) { setstring this.a,#a; setstring this.g,#g; setstring this.bigg,#G; }", client->getId());
	CAPTURE(error);
	REQUIRE(error.empty());

	const auto& store = npc()->scripting.variables;
	CHECK(store.getValue<std::string>("a").value_or(""s) == "labtest"s);
	CHECK(store.getValue<std::string>("g").value_or(""s) == "Testers"s);
	CHECK(store.getValue<std::string>("bigg").value_or(""s) == "classic"s);
}


TEST_CASE("a script's hurt reaches 1.x to 2.31 clients as a hearts prop", "[GS1][issue33]")
{
	// Those clients refuse every PLO_HURTPLAYER a script's hit can send (unknown attacker id 0, or any trailing NPC id).
	CHECK(Server::hurtsByHeartsProp(CLVER_1_411));
	CHECK(Server::hurtsByHeartsProp(CLVER_2_052));
	CHECK(Server::hurtsByHeartsProp(CLVER_2_22));
	CHECK(Server::hurtsByHeartsProp(CLVER_2_31));
	CHECK_FALSE(Server::hurtsByHeartsProp(CLVER_3));
	CHECK_FALSE(Server::hurtsByHeartsProp(CLVER_6_037));
	CHECK_FALSE(Server::hurtsByHeartsProp(CLVER_UNKNOWN));
}
