// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include <any>
#include <atomic>
#include <cstdint>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include <CSocket.h>

#include <BabyDI.h>
#include <Server.h>
#include <npcserver/NPCServer.h>
#include <scripting/IScriptEngine.h>
#include <scripting/Script.h>
#include <scripting/ScriptSystem.h>
#include <scripting/ScriptTypes.h>
#include <scripting/gs1/GS1Variables.h>
#include <scripting/gs1/ScriptEngineGS1.h>
#include <utilities/CommonTypes.h>
#include <utilities/Extents.h>
#include <utilities/Log.h>

using namespace preagonal;
using namespace std::string_literals;
using namespace std::string_view_literals;

std::atomic_bool shutdownProgram{false};

struct ServerFixture
{
	ServerFixture()
	{
		log::networkdump.disabled = true;
		log::npc.disabled = true;
		log::rc.disabled = true;
		log::script.disabled = true;
		log::server.disabled = true;

		BabyDI_RELEASE(Server);
		server = BabyDI_PROVIDE(Server, new Server("test"));
		server->getSettings().set("serverside", true);
		server->loadNPCServer();

		// Link the scripting engine.
		npcServer = server->getNPCServer();
		engine = std::dynamic_pointer_cast<gs1::ScriptEngineGS1>(npcServer->scripting.getScriptEngine(gs1::ScriptEngineGS1::EngineName));

		// Configure NPC-Player.
		const auto player = std::dynamic_pointer_cast<Player>(server->getNPCServer()->getPlayerNPCServer());
		player->constructScriptParameters();

		// Configure Test NPC.
		const auto npc = npcServer->addNPC("door.png"sv, ""sv, nullptr, TilePosition{20.0f, 30.0f}, NPCTYPE_OBJECT);
		npc->name = "Test";
		testNPC = npc->id;

		// Configure Test Clients.
		client = std::make_shared<PlayerClient>(new CSocket(), server->getPlayerIdGenerator().getAvailableId());
		client->setType(PLTYPE_CLIENT2);
		server->addPlayer(client, client->getId());
		npcServer->playerLogin(client);
	}

	NPCID testNPC = NPCID_GEN_DATABASE_LOCALN;
	Server* server = nullptr;
	std::shared_ptr<NPCServer> npcServer;
	std::shared_ptr<gs1::ScriptEngineGS1> engine;
	std::shared_ptr<PlayerClient> client;
};

////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Script client bytecode", "[Scripting][Script]")
{
	Script script;
	auto context = std::make_shared<ScriptExecutionContext>();
	context->script = std::make_shared<std::any>(std::vector<uint8_t>{1, 2, 3});
	script.setClientCompiledScript(context);
	REQUIRE(script.getClientByteCode() == std::vector<uint8_t>{1, 2, 3});

	context = std::make_shared<ScriptExecutionContext>();
	context->script = nullptr;
	script.setClientCompiledScript(context);
	REQUIRE(script.getClientByteCode().empty());

	context = std::make_shared<ScriptExecutionContext>();
	context->script = std::make_shared<std::any>(std::string{"bytecode"});
	script.setClientCompiledScript(context);
	REQUIRE(script.getClientByteCode().empty());
}

TEST_CASE("Script minify with block comments", "[Scripting][Script]")
{
	// A comment that is closed drops only the comment.
	const auto closed = Script::minify("//#CLIENTSIDE\nmessage before;\n/* a\nb */\nmessage after;\n");
	CHECK(closed.find("message before;") != std::string::npos);
	CHECK(closed.find("message after;") != std::string::npos);
	CHECK(closed.find("b */") == std::string::npos);

	// A comment that is never closed runs to the end of the script. This used to
	// remove_prefix(npos) and loop forever.
	const auto open = Script::minify("//#CLIENTSIDE\nmessage before;\n/* never closed\nmessage gone;\n");
	CHECK(open.find("message before;") != std::string::npos);
	CHECK(open.find("message gone;") == std::string::npos);
	CHECK(open.find("never closed") == std::string::npos);

	// On the last line, with no newline after it.
	const auto last = Script::minify("//#CLIENTSIDE\nmessage before; /* trailing");
	CHECK(last.find("message before;") != std::string::npos);
	CHECK(last.find("trailing") == std::string::npos);
}

TEST_CASE_METHOD(ServerFixture, "Script joins", "[Scripting][IScriptEngine][GS1]")
{
	ScriptEvent created{.type = ScriptEventType::CREATED, .initiator = source::FromPlayer(NPCServerPlayerID)};
	const auto player = npcServer->getPlayerNPCServer();
	player->account.character = Character{};
	player->account.character.nickName = "NPC-Server (Server)";

	npcServer->addClass("TestClass"sv, "if (created) { this.test = 42; }"sv);
	server->getSettings().set("serverside", false);

	SECTION("clientside join hack")
	{
		constexpr std::string_view script = R"(
			//#CLIENTSIDE
			join TestClass;
		)";

		auto npc = server->getNPC(testNPC);
		REQUIRE(npc != nullptr);

		npc->setScript(script);
		REQUIRE_FALSE(npc->getScript().getClientSide().empty());
		CHECK(npc->getScript().getClientSide() == "//#CLIENTSIDE\xA7;\xA7if (created) { this.test = 42; }"s);
	}

	SECTION("clientside join hack ignores sign")
	{
		constexpr std::string_view script = R"(
			//#CLIENTSIDE
			say2 join TestClass#b
			join TestClass;
			if (created) { say2 join TestClass; }
		)";

		auto npc = server->getNPC(testNPC);
		REQUIRE(npc != nullptr);

		npc->setScript(script);
		REQUIRE_FALSE(npc->getScript().getClientSide().empty());
		CHECK(npc->getScript().getClientSide() == "//#CLIENTSIDE\xA7say2 join TestClass#b\xA7join TestClass;\xA7if (created) { say2 join TestClass; }"s);
	}
}
