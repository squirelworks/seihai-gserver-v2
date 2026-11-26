// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-19, 2026-11-25, 2026-11-26: see CHANGES-SEIHAI.md.
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
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>

#include <CSocket.h>
#include <CString.h>

#include <graal3d/Graal3DCapabilities.h>
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
		// A client build with a Torque 3D side: 3.153 to 5.x.
		static bool clientHasGraal3D(int version);

		// The objects every client is sent. `packetStrings`: see TorqueBitStream.h
		// (the option graal3dstringprefix). Call before start().
		// What to do with a world object whose .3ds fails unsafe3ds: drop it
		// (the default), only warn, or not check. Call before setWorld().
		enum class ShapeCheck
		{
			Skip,
			Warn,
			Off,
		};
		void setShapeCheck(ShapeCheck check) { m_shapeCheck = check; }

		void setWorld(World world, bool packetStrings);
		// graal3droute = sharp3d: protocol 24 clients get the Sharp3D route (the
		// world as GhostAlwaysObjectEvents, no mission phases, a Camera as the
		// control object). Call before start().
		void setSharp3DRoute(bool on) { m_sharp3d = on; }
		// graal3dtrace = <file>: append one tab-separated line for each 3D link,
		// each event and each control-object change every 3D client sends, and each
		// time a player's object is passed on to the others, for multi-client labs
		// (see bin/docs/graal3d.md). Empty: off.
		void setTrace(const std::string& path);
		// graal3dmutate = <kind>[:<seed>]: lab only, one seeded mutation per 3D link
		// (see bin/docs/graal3d.md, "Lab: mutations"). Empty or "off": none.
		void setMutation(const std::string& text);
		// Lab: graal3dterrainall = true sends the 5.x TerrainBlock, WaterBlock and
		// chunks to every GameObject-era client, whatever its capability row says.
		void setTerrainForAll(bool on) { m_terrainForAll = on; }
		// graal3dpushterrain = true: right after a 3D link, heights.info and its
		// textures go to the client over TCP unasked (5.12 fetches the textures
		// only on its second visit, so a first visit drew the terrain white).
		void setPushTerrainFiles(bool on) { m_pushTerrainFiles = on; }
		// graal3dpushgrass (default true): right after a 3D link, the textures
		// the grass replicator lines name go to the client over TCP unasked,
		// once per account (5.12 never fetches them itself).
		void setPushGrassTextures(bool on) { m_pushGrassTextures = on; }
		// Lab: graal3dgrassdelay = <ms> ghosts the grass replicators that long
		// after the rest of the world (0, the default: with the rest).
		void setGrassDelay(uint32_t ms) { m_grassDelayMs = ms; }

		// Files the start-up check found would crash a client (graal3dshapecheck
		// = skip): a 3D client asking for one is answered as if the server did
		// not have it, which it survives.
		[[nodiscard]] const std::set<std::string>& refusedFiles() const { return m_refusedFiles; }
		[[nodiscard]] uint16_t port() const { return m_port; }

		// Reads waiting datagrams and runs every connection. Called each server frame.
		void update();

		// The TCP session of `player` is over: close its 3D link.
		void onPlayerLeft(PlayerID player);

		// A TCP packet is about to go to a player who has a 3D link.
		void mirrorPacket(PlayerID player, const CString& packet);

		// A script on the player's client asked for its own object to be moved, with
		// triggerserver(<kind>, <weapon>, "g3dpos", x, y, z), which a 5.12 client in 3D
		// mode sends over the 3D text channel. Its scripts cannot reach the object,
		// so the server moves it and ghosts it to everyone, the player's own screen
		// included. `position` is "x y z". Returns false if the player has no 3D
		// link or the value is not three numbers.
		bool moveAvatar(PlayerID player, std::string_view position);

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
			// What the client's build can take (by its login token and protocol).
			const Capabilities* caps = nullptr;
			// The 2D players in this player's level, as last put on its screen.
			std::map<PlayerID, GhostObject> tcpShown;
			// graal3dgrassdelay: when the held grass objects are ghosted (0: none held).
			uint64_t grassDueMs = 0;
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
		// Puts `who`'s object on `connection` in the form its build takes: the
		// GameObject itself (protocols 21 and 24), or a stock TGE Player with the
		// world's tgeplayershape (protocol 19, when that client's own control
		// object is a Player too).
		void ghostAvatar(Connection& connection, ObjectKey key, const GhostObject& avatar) const;
		[[nodiscard]] bool tgeOthers() const;
		// A protocol 19 client moves its Player itself: pass that on to the others.
		void relayTgeMoves();
		void relayAvatar(PlayerID playerId, const GhostObject& before);
		// Moves the player's TCP position to follow its 3D object.
		void syncTcpPosition(PlayerID playerId, const std::array<float, 3>& position);
		static ObjectKey avatarKey(PlayerID player) { return 0x10000u + player; }
		static ObjectKey cameraKey(PlayerID player) { return 0x20000u + player; }
		static ObjectKey skyKey() { return 0x30000u; } // protocol 19's plain Sky, when the world has none
		static ObjectKey tcpKey(PlayerID player) { return 0x40000u + player; }
		// Where a new 3D player starts: the world's spawn for the first, then the
		// nearest free point on rings 2 units apart around it.
		[[nodiscard]] std::array<float, 3> spawnPosition() const;
		// Puts the 2D (TCP only) players of each 3D player's level on its screen
		// as GameObjects, and takes them off when they leave the level.
		void relayTcpPlayers(uint64_t now);
		uint64_t m_nextTcpRelay = 0;
		void sendInitialFlags(PlayerID player, Connection& connection);
		// Lab: the world's rawevent lines (`late`: the ones for after the mission
		// start) and, for protocol 19, its rawobject lines.
		void sendRawEvents(const std::string& account, Connection& connection, bool late);
		void sendTerrain(const std::string& account, Connection& connection, const Capabilities* caps, const std::array<uint32_t, 256>& stamps);
		void ghostRawObjects(const std::string& account, Connection& connection, const Capabilities* caps, ObjectKey firstKey);
		// The capabilities of the player's 3D session, or nullptr.
		const Capabilities* capabilitiesOf(PlayerID player) const;
		void sendWorldWeapons(Connection& connection);
		std::string compileClientScript(const std::string& name, std::string_view source);
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
		ShapeCheck m_shapeCheck = ShapeCheck::Skip;
		void checkWorldFiles();
		bool m_packetStrings = true;
		bool m_sharp3d = false;
		std::map<PlayerID, GhostObject> m_avatars;
		// What each client last reported as its own position, and, after a g3dpos
		// move, the report from before it: the client keeps sending that until it
		// moves itself, and those reports must not undo the server's move.
		std::map<PlayerID, std::array<float, 3>> m_lastReport;
		std::map<PlayerID, std::array<float, 3>> m_staleReport;
		std::unique_ptr<std::ofstream> m_trace;
		Mutation m_mutation = Mutation::None;
		uint32_t m_mutationSeed = 1;
		bool m_terrainForAll = false;
		bool m_pushTerrainFiles = false;
		std::vector<std::string> m_terrainFiles; // heights.info, then its textures, as found at start-up
		std::set<std::string> m_refusedFiles;   // lower-cased file names the start-up check found would crash a client
		std::set<std::string> m_terrainPushedTo; // accounts already sent m_terrainFiles (graal3dpushterrain)
		bool m_pushGrassTextures = true;
		uint32_t m_grassDelayMs = 0;
		static bool isGrass(const GhostObject& o) { return o.rawClass == "DynamicGrass" || o.rawClass == "DynamicGrassReplicator"; }
		std::vector<std::string> m_grassTextures; // replicator textures found and not refused at start-up
		std::set<std::string> m_grassPushedTo;   // accounts already sent m_grassTextures (graal3dpushgrass)
		[[nodiscard]] bool offers(const Capabilities* caps, const GhostObject& object, int protocol) const;
		[[nodiscard]] bool takesTerrain(const Capabilities* caps, int protocol) const;
		void trace(std::string_view account, int protocol, std::string_view what);
		std::set<std::string> m_unmatched; // "ip,account" already logged as unmatched
	};

} // namespace graal3d

#endif
