// Seihai project, 2026-11-25, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the add-on hook surface. An add-on is an optional subsystem (the
// Graal 3D side is the first) that is compiled in with a CMake option, switched
// on by a server option, and reached by the core only through the hooks below.
// With no add-on compiled in, or none switched on, every hook is a no-op and the
// server behaves exactly as without this file. See docs/addons.md.
//
// Only standard types cross these hooks and AddonHost.h, so an add-on can also be
// built as a separate module and loaded at start (addons/AddonModule.h). Any
// change here changes the add-on API: bump kAddonApiVersion in AddonModule.h.

#ifndef SEIHAI_ADDONS_SERVERADDON_H
#define SEIHAI_ADDONS_SERVERADDON_H

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include <utilities/CommonTypes.h>

namespace preagonal
{
	class Server;
	class Player;

	/// What an add-on needs the core to do differently for one client. The
	/// defaults are the core's own behaviour.
	struct AddonClientNeeds
	{
		/// A missing file named in a file-checksum request (PLI_UPDATEFILE) gets
		/// PLO_FILESENDFAILED instead of no answer.
		bool answerMissingChecksumFiles = false;

		/// PLO_SERVERLISTCONNECTED is sent at login even when no listserver is
		/// connected.
		bool alwaysServerListConnected = false;

		/// Extensions tried, in order, when the client asks for a file with no
		/// extension that the server does not have under the bare name.
		std::span<const char* const> bareNameExtensions;

		/// File names (lower-cased, no folder) the client is answered as if the
		/// server did not have them: files the add-on found would crash it.
		const std::set<std::string>* refusedFiles = nullptr;

		/// Files sent to the client unasked at login, after the gpaks option's
		/// packages: files its add-on side reads from its cache but never asks for.
		const std::vector<std::string>* loginFiles = nullptr;

		AddonClientNeeds& operator|=(const AddonClientNeeds& o)
		{
			answerMissingChecksumFiles |= o.answerMissingChecksumFiles;
			alwaysServerListConnected |= o.alwaysServerListConnected;
			if (bareNameExtensions.empty())
				bareNameExtensions = o.bareNameExtensions;
			if (refusedFiles == nullptr)
				refusedFiles = o.refusedFiles;
			if (loginFiles == nullptr)
				loginFiles = o.loginFiles;
			return *this;
		}
	};

	/// One add-on. Every hook has a default that leaves the core's behaviour as is.
	class ServerAddon
	{
	public:
		virtual ~ServerAddon() = default;

		/// For logs.
		[[nodiscard]] virtual std::string_view name() const = 0;

		/// Called once from Server::init, after the game port is listening.
		/// `interfaceIp` is the `serverinterface` option (empty: every interface).
		/// Return false to drop the add-on for this run.
		virtual bool start(std::string_view interfaceIp) = 0;

		/// Called once per server frame, after the sockets are polled.
		virtual void update() {}

		/// A player's TCP session is ending (Server::deletePlayer).
		virtual void onPlayerLeft(PlayerID) {}

		/// A packet is about to be queued for a player whose `addonLinked` flag is
		/// set (Player::setAddonLinked). Called from Player::sendPacket. `packet`
		/// is the packet's bytes, its id first.
		virtual void onPacketToPlayer(PlayerID, std::string_view /*packet*/) {}

		/// The HQ level the server registers at with the listserver, or nullopt to
		/// leave it to `hq_level`. `onlystaff` still wins (level 0).
		[[nodiscard]] virtual std::optional<int> listHqLevel() const { return std::nullopt; }

		/// What this add-on needs for a game client with the given version id.
		[[nodiscard]] virtual AddonClientNeeds clientNeeds(int /*versionId*/) const { return {}; }
	};

	using ServerAddonPtr = std::unique_ptr<ServerAddon>;

} // namespace preagonal

#endif
