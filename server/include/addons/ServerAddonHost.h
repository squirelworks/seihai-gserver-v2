// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the core's implementation of AddonHost, on a Server.

#ifndef SEIHAI_ADDONS_SERVERADDONHOST_H
#define SEIHAI_ADDONS_SERVERADDONHOST_H

#include <addons/AddonHost.h>

namespace preagonal
{
	class Server;

	class ServerAddonHost final : public AddonHost
	{
	public:
		explicit ServerAddonHost(Server& server) : m_server(server) {}

		[[nodiscard]] std::optional<std::string> settingString(std::string_view key) const override;
		[[nodiscard]] std::optional<bool> settingBool(std::string_view key) const override;
		[[nodiscard]] std::optional<int> settingInt(std::string_view key) const override;
		[[nodiscard]] std::optional<float> settingFloat(std::string_view key) const override;

		void logLine(std::string_view text) override;

		[[nodiscard]] std::optional<std::string> findFile(std::string_view name) const override;

		[[nodiscard]] std::vector<PlayerID> playerIds() const override;
		[[nodiscard]] std::optional<AddonPlayerInfo> player(PlayerID id) const override;
		[[nodiscard]] std::optional<PlayerID> findClient(std::string_view account) const override;
		void setTilePosition(PlayerID id, float tileX, float tileY) override;
		void setChat(PlayerID id, std::string_view text) override;
		void playerFlagSet(PlayerID id, std::string_view flag) override;
		void playerTriggerAction(PlayerID id, std::string_view action) override;
		void addWeapon(PlayerID id, std::string_view weapon) override;
		void sendFile(PlayerID id, std::string_view name) override;
		void setAddonLinked(PlayerID id, bool linked) override;

		[[nodiscard]] std::vector<std::string> loginFlags(PlayerID id) const override;
		[[nodiscard]] std::string compileClientScript(std::string_view name, std::string_view gs2) override;
		[[nodiscard]] std::optional<std::string> weaponClientScript(std::string_view name) const override;

	private:
		Server& m_server;
	};

} // namespace preagonal

#endif
