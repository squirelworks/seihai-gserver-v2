// Modified by the Seihai project, 2026-11-22: see CHANGES-SEIHAI.md.
#include <catch2/catch_all.hpp>

#include <cstdint>
#include <memory>

#include <BabyDI.h>
#include <IEnums.h>
#include <Server.h>
#include <level/LevelItem.h>
#include <player/PlayerClient.h>
#include <player/PlayerProps.h>
#include <utilities/Log.h>
#include <utilities/PropertySerializers.h>

using namespace preagonal;

namespace
{
	struct HeartItemClient : PlayerClient
	{
		using PlayerClient::PlayerClient;
		using PlayerClient::addItem;
	};

	struct HeartItemFixture
	{
		HeartItemFixture()
		{
			log::networkdump.disabled = true;
			log::npc.disabled = true;
			log::rc.disabled = true;
			log::script.disabled = true;
			log::server.disabled = true;

			BabyDI_RELEASE(Server);
			server = BabyDI_PROVIDE(Server, new Server("heart-item"));
			server->prepareSettings();
			server->getSettings().set("heartlimit", static_cast<uint8_t>(3));

			// setProp requires shared ownership. This fixture has no socket,
			// login, server registration or listener.
			client = std::make_shared<HeartItemClient>(nullptr, 0);
			client->setType(PLTYPE_CLIENT2);
			client->setLoaded(false);
			client->account.loadOnly = true;
			client->account.maxHitpoints = 3;
			client->account.character.hitpointsInHalves = 2;
			client->account.character.ap = 100;
			client->account.character.bombs = 7;
		}

		~HeartItemFixture()
		{
			// Destroy the player before releasing the Server it references.
			client.reset();
			BabyDI_RELEASE(Server);
		}

		Server* server = nullptr;
		std::shared_ptr<HeartItemClient> client;
	};
}

TEST_CASE_METHOD(HeartItemFixture, "HEART rollback restores half-hearts without changing bombs", "[heart-item][serveroptions]")
{
	REQUIRE(server->cached.maxHeartLimit.getValue() == 3);
	REQUIRE_FALSE(server->hasNPCServer());
	const auto result = client->addItem(LevelItemType::HEART, props::SetBy::SERVER);
	CHECK(result.propId == static_cast<uint8_t>(PlayerProp::HALFHEARTS));
	CHECK_FALSE(result.resultFlags.test(props::SetResults::wasInvalid));
	CHECK(result.resultFlags.test(props::SetResults::sendToSource));
	CHECK(client->account.character.hitpointsInHalves == 4);
	CHECK(client->account.character.bombs == 7);
	CHECK(client->account.maxHitpoints == 3);
}

TEST_CASE_METHOD(HeartItemFixture, "HEART rollback respects the configured heart ceiling", "[heart-item][serveroptions]")
{
	client->account.character.hitpointsInHalves = 5;
	const auto result = client->addItem(LevelItemType::HEART, props::SetBy::SERVER);
	CHECK(result.propId == static_cast<uint8_t>(PlayerProp::HALFHEARTS));
	CHECK_FALSE(result.resultFlags.test(props::SetResults::wasInvalid));
	CHECK(client->account.character.hitpointsInHalves == 6);
	CHECK(client->account.character.bombs == 7);
	CHECK(client->account.maxHitpoints == 3);
}

TEST_CASE_METHOD(HeartItemFixture, "HEART rollback retains the existing low-AP healing restriction", "[heart-item][serveroptions]")
{
	client->account.character.ap = 39;
	const auto result = client->addItem(LevelItemType::HEART, props::SetBy::SERVER);
	CHECK(result.propId == static_cast<uint8_t>(PlayerProp::HALFHEARTS));
	CHECK(result.resultFlags.test(props::SetResults::wasInvalid));
	CHECK(client->account.character.hitpointsInHalves == 2);
	CHECK(client->account.character.bombs == 7);
	CHECK(client->account.character.ap == 39);
}

TEST_CASE_METHOD(HeartItemFixture, "BOMBS rollback still restores bombs without changing half-hearts", "[heart-item][serveroptions]")
{
	const auto result = client->addItem(LevelItemType::BOMBS, props::SetBy::SERVER);
	CHECK(result.propId == static_cast<uint8_t>(PlayerProp::BOMBS));
	CHECK_FALSE(result.resultFlags.test(props::SetResults::wasInvalid));
	CHECK(client->account.character.bombs == 12);
	CHECK(client->account.character.hitpointsInHalves == 2);
}
