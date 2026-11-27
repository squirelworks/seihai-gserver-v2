// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: AddonHost on a Server. The code here is what the Graal 3D add-on
// used to do with Server and Player directly (Graal3DService.cpp before
// 2026-11-27); it moved here unchanged so the add-on can be built as a module.

#include <addons/ServerAddonHost.h>

#include <algorithm>
#include <any>
#include <cctype>
#include <ranges>

#include <IEnums.h>

#include <Server.h>
#include <level/Level.h>
#include <npcserver/NPCServer.h>
#include <object/Player.h>
#include <object/Weapon.h>
#include <player/PlayerClient.h>
#include <scripting/ScriptContainers.h>
#include <utilities/Log.h>

namespace preagonal
{
	std::optional<std::string> ServerAddonHost::settingString(std::string_view key) const
	{
		return m_server.getSettings().get<std::string>(key);
	}

	std::optional<bool> ServerAddonHost::settingBool(std::string_view key) const
	{
		return m_server.getSettings().get<bool>(key);
	}

	std::optional<int> ServerAddonHost::settingInt(std::string_view key) const
	{
		return m_server.getSettings().get<int>(key);
	}

	std::optional<float> ServerAddonHost::settingFloat(std::string_view key) const
	{
		return m_server.getSettings().get<float>(key);
	}

	void ServerAddonHost::logLine(std::string_view text)
	{
		log::printLine(log::server, "{}", text);
	}

	std::optional<std::string> ServerAddonHost::findFile(std::string_view name) const
	{
		const auto* info = m_server.getFileSystem().infoi(fs::FileCategory::ALL, std::filesystem::path{name}.filename());
		if (info == nullptr)
			return std::nullopt;
		return info->file.string();
	}

	std::vector<PlayerID> ServerAddonHost::playerIds() const
	{
		std::vector<PlayerID> ids;
		for (const auto& [id, p] : m_server.getPlayerList())
			if (p != nullptr)
				ids.push_back(id);
		return ids;
	}

	std::optional<AddonPlayerInfo> ServerAddonHost::player(PlayerID id) const
	{
		const auto p = m_server.getPlayer(id);
		if (p == nullptr)
			return std::nullopt;
		AddonPlayerInfo info;
		info.id = id;
		info.account = p->account.name;
		info.nickname = p->account.character.nickName;
		info.chat = p->account.character.chatMessage;
		info.versionToken = p->getVersionStr();
		info.versionId = p->getVersion();
		if (const auto* socket = p->getSocket(); socket != nullptr)
			if (const char* ip = socket->getRemoteIp(); ip != nullptr)
				info.ip = ip;
		if (const auto level = p->getLevel(); level != nullptr)
			info.level = std::string(level->levelName);
		const auto tile = p->getTilePosition();
		info.tileX = static_cast<float>(tile.x());
		info.tileY = static_cast<float>(tile.y());
		info.isClient = p->isClient();
		info.isLoaded = p->isLoaded();
		info.isGuest = p->isGuest();
		return info;
	}

	std::optional<PlayerID> ServerAddonHost::findClient(std::string_view account) const
	{
		if (const auto p = m_server.getPlayer(CString(std::string(account)), PLTYPE_ANYCLIENT); p != nullptr)
			return p->getId();
		return std::nullopt;
	}

	// PlayerClient::setPosition sends the new x2/y2 to the level, so 2D clients
	// there see the player move.
	void ServerAddonHost::setTilePosition(PlayerID id, float tileX, float tileY)
	{
		const auto p = m_server.getPlayer(id);
		if (p == nullptr)
			return;
		auto tile = p->getTilePosition();
		tile.x() = tileX;
		tile.y() = tileY;
		p->setPosition(toPixelPosition(tile));
	}

	void ServerAddonHost::setChat(PlayerID id, std::string_view text)
	{
		if (const auto p = m_server.getPlayer(id))
			p->setChat(CString(std::string(text)));
	}

	void ServerAddonHost::playerFlagSet(PlayerID id, std::string_view flag)
	{
		const auto p = m_server.getPlayer<PlayerClient>(id);
		if (p == nullptr)
			return;
		CString packet = CString() >> (char)PLI_FLAGSET << std::string(flag);
		packet.setRead(1);
		p->msgPLI_FLAGSET(packet);
	}

	void ServerAddonHost::playerTriggerAction(PlayerID id, std::string_view action)
	{
		const auto p = m_server.getPlayer<PlayerClient>(id);
		if (p == nullptr)
			return;
		CString packet = CString() >> (char)PLI_TRIGGERACTION;
		packet.writeGInt(0);
		packet.writeGChar(0);
		packet.writeGChar(0);
		packet << std::string(action);
		packet.setRead(1);
		p->msgPLI_TRIGGERACTION(packet);
	}

	void ServerAddonHost::addWeapon(PlayerID id, std::string_view weapon)
	{
		if (const auto p = m_server.getPlayer<PlayerClient>(id))
			p->addWeapon(weapon);
	}

	void ServerAddonHost::sendFile(PlayerID id, std::string_view name)
	{
		if (const auto p = m_server.getPlayer(id))
			p->sendFile(std::filesystem::path{name});
	}

	void ServerAddonHost::setAddonLinked(PlayerID id, bool linked)
	{
		if (const auto p = m_server.getPlayer(id))
			p->setAddonLinked(linked);
	}

	std::vector<std::string> ServerAddonHost::loginFlags(PlayerID id) const
	{
		std::vector<std::string> lines;
		const auto p = m_server.getPlayer(id);
		if (p == nullptr)
			return lines;
		for (const auto& [flag, value] : p->account.variables.store | variables::serializable)
		{
			if (auto serialized = p->account.variables.serializeModern(flag); serialized.has_value())
				lines.push_back(*serialized);
		}
		const bool npcServer = m_server.hasNPCServer();
		for (const auto& flag : m_server.Scripting.variables.store | std::views::keys)
		{
			if (npcServer && !flag.starts_with("serverr.")) continue;
			if (auto serialized = m_server.Scripting.variables.serializeModern(flag); serialized.has_value())
				lines.push_back(*serialized);
		}
		return lines;
	}

	std::string ServerAddonHost::compileClientScript(std::string_view name, std::string_view gs2)
	{
		const auto npcServer = m_server.getNPCServer();
		if (npcServer == nullptr)
			return {};
		const auto compiled = npcServer->scripting.getCompiledClientScript(name, gs2);
		if (compiled == nullptr || compiled->script == nullptr || !compiled->script->has_value())
			return {};
		const auto* bytecode = std::any_cast<ScriptByteCode>(compiled->script.get());
		if (bytecode == nullptr)
			return {};
		return std::string(reinterpret_cast<const char*>(bytecode->data()), bytecode->size());
	}

	std::optional<std::string> ServerAddonHost::weaponClientScript(std::string_view name) const
	{
		const auto w = m_server.getWeapon(name);
		if (w == nullptr)
			return std::nullopt;
		if (const auto& bytecode = w->getScript().getClientSideByteCode(); !bytecode.empty())
			return std::string(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
		return std::string(w->getScript().getClientSideScript());
	}

} // namespace preagonal
