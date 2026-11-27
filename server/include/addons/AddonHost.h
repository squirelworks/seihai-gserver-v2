// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: what an add-on may ask of the core. The Graal 3D add-on reaches the
// server only through this interface (and the hooks in ServerAddon.h), so it can
// be built as a separate module (docs/addons.md, "Add-on modules") that does not
// see Server, Player or any other core class. Only standard types cross it.
//
// Changing anything here, in ServerAddon.h or in AddonModule.h changes the add-on
// API: bump kAddonApiVersion in AddonModule.h.

#ifndef SEIHAI_ADDONS_ADDONHOST_H
#define SEIHAI_ADDONS_ADDONHOST_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <utilities/CommonTypes.h>

namespace preagonal
{
	/// A snapshot of one player, as an add-on sees it.
	struct AddonPlayerInfo
	{
		PlayerID id = 0;
		std::string account;
		std::string nickname;
		std::string chat;
		/// The client's eight-character version token (G3D0511C, ...).
		std::string versionToken;
		/// gs2lib's version id for that token (CLVER_*).
		int versionId = 0;
		/// The remote address of its TCP socket, as the socket gives it.
		std::string ip;
		/// The level it is in; empty when it is in none.
		std::string level;
		/// Its position in tiles.
		float tileX = 0.0f;
		float tileY = 0.0f;
		bool isClient = false;
		bool isLoaded = false;
		bool isGuest = false;
	};

	/// The core's side of the add-on API. Every call is made on the server's own
	/// thread, from a hook (ServerAddon) or from code a hook runs.
	class AddonHost
	{
	public:
		virtual ~AddonHost() = default;

		// ---- Settings (config/serveroptions.txt) ----
		[[nodiscard]] virtual std::optional<std::string> settingString(std::string_view key) const = 0;
		[[nodiscard]] virtual std::optional<bool> settingBool(std::string_view key) const = 0;
		[[nodiscard]] virtual std::optional<int> settingInt(std::string_view key) const = 0;
		[[nodiscard]] virtual std::optional<float> settingFloat(std::string_view key) const = 0;

		// ---- Log ----
		/// One line in the server log (and console).
		virtual void logLine(std::string_view text) = 0;

		// ---- Files ----
		/// The server's file of that name in any served folder, found
		/// case-insensitively; nullopt if it has none.
		[[nodiscard]] virtual std::optional<std::string> findFile(std::string_view name) const = 0;

		// ---- Players ----
		[[nodiscard]] virtual std::vector<PlayerID> playerIds() const = 0;
		[[nodiscard]] virtual std::optional<AddonPlayerInfo> player(PlayerID id) const = 0;
		/// The client player logged in with that account name, if any.
		[[nodiscard]] virtual std::optional<PlayerID> findClient(std::string_view account) const = 0;

		/// Moves the player's TCP position (tiles); the level is told, as for a
		/// move of its own.
		virtual void setTilePosition(PlayerID id, float tileX, float tileY) = 0;
		/// Sets its chat prop, which the 2D clients in its level see.
		virtual void setChat(PlayerID id, std::string_view text) = 0;
		/// A flag line from the player (PLI_FLAGSET: "name=value").
		virtual void playerFlagSet(PlayerID id, std::string_view flag) = 0;
		/// A serverside triggeraction from the player ("serverside,<params>").
		virtual void playerTriggerAction(PlayerID id, std::string_view action) = 0;
		/// Gives the player a weapon by name.
		virtual void addWeapon(PlayerID id, std::string_view weapon) = 0;
		/// Sends the player a file over TCP.
		virtual void sendFile(PlayerID id, std::string_view name) = 0;
		/// Marks the player as linked to an add-on: its packets are then passed to
		/// ServerAddon::onPacketToPlayer.
		virtual void setAddonLinked(PlayerID id, bool linked) = 0;

		// ---- Flags and scripts ----
		/// The flags the TCP login sends the player, serialized (name=value):
		/// its own, then the server's (only serverr.* with an NPC-server).
		[[nodiscard]] virtual std::vector<std::string> loginFlags(PlayerID id) const = 0;
		/// Clientside GS2 compiled to bytecode by the NPC-server; empty without
		/// one or when it does not compile.
		[[nodiscard]] virtual std::string compileClientScript(std::string_view name, std::string_view gs2) = 0;
		/// A server weapon's clientside part: its bytecode if it has one, else its
		/// source; nullopt if the server has no weapon of that name.
		[[nodiscard]] virtual std::optional<std::string> weaponClientScript(std::string_view name) const = 0;
	};

} // namespace preagonal

#endif
