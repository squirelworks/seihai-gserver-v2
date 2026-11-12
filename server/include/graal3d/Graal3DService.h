// Modified by the Seihai project, 2026-11-11, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: the gserver's Graal 3D side. A Graal 3D server is two servers on one
// host: this gserver on TCP port P, and a Torque UDP server on port P + 1, which the
// 3.153 to 5.x clients open when the server's listed name starts with "3 ". This
// service is that UDP server. It accepts a 3D connection only for an account that
// is already logged in over TCP from the same address, keeps the two together (the
// 3D link closes with the TCP session), drives the mission start phases, and
// bridges Graal's BigData text channel to the player:
//
// - client "events" lines: `s name=value` (flag or client variable), `w weapon`
//   (getweapon), `t weapon,name,params` (a serverside triggeraction);
// - server to client: the player's flags at link time, then every PLO_FLAGSET and
//   PLO_FLAGDEL the player is sent, as `set` / `unset` texts (a client in 3D mode
//   ignores the TCP flag packets).
//
// The world (4.20r6 and 5.x): every client gets the objects of the world file as
// ghosts, plus one GameObject per linked player named G3DPlayer_<account>. A
// player's own object becomes its control object, which takes a 5.x client to
// login step 5. Each client reports its position in every packet; the service
// moves that player's object on everyone else's screen, and passes on its chat.
//
// Off unless the server option `graal3d` is true.

#ifndef SEIHAI_GRAAL3D_GRAAL3DSERVICE_H
#define SEIHAI_GRAAL3D_GRAAL3DSERVICE_H

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>

#include <CSocket.h>
#include <CString.h>

#include <graal3d/Graal3DConnection.h>
#include <graal3d/Graal3DObjects.h>
#include <utilities/CommonTypes.h>

namespace preagonal
{
	class Server;
	class Player;
}

namespace graal3d
{
	using preagonal::PlayerID;

	class Service
	{
	public:
		explicit Service(preagonal::Server& server);
		~Service();
		Service(const Service&) = delete;
		Service& operator=(const Service&) = delete;

		// Opens the UDP port. `interfaceIp` empty means every interface.
		bool start(std::string_view interfaceIp, uint16_t port);

		// The objects every client is sent. `packetStrings`: see TorqueBitStream.h
		// (the option graal3dstringprefix). Call before start().
		void setWorld(World world, bool packetStrings);
		[[nodiscard]] uint16_t port() const { return m_port; }

		// Reads waiting datagrams and runs every connection. Called each server frame.
		void update();

		// The TCP session of `player` is over: close its 3D link.
		void onPlayerLeft(PlayerID player);

		// A TCP packet is about to go to a player who has a 3D link.
		void mirrorPacket(PlayerID player, const CString& packet);

		[[nodiscard]] size_t connectionCount() const { return m_sessions.size(); }

	private:
		struct Endpoint
		{
			uint32_t ip = 0;   // network byte order
			uint16_t port = 0; // network byte order
			auto operator<=>(const Endpoint&) const = default;
		};

		struct Session
		{
			Endpoint endpoint;
			PlayerID player;
			std::unique_ptr<Connection> connection;
		};

		void handleDatagram(const Endpoint& from, std::string_view data, uint64_t nowMs);
		void handleConnectRequest(const Endpoint& from, std::string_view data, uint64_t nowMs);
		// The TCP client a 3D connect request belongs to: the named account,
		// logged in from `ip`, or for "guest" that address's guest client.
		std::shared_ptr<preagonal::Player> findTcpPlayer(const std::string& requested, const std::string& ip);
		void handleText(PlayerID player, const TextMessage& message);
		void handleClientState(PlayerID player, const ClientState& state);
		void ghostWorld(PlayerID player, const std::string& account, Connection& connection);
		void forEachOtherConnection(PlayerID player, const std::function<void(Connection&)>& fn);
		static ObjectKey avatarKey(PlayerID player) { return 0x10000u + player; }
		static ObjectKey cameraKey(PlayerID player) { return 0x20000u + player; }
		void sendInitialFlags(PlayerID player, Connection& connection);
		void sendTo(const Endpoint& to, std::string_view data);
		void closeSession(const Endpoint& endpoint, std::string_view reason, bool notifyClient);
		Digest digestFor(const Endpoint& from, uint32_t sequence) const;
		static std::string ipString(const Endpoint& endpoint);
		static uint64_t nowMs();

		preagonal::Server& m_server;
		SOCKET m_socket = INVALID_SOCKET;
		uint16_t m_port = 0;
		std::array<uint32_t, 4> m_secret{};
		std::map<Endpoint, Session> m_sessions;
		std::map<PlayerID, Endpoint> m_byPlayer;

		World m_world;
		bool m_packetStrings = true;
		std::map<PlayerID, GhostObject> m_avatars;
		std::set<std::string> m_unmatched; // "ip,account" already logged as unmatched
	};

} // namespace graal3d

#endif
