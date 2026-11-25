// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13, 2026-11-14, 2026-11-19, 2026-11-20, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the gserver's Graal 3D UDP side. See Graal3DService.h.

#include <graal3d/Graal3DService.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <IWin.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <IEnums.h>

#include <Server.h>
#include <object/Player.h>
#include <npcserver/NPCServer.h>
#include <object/Weapon.h>
#include <player/PlayerClient.h>
#include <scripting/ScriptContainers.h>
#include <utilities/Log.h>
#include <utilities/StringUtils.h>

using namespace preagonal;

namespace graal3d
{
	namespace
	{
		std::string hexString(std::string_view bytes)
		{
			static constexpr char digits[] = "0123456789abcdef";
			std::string out;
			for (const auto c : bytes)
			{
				out.push_back(digits[static_cast<uint8_t>(c) >> 4]);
				out.push_back(digits[static_cast<uint8_t>(c) & 15]);
			}
			return out;
		}
	} // namespace

	namespace
	{
		void closeSocket(SOCKET s)
		{
#if defined(_WIN32) || defined(_WIN64)
			closesocket(s);
#else
			close(s);
#endif
		}

		bool setNonBlocking(SOCKET s)
		{
#if defined(_WIN32) || defined(_WIN64)
			u_long on = 1;
			return ioctlsocket(s, FIONBIO, &on) == 0;
#else
			const int flags = fcntl(s, F_GETFL, 0);
			return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
		}

		uint32_t mix(uint64_t x)
		{
			// splitmix64 finaliser
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return static_cast<uint32_t>(x ^ (x >> 31));
		}

		// "::ffff:1.2.3.4" (a v4 client on a dual-stack listener) is "1.2.3.4".
		std::string_view plainIpv4(std::string_view ip)
		{
			constexpr std::string_view mapped = "::ffff:";
			if (ip.starts_with(mapped))
				ip.remove_prefix(mapped.size());
			return ip;
		}

		bool hasGraal3D(int version) { return Service::clientHasGraal3D(version); }
	} // namespace

	// 3.04 speaks the stock TGE demo protocol and is refused by the game string check.
	// The Linux 3.151 build (CLVER_3_151_LINUX, ordered just below 3.153) speaks
	// protocol 19 too.
	bool Service::clientHasGraal3D(int version)
	{
		return version >= CLVER_3_151_LINUX && version < CLVER_6_015;
	}

	Service::Service(Server& server)
		: m_server(server)
	{
		std::random_device rd;
		for (auto& s : m_secret)
			s = rd();
	}

	Service::~Service()
	{
		for (auto& [endpoint, session] : m_sessions)
			sendTo(endpoint, session.connection->disconnectPacket("The server is shutting down."));
		if (m_socket != INVALID_SOCKET)
			closeSocket(m_socket);
	}

	uint64_t Service::nowMs()
	{
		using namespace std::chrono;
		return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
	}

	std::string Service::ipString(const Endpoint& endpoint)
	{
		char buf[INET_ADDRSTRLEN] = {};
		in_addr addr{};
		addr.s_addr = endpoint.ip;
		inet_ntop(AF_INET, &addr, buf, sizeof(buf));
		return buf;
	}

	bool Service::start(std::string_view interfaceIp, uint16_t port)
	{
		m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket == INVALID_SOCKET)
			return false;

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (!interfaceIp.empty())
		{
			const std::string ip{interfaceIp};
			if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
				addr.sin_addr.s_addr = htonl(INADDR_ANY);
		}

		if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || !setNonBlocking(m_socket))
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCKET;
			return false;
		}
		m_port = port;
		return true;
	}

	void Service::sendTo(const Endpoint& to, std::string_view data)
	{
		if (m_socket == INVALID_SOCKET || data.empty())
			return;
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = to.ip;
		addr.sin_port = to.port;
		sendto(m_socket, data.data(), static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
	}

	Digest Service::digestFor(const Endpoint& from, uint32_t sequence) const
	{
		Digest d{};
		const uint64_t base = (static_cast<uint64_t>(from.ip) << 32) ^ (static_cast<uint64_t>(from.port) << 16) ^ sequence;
		for (size_t i = 0; i < d.size(); ++i)
			d[i] = mix(base ^ (static_cast<uint64_t>(m_secret[i]) << 21) ^ (i * 0x1000193ull));
		return d;
	}

	void Service::update()
	{
		if (m_socket == INVALID_SOCKET)
			return;

		const auto now = nowMs();
		char buffer[1500];
		for (int i = 0; i < 256; ++i)
		{
			sockaddr_in from{};
			socklen_t fromLen = sizeof(from);
			const auto got = recvfrom(m_socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
			if (got <= 0)
				break;
			handleDatagram(Endpoint{from.sin_addr.s_addr, from.sin_port}, std::string_view(buffer, static_cast<size_t>(got)), now);
		}

		std::vector<Endpoint> dead;
		for (auto& [endpoint, session] : m_sessions)
		{
			if (!session.connection->update(now))
				dead.push_back(endpoint);
		}
		for (const auto& endpoint : dead)
			closeSession(endpoint, "The 3D connection timed out.", false);
		relayTgeMoves();
		relayTcpPlayers(now);
	}

	std::array<float, 3> Service::spawnPosition() const
	{
		// Two players on one point draw as one (two 5.12 clients, lab
		// 2026-11-25): each new player takes the first free point of 8 around the
		// spawn, 2 units out, then 16 at 4 units, and so on.
		const auto& spawn = m_world.spawn.position;
		const auto taken = [&](const std::array<float, 3>& p) {
			for (const auto& [id, avatar] : m_avatars)
			{
				const auto& q = avatar.object.transform.position;
				if (std::hypot(q[0] - p[0], q[1] - p[1]) < 1.0f)
					return true;
			}
			return false;
		};
		if (!taken(spawn))
			return spawn;
		constexpr float pi = 3.14159265f;
		for (int ring = 1; ring <= 8; ++ring)
		{
			const int points = 8 * ring;
			for (int i = 0; i < points; ++i)
			{
				const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(points);
				const std::array<float, 3> p{spawn[0] + 2.0f * ring * std::cos(a), spawn[1] + 2.0f * ring * std::sin(a), spawn[2]};
				if (!taken(p))
					return p;
			}
		}
		return spawn;
	}

	void Service::relayTcpPlayers(uint64_t now)
	{
		// A 3D client draws only the GameObjects it is ghosted: a 2D player in its
		// level never showed (5.24r3 2D with 5.12 3D, lab 2026-11-25). Each 2D
		// player gets an object at its tile position, Torque (x, -y), at the
		// spawn's height, with its nickname and chat. Checked 5 times a second.
		if (now < m_nextTcpRelay)
			return;
		m_nextTcpRelay = now + 200;
		for (auto& [endpoint, session] : m_sessions)
		{
			const auto me = m_server.getPlayer(session.player);
			const auto level = me != nullptr ? me->getLevel() : nullptr;
			std::map<PlayerID, GhostObject> want;
			if (level != nullptr)
			{
				for (const auto& [id, p] : m_server.getPlayerList())
				{
					if (p == nullptr || id == session.player || m_byPlayer.contains(id) || !p->isClient() || !p->isLoaded() || p->getLevel() != level)
						continue;
					GhostObject avatar;
					avatar.object.name = "G2DPlayer_" + p->account.name;
					avatar.object.shape = m_world.playerShape;
					avatar.object.transform = m_world.spawn;
					const auto tile = p->getTilePosition();
					avatar.object.transform.position = {static_cast<float>(tile.x()), -static_cast<float>(tile.y()), m_world.spawn.position[2]};
					avatar.object.nickname = p->account.character.nickName;
					avatar.object.chat = p->account.character.chatMessage;
					avatar.object.flags = GameObjectFlags::Shadow;
					want.emplace(id, std::move(avatar));
				}
			}
			auto& shown = session.tcpShown;
			const auto account = me != nullptr ? me->account.name : std::to_string(session.player);
			const int protocol = static_cast<int>(session.connection->profile().protocol);
			for (auto it = shown.begin(); it != shown.end();)
			{
				if (want.contains(it->first))
				{
					++it;
					continue;
				}
				session.connection->removeGhost(tcpKey(it->first));
				trace(account, protocol, "2D player gone: " + it->second.object.name);
				it = shown.erase(it);
			}
			for (auto& [id, avatar] : want)
			{
				const auto s = shown.find(id);
				if (s != shown.end() && s->second.object == avatar.object)
					continue;
				ghostAvatar(*session.connection, tcpKey(id), avatar);
				const auto& p = avatar.object.transform.position;
				trace(account, protocol,
					  std::format("2D player {} {}: at={:.3f},{:.3f},{:.3f}{}", s == shown.end() ? "shown" : "update", avatar.object.name, p[0], p[1], p[2],
								  avatar.object.chat.empty() ? std::string() : " chat=" + avatar.object.chat));
				shown[id] = std::move(avatar);
			}
		}
	}

	bool Service::tgeOthers() const
	{
		return m_world.tgeControl == World::TgeControl::Player && !m_world.tgePlayerShape.empty();
	}

	void Service::ghostAvatar(Connection& connection, ObjectKey key, const GhostObject& avatar) const
	{
		if (!connection.profile().tgePackets)
		{
			connection.setGhost(key, avatar);
			return;
		}
		// Protocol 19 has no GameObject: the other player is a stock Player on the
		// PlayerData this client already has (sent for its own Player). Without
		// one, it gets no other players.
		if (!tgeOthers())
			return;
		GhostObject player;
		player.kind = GhostObject::Kind::TgePlayer;
		player.datablockId = tge::PlayerDatablockId;
		player.tgeShape = m_world.tgePlayerShape;
		player.object.name = avatar.object.name;
		const auto& t = avatar.object.transform;
		player.tgePlayer.position = t.position;
		player.tgePlayer.rotZ = t.axis[2] < 0.0f ? -t.angle : t.angle;
		connection.setGhost(key, player);
	}

	void Service::relayAvatar(PlayerID playerId, const GhostObject& before)
	{
		const auto it = m_avatars.find(playerId);
		if (it == m_avatars.end())
			return;
		if (it->second.object.transform.position != before.object.transform.position)
			syncTcpPosition(playerId, it->second.object.transform.position);
		size_t relayed = 0;
		forEachOtherConnection(playerId, [&](Connection& other) {
			ghostAvatar(other, avatarKey(playerId), it->second);
			++relayed;
		});
		if (m_trace)
		{
			const auto& object = it->second.object;
			trace(object.name, 0,
				  std::format("relay to {} other 3D clients: at={:.3f},{:.3f},{:.3f} angle={:.3f}{}", relayed, object.transform.position[0],
							  object.transform.position[1], object.transform.position[2], object.transform.angle,
							  object.chat != before.object.chat ? " chat=" + object.chat : std::string()));
		}
	}

	void Service::syncTcpPosition(PlayerID playerId, const std::array<float, 3>& position)
	{
		// The player's TCP side follows the 3D object, so serverside scripts see
		// where the player is: Torque (x, y) is map tile (x, -y). Without this the
		// script `player` stayed at the spawn point while the 3D Player walked
		// (4.04r2, lab 2026-11-25). Positions off the map (x or -y below 0) are left.
		// PlayerClient::setPosition sends the new x2/y2 to the level, so 2D
		// clients there see the 3D player walk.
		const auto player = m_server.getPlayer(playerId);
		if (player == nullptr)
			return;
		if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || position[0] < 0.0f || position[1] > 0.0f)
		{
			trace(player->account.name, 0, std::format("tcp position not set: {:.3f},{:.3f} is off the map (needs x >= 0, y <= 0)", position[0], position[1]));
			return;
		}
		auto tile = player->getTilePosition();
		tile.x() = position[0];
		tile.y() = -position[1];
		player->setPosition(toPixelPosition(tile));
		if (m_trace)
		{
			const auto level = player->getLevel();
			trace(player->account.name, 0,
				  std::format("tcp position {:.2f},{:.2f} in {}", position[0], -position[1], level != nullptr ? std::string(level->levelName) : std::string("no level")));
		}
	}

	void Service::relayTgeMoves()
	{
		for (auto& [endpoint, session] : m_sessions)
		{
			if (!session.connection->profile().tgePackets)
				continue;
			const auto player = session.connection->controlPlayer();
			if (!player)
				continue;
			const auto it = m_avatars.find(session.player);
			if (it == m_avatars.end())
				continue;
			const GhostObject before = it->second;
			auto& t = it->second.object.transform;
			t.position = player->position;
			t.axis = {0.0f, 0.0f, 1.0f};
			t.angle = player->rotZ;
			if (it->second.object == before.object)
				continue;
			relayAvatar(session.player, before);
		}
	}

	void Service::handleDatagram(const Endpoint& from, std::string_view data, uint64_t now)
	{
		if (data.empty())
			return;

		const auto type = static_cast<uint8_t>(data[0]);
		if ((type & 1u) != 0)
		{
			if (auto it = m_sessions.find(from); it != m_sessions.end())
				it->second.connection->receive(data, now);
			return;
		}

		switch (static_cast<InfoPacket>(type))
		{
			case InfoPacket::ChallengeRequest:
				if (const auto seq = parseChallengeRequest(data))
					sendTo(from, buildChallengeResponse(*seq, digestFor(from, *seq)));
				break;

			case InfoPacket::ConnectRequest:
				handleConnectRequest(from, data, now);
				break;

			case InfoPacket::Disconnect:
				if (auto it = m_sessions.find(from); it != m_sessions.end())
				{
					if (parseDisconnect(data) == it->second.connection->connectSequence())
						closeSession(from, "client disconnected", false);
				}
				break;

			default:
				break; // Torque's master-server and LAN queries are not answered.
		}
	}

	void Service::handleConnectRequest(const Endpoint& from, std::string_view data, uint64_t now)
	{
		const auto req = parseConnectRequest(data);
		if (!req || req->digest != digestFor(from, req->sequence))
			return;

		// A repeat of a request we already accepted: send the accept again.
		if (auto it = m_sessions.find(from); it != m_sessions.end())
		{
			if (it->second.connection->connectSequence() == req->sequence)
			{
				sendTo(from, buildConnectAccept(req->sequence, it->second.connection->profile(),
												 acceptTweak(m_mutation, m_mutationSeed, it->second.connection->profile())));
				return;
			}
			closeSession(from, "reconnected", false);
		}

		if (auto reason = checkConnectRequest(*req))
		{
			sendTo(from, buildConnectReject(req->sequence, *reason));
			return;
		}

		// The client opens TCP and UDP together, and the TCP login may not be done
		// yet. Until it is, say nothing: Torque resends the connect request.
		const std::string& requested = req->args[0];
		auto player = findTcpPlayer(requested, ipString(from));
		if (player == nullptr)
		{
			// Say it once per address and name, not on every resend.
			if (m_unmatched.size() > 1024)
				m_unmatched.clear();
			if (m_unmatched.insert(ipString(from) + "," + requested).second)
				log::printLine(log::server, "Graal3D: {} asked for a 3D link from {}, but no client logged in over TCP from there matches it (yet).", requested, ipString(from));
			return;
		}
		const std::string account = player->account.name;

		if (!hasGraal3D(player->getVersion()))
		{
			sendTo(from, buildConnectReject(req->sequence, "CHR_PROTOCOL"));
			return;
		}

		const auto* profile = profileForProtocol(req->currentProtocol);
		if (m_sharp3d)
		{
			if (const auto* sharp = sharp3dProfileForProtocol(req->currentProtocol))
				profile = sharp;
		}
		const PlayerID playerId = player->getId();
		const Capabilities* caps = capabilitiesFor(player->getVersionStr(), profile->protocol);
		if (caps)
		{
			const char* terrain = caps->takesStreamedTerrain512() ? "streamed"
								  : takesTerrain(caps, static_cast<int>(profile->protocol)) ? "streamed (graal3dterrainall)"
								  : caps->terrainChunks ? "stock TGE only (not served)"
														: "none";
			log::printLine(log::server, "Graal3D: {} is a {} client: {}, terrain {}, TorqueScript {}{}.", account, caps->label,
						   caps->gameLayer == GameLayer::Tge ? "TGE layer" : "GameObject layer", terrain, caps->torqueScript ? "yes" : "no",
						   caps->allAssumed() ? " (every capability assumed)" : caps->confirmed(CapField::Protocol) ? "" : " (protocol assumed)");
		}

		// One 3D link per player.
		if (auto it = m_byPlayer.find(playerId); it != m_byPlayer.end())
			closeSession(it->second, "Another 3D connection was opened.", true);

		Connection::Callbacks cb;
		cb.send = [this, from](std::string_view d) { sendTo(from, d); };
		cb.onText = [this, playerId](const TextMessage& m) { handleText(playerId, m); };
		cb.log = [account](std::string_view text) { log::printLine(log::server, "Graal3D {}: {}", account, text); };
		cb.onMission = [this, playerId, account](MissionState state) {
			if (state == MissionState::Phase1Sent)
			{
				if (auto it = m_byPlayer.find(playerId); it != m_byPlayer.end())
				{
					if (auto s = m_sessions.find(it->second); s != m_sessions.end())
					{
						sendInitialFlags(playerId, *s->second.connection);
						sendWorldWeapons(*s->second.connection);
					}
				}
			}
			else if (state == MissionState::Done)
			{
				log::printLine(log::server, "Graal3D: {} finished the mission start.", account);
				// The client loads a Sky's material list only when it unpacks the Sky,
				// and the list arrives over TCP after the first unpack: send it again.
				if (auto it = m_byPlayer.find(playerId); it != m_byPlayer.end())
				{
					if (auto s = m_sessions.find(it->second); s != m_sessions.end())
					{
						ObjectKey key = 1;
						for (const auto& object : m_world.objects)
						{
							if (object.kind == GhostObject::Kind::Sky)
								s->second.connection->resendGhost(key);
							++key;
						}
						sendRawEvents(account, *s->second.connection, true);
					}
				}
			}
		};

		cb.onClientState = [this, playerId](const ClientState& state) { handleClientState(playerId, state); };
		if (m_trace)
			cb.trace = [this, account, protocol = profile->protocol](std::string_view line) { trace(account, protocol, line); };
		cb.onTerrainRequest = [this, playerId, account](const std::array<uint32_t, 256>& stamps) {
			if (auto it = m_byPlayer.find(playerId); it != m_byPlayer.end())
				if (auto s = m_sessions.find(it->second); s != m_sessions.end())
					sendTerrain(account, *s->second.connection, s->second.caps, stamps);
		};

		Session session{from, playerId, std::make_unique<Connection>(*profile, req->sequence, now, std::move(cb)), caps};
		auto& connection = *session.connection;
		connection.setPacketStrings(m_packetStrings);
		m_sessions.emplace(from, std::move(session));
		m_byPlayer[playerId] = from;
		player->setAddonLinked(true);
		// Once per account per server run: a client keeps what it was sent, so
		// pushing again on every link only repeats the same transfers.
		if (m_pushTerrainFiles && m_world.terrain && takesTerrain(caps, static_cast<int>(profile->protocol)) && m_terrainPushedTo.insert(account).second)
		{
			for (const auto& name : m_terrainFiles)
				player->sendFile(name);
			trace(account, profile->protocol, std::format("pushed {} terrain files over TCP (graal3dpushterrain)", m_terrainFiles.size()));
		}

		// The player's own object, on every screen; the world and the other
		// players on theirs. Ghosts go out once the connection is up.
		GhostObject avatar;
		avatar.object.name = "G3DPlayer_" + account;
		avatar.object.shape = m_world.playerShape;
		avatar.object.transform = m_world.spawn;
		avatar.object.transform.position = spawnPosition();
		avatar.object.nickname = player->account.character.nickName;
		avatar.object.flags = GameObjectFlags::Shadow;
		m_avatars[playerId] = avatar;
		// The TCP side starts where the 3D object does: a 3D spawn at (20, -20)
		// left the account at the server's start position (lab, 2026-11-25).
		syncTcpPosition(playerId, avatar.object.transform.position);
		ghostWorld(playerId, account, connection);
		forEachOtherConnection(playerId, [&](Connection& other) { ghostAvatar(other, avatarKey(playerId), avatar); });

		connection.setMutation(m_mutation, m_mutationSeed);
		std::string tweaked;
		const auto acceptPacket = buildConnectAccept(req->sequence, *profile, acceptTweak(m_mutation, m_mutationSeed, *profile, &tweaked));
		if (!tweaked.empty())
		{
			const auto line = std::format("mutation {}:{} in the connect accept: {}; packet {}", mutationName(m_mutation), m_mutationSeed, tweaked,
										  hexString(acceptPacket));
			log::printLine(log::server, "Graal3D {}: {}", account, line);
			trace(account, profile->protocol, line);
		}
		sendTo(from, acceptPacket);
		log::printLine(log::server, "Graal3D: {} linked from {} (protocol {}, {}).", account, ipString(from), profile->protocol, profile->clients);
		{
			const auto& at = m_avatars[playerId].object.transform.position;
			trace(account, profile->protocol,
				  std::format("linked from {} ({}); {} other 3D players; spawn {:.2f},{:.2f},{:.2f}", ipString(from), profile->clients, m_sessions.size() - 1, at[0],
							  at[1], at[2]));
		}
	}

	std::shared_ptr<Player> Service::findTcpPlayer(const std::string& requested, const std::string& ip)
	{
		const auto fromThere = [&](const std::shared_ptr<Player>& p) {
			if (p == nullptr || !p->isLoaded() || p->getSocket() == nullptr || !p->isClient())
				return false;
			const char* tcpIp = p->getSocket()->getRemoteIp();
			return tcpIp != nullptr && plainIpv4(tcpIp) == ip;
		};

		if (auto p = m_server.getPlayer(CString(requested), PLTYPE_ANYCLIENT); fromThere(p))
			return p;

		// The 5.x client sends "guest" for a guest login, but the gserver renames
		// guests to pc:<number>. Take the guest client from the same address,
		// preferring one without a 3D link; the newest login wins a tie.
		if (requested != "guest")
			return nullptr;
		std::shared_ptr<Player> best;
		for (const auto& [id, p] : m_server.getPlayerList())
		{
			if (!fromThere(p) || !p->isGuest() || !hasGraal3D(p->getVersion()))
				continue;
			const bool linked = m_byPlayer.contains(id);
			const bool bestLinked = best != nullptr && m_byPlayer.contains(best->getId());
			if (best == nullptr || (bestLinked && !linked) || (bestLinked == linked && id > best->getId()))
				best = p;
		}
		return best;
	}

	void Service::closeSession(const Endpoint& endpoint, std::string_view reason, bool notifyClient)
	{
		auto it = m_sessions.find(endpoint);
		if (it == m_sessions.end())
			return;

		if (notifyClient)
			sendTo(endpoint, it->second.connection->disconnectPacket(reason));

		const PlayerID playerId = it->second.player;
		if (m_trace)
		{
			const auto a = m_avatars.find(playerId);
			trace(a != m_avatars.end() ? a->second.object.name : std::to_string(playerId), it->second.connection->profile().protocol,
				  "closed: " + std::string(reason));
		}
		if (auto p = m_byPlayer.find(playerId); p != m_byPlayer.end() && p->second == endpoint)
		{
			m_byPlayer.erase(p);
			if (auto player = m_server.getPlayer(playerId))
				player->setAddonLinked(false);
			const auto a = m_avatars.find(playerId);
			const std::string name = a != m_avatars.end() ? a->second.object.name : std::to_string(playerId);
			m_avatars.erase(playerId);
			m_lastReport.erase(playerId);
			m_staleReport.erase(playerId);
			size_t removed = 0;
			forEachOtherConnection(playerId, [&](Connection& other) {
				if (other.ghostIndex(avatarKey(playerId)).has_value())
					++removed;
				other.removeGhost(avatarKey(playerId));
			});
			trace(name, it->second.connection->profile().protocol, std::format("left: its object goes from {} other 3D clients", removed));
		}
		log::printLine(log::server, "Graal3D: link from {} closed ({}).", ipString(endpoint), reason);
		m_sessions.erase(it);
	}

	void Service::checkWorldFiles()
	{
		// Every file the world names is looked up the way a client's request is:
		// case-insensitively, and a bare texture name as <name>.<image extension>.
		// Files are meant to keep their real extensions on disk; the clients
		// drop them when they ask (bin/docs/graal3d.md, "Shipping a 3D world").
		const auto& files = m_server.getFileSystem();
		m_terrainFiles.clear();
		const auto find = [&](const std::string& name) -> preagonal::fs::FileData* {
			const std::filesystem::path path{name};
			auto info = files.infoi(preagonal::fs::FileCategory::ALL, path.filename());
			if (info == nullptr && !path.has_extension())
				for (const auto* ext : kImageExtensions)
					if ((info = files.infoi(preagonal::fs::FileCategory::ALL, path.filename().string() + ext)) != nullptr)
						break;
			return info;
		};
		const auto readAll = [](const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};

		// A texture cannot be left out the way an object can, so a bad one is
		// only logged (lab, 2026-11-25: a PNG claiming 30000x30000 crashed 5.12).
		const auto checkImage = [&](const preagonal::fs::FileData& info) {
			if (m_shapeCheck == ShapeCheck::Off)
				return;
			const auto data = readAll(info.file);
			if (const auto why = unsafeImage(data))
				log::printLine(log::server, "** [Error] Graal3D: {} would crash a client ({}); replace it.", info.file.filename().string(), *why);
			// 8-bit PNG, JPEG and GIF draw on 5.12; a 16-bit PNG does not
			// (texture probe, Graal_analysis, 2026-11-25).
			else if (pngSize(data) && static_cast<uint8_t>(data[24]) == 16)
				log::printLine(log::server, "** [Warning] Graal3D: {} is a 16-bit PNG, which 5.12 does not draw; save it as 8-bit.", info.file.filename().string());
			// 5.12 drew textures up to 1024 a side; 2048 and 4096 came out white
			// (live, Graal_analysis torque/live-512-shapes-2026-11-25.md).
			else if (const auto size = pngSize(data); size && (size->first > kMaxDrawnImageSide || size->second > kMaxDrawnImageSide))
				log::printLine(log::server, "** [Warning] Graal3D: {} is {}x{}; 5.12 draws nothing over {} a side.", info.file.filename().string(), size->first,
							   size->second, kMaxDrawnImageSide);
			if (data.empty())
				log::printLine(log::server, "** [Warning] Graal3D: {} is empty; clients draw it white.", info.file.filename().string());
			auto ext = info.file.extension().string();
			for (auto& ch : ext)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			if (ext == ".bmp")
				log::printLine(log::server, "** [Warning] Graal3D: {} is a BMP, which 5.12 does not draw; use PNG, JPEG or GIF.", info.file.filename().string());
		};

		size_t found = 0, missing = 0, dropped = 0;
		std::erase_if(m_world.objects, [&](const GhostObject& object) {
			if (object.kind == GhostObject::Kind::Sky)
			{
				size_t start = 0;
				const auto& list = object.sky.materialList;
				while (start < list.size())
				{
					auto end = list.find(',', start);
					if (end == std::string::npos)
						end = list.size();
					const auto name = list.substr(start, end - start);
					start = end + 1;
					if (name.empty())
						continue;
					if (const auto info = find(name); info == nullptr)
					{
						++missing;
						log::printLine(log::server, "** [Warning] Graal3D: the Sky's texture {} is not in the file folders.", name);
					}
					else
					{
						++found;
						checkImage(*info);
					}
				}
				return false;
			}
			if (object.kind != GhostObject::Kind::GameObject || object.object.shape.empty())
				return false;
			const auto& shape = object.object.shape;
			const auto info = find(shape);
			if (info == nullptr)
			{
				++missing;
				log::printLine(log::server, "** [Warning] Graal3D: {}'s shape {} is not in the file folders.", object.object.name, shape);
				return false;
			}
			++found;
			if (!info->file.has_extension())
				log::printLine(log::server, "** [Warning] Graal3D: {} has no extension on disk; ship it under its real name.", info->file.filename().string());
			auto ext = info->file.extension().string();
			for (auto& ch : ext)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			if ((ext != ".3ds" && ext != ".dts") || m_shapeCheck == ShapeCheck::Off)
				return false;
			const auto why = unsafeAsset(ext, readAll(info->file));
			if (!why)
				return false;
			if (m_shapeCheck == ShapeCheck::Warn)
			{
				log::printLine(log::server, "** [Warning] Graal3D: {} would crash a client ({}); sent anyway (graal3dshapecheck = warn).", shape, *why);
				return false;
			}
			++dropped;
			log::printLine(log::server, "** [Error] Graal3D: {} would crash a client ({}); {} is left out (graal3dshapecheck = warn sends it).", shape, *why, object.object.name);
			return true;
		});
		// The player shapes go out at login; a bad one crashed 4.04r2 in "Phase 1:
		// Download Datablocks" in 16 of 20 fuzzed cases (lab, 2026-11-25). With
		// graal3dshapecheck = skip it is dropped: protocol 19 then controls a
		// Camera, and the GameObject route draws players without a shape.
		for (auto* shape : {&m_world.playerShape, &m_world.tgePlayerShape})
		{
			if (shape->empty())
				continue;
			const auto info = find(*shape);
			if (info == nullptr)
			{
				++missing;
				log::printLine(log::server, "** [Warning] Graal3D: the player shape {} is not in the file folders.", *shape);
				continue;
			}
			++found;
			auto ext = info->file.extension().string();
			for (auto& ch : ext)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			if ((ext != ".3ds" && ext != ".dts") || m_shapeCheck == ShapeCheck::Off)
				continue;
			const auto why = unsafeAsset(ext, readAll(info->file));
			if (!why)
				continue;
			if (m_shapeCheck == ShapeCheck::Warn)
			{
				log::printLine(log::server, "** [Warning] Graal3D: the player shape {} would crash a client at login ({}); sent anyway (graal3dshapecheck = warn).", *shape, *why);
				continue;
			}
			log::printLine(log::server, "** [Error] Graal3D: the player shape {} would crash a client at login ({}); it is not used.", *shape, *why);
			shape->clear();
		}

		// Streamed terrain: the client asks for heights.info over TCP, then for
		// each texture it names. If the file folders do not cover the terrain
		// folder, 5.12 draws the terrain white and fetches nothing (lab,
		// 2026-11-25); a foldersconfig line such as `file 3d/<name>_terrain/*`
		// is needed, since a folder line covers only its own files.
		if (m_world.terrain)
		{
			const auto info = find("heights.info");
			if (info == nullptr)
			{
				++missing;
				log::printLine(log::server, "** [Warning] Graal3D: heights.info is not in the file folders, so clients draw the terrain white; add a foldersconfig line for {}.",
							   m_world.terrain->folder);
			}
			else
			{
				++found;
				m_terrainFiles.push_back("heights.info");
				const auto expected = std::filesystem::path(m_world.terrain->folder) / "heights.info";
				std::error_code ec;
				// Clients ask for the bare name, so any other heights.info in the
				// file folders can win: the terrain then asks for that file's
				// textures (lab, 2026-11-25: a stray world/global/heights.info
				// gave "Missing terrain texture: grass/rock").
				if (!std::filesystem::equivalent(info->file, expected, ec))
					log::printLine(log::server, "** [Error] Graal3D: clients get {} as heights.info, not the one in {}; remove or rename the other file, or the terrain asks for the wrong textures.",
								   info->file.string(), m_world.terrain->folder);
				std::istringstream lines(readAll(info->file));
				for (std::string line; std::getline(lines, line);)
				{
					while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
						line.pop_back();
					if (line.empty())
						continue;
					// 5.12 asks for the whole line as one file name, so four names
					// on one line become one request for a file that is not there
					// (fuzzing, lab 2026-11-25).
					if (line.find_first_of(" \t,") != std::string::npos)
						log::printLine(log::server, "** [Warning] Graal3D: heights.info line \"{}\" is not one texture name; clients ask for the whole line. Put one name on each line.", line);
					// A name listed again is drawn from the same file: check and
					// push it once (1000 repeats made the push about 60 MB).
					if (std::ranges::find(m_terrainFiles, line) != m_terrainFiles.end())
						continue;
					if (const auto texture = find(line); texture == nullptr)
					{
						++missing;
						log::printLine(log::server, "** [Warning] Graal3D: terrain texture {} (heights.info) is not in the file folders.", line);
					}
					else
					{
						++found;
						checkImage(*texture);
						const auto data = readAll(texture->file);
						// Anything else and the client draws its default texture
						// instead (lab, 2026-11-25).
						if (const auto size = pngSize(data); size && (size->first != 256 || size->second != 256))
							log::printLine(log::server, "** [Warning] Graal3D: terrain texture {} is {}x{}; clients draw only 256x256 ones.",
										   texture->file.filename().string(), size->first, size->second);
						// One that would crash a client (30000x30000 and 65536x1
						// did) is never pushed unasked.
						if (m_shapeCheck == ShapeCheck::Off || !unsafeImage(data))
							m_terrainFiles.push_back(line);
					}
				}
			}
		}
		log::printLine(log::server, "Graal3D: the world names {} files the folders have, {} they do not{}.", found, missing,
					   dropped ? std::format(", and {} objects with unsafe shapes were left out", dropped) : std::string{});
	}

	void Service::setWorld(World world, bool packetStrings)
	{
		m_world = std::move(world);
		m_packetStrings = packetStrings;
		checkWorldFiles();

		// One TerrainBlock a world: with two, live 5.12 asks for chunks for both
		// but lays them only on the second (terrain lab, 2026-11-25).
		const auto terrainBlocks = std::ranges::count_if(m_world.objects, [](const GhostObject& o) { return o.rawClass == "TerrainBlock"; });
		if (terrainBlocks > 1)
			log::printLine(log::server, "** [Warning] Graal3D: the world has {} TerrainBlocks; 5.12 lays the streamed chunks only on the last. Keep one.",
						   terrainBlocks);

		// The lab's raw fields may name files (file:, zfile:), relative to the
		// server folder like the weapon3d files.
		const auto read = [](const std::string& path) -> std::optional<std::string> {
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return std::nullopt;
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
		std::vector<std::string> errors;
		for (auto& object : m_world.objects)
		{
			if (object.kind != GhostObject::Kind::Raw)
				continue;
			for (auto& e : loadRawFiles(object.raw, read))
				errors.push_back("rawobject " + object.rawClass + ": " + e);
			const auto bits = rawFieldBits(object.raw);
			if (bits >= (1u << 14))
				errors.push_back("rawobject " + object.rawClass + ": the update is " + std::to_string(bits) +
								 " bits, more than an int14 length holds");
		}
		size_t raw = 0;
		for (auto& event : m_world.rawEvents)
		{
			for (auto& e : loadRawFiles(event.fields, read))
				errors.push_back("rawevent " + event.className + ": " + e);
			if (const auto bits = rawFieldBits(event.fields); bits >= (1u << 14))
				errors.push_back("rawevent " + event.className + ": the payload is " + std::to_string(bits) +
								 " bits, more than an int14 length holds");
			++raw;
		}
		if (m_world.terrain)
		{
			for (auto& e : loadTerrain(*m_world.terrain, read))
				errors.push_back("terrain " + m_world.terrain->folder + ": " + e);
			size_t chunks = 0;
			for (const auto& c : m_world.terrain->chunks)
				chunks += c ? 1 : 0;
			log::printLine(log::server, "Graal3D: terrain from {}: {} of 256 chunks, square size {}{}.", m_world.terrain->folder, chunks,
						   m_world.terrain->squareSize, m_world.terrain->repeat ? ", repeating" : "");
		}
		for (const auto& e : errors)
			log::printLine(log::server, "** [Error] Graal3D: graal3dworld.txt {}", e);
		if (raw > 0)
			log::printLine(log::server, "Graal3D: the world has {} lab raw events.", raw);
	}

	void Service::sendRawEvents(const std::string& account, Connection& connection, bool late)
	{
		size_t sent = 0;
		std::set<std::string> missing;
		for (const auto& event : m_world.rawEvents)
		{
			if (event.late != late)
				continue;
			if (connection.postRawEvent(event.className, event.fields))
				++sent;
			else
				missing.insert(event.className);
		}
		if (sent > 0)
			log::printLine(log::server, "Graal3D: {} gets {} raw events{}.", account, sent, late ? " after the mission start" : "");
		for (const auto& name : missing)
			log::printLine(log::server, "Graal3D: {}'s client has no event class {}: its raw events are not sent.", account, name);
	}

	const Capabilities* Service::capabilitiesOf(PlayerID player) const
	{
		if (auto it = m_byPlayer.find(player); it != m_byPlayer.end())
			if (auto s = m_sessions.find(it->second); s != m_sessions.end())
				return s->second.caps;
		return nullptr;
	}

	bool Service::takesTerrain(const Capabilities* caps, int protocol) const
	{
		if (m_terrainForAll && (protocol == 21 || protocol == 24))
			return true;
		return caps && caps->takesStreamedTerrain512();
	}

	bool Service::offers(const Capabilities* caps, const GhostObject& object, int protocol) const
	{
		if (object.layout512 && m_terrainForAll && (protocol == 21 || protocol == 24))
			return true;
		return offersWorldObject(caps, object);
	}

	void Service::sendTerrain(const std::string& account, Connection& connection, const Capabilities* caps, const std::array<uint32_t, 256>& stamps)
	{
		if (!m_world.terrain)
		{
			log::printLine(log::server, "Graal3D: {} asked for terrain chunks; the world has no terrain line.", account);
			return;
		}
		if (!takesTerrain(caps, static_cast<int>(connection.profile().protocol)))
		{
			log::printLine(log::server, "Graal3D: {} asked for terrain chunks; its build ({}) does not take the 5.x terrain layout.", account,
						   caps ? caps->label : "unknown");
			return;
		}
		size_t sent = 0, current = 0;
		for (size_t i = 0; i < 256; ++i)
		{
			const auto& chunk = m_world.terrain->chunks[i];
			if (!chunk)
				continue;
			if (chunk->stamp == stamps[i])
			{
				++current;
				continue;
			}
			if (!connection.postTerrainChunk(static_cast<uint32_t>(i % 16), static_cast<uint32_t>(i / 16), chunk->stamp, chunk->packed))
			{
				log::printLine(log::server, "Graal3D: {}'s client has no TerrainChunkEvent class: no terrain sent.", account);
				return;
			}
			++sent;
		}
		log::printLine(log::server, "Graal3D: {} gets {} terrain chunks ({} already current).", account, sent, current);
	}

	void Service::ghostRawObjects(const std::string& account, Connection& connection, const Capabilities* caps, ObjectKey firstKey)
	{
		// Every raw object goes to every client whose build takes it;
		// Connection::setGhost logs the ones whose class the client's table does
		// not have.
		ObjectKey key = firstKey;
		size_t count = 0, skipped = 0;
		for (const auto& object : m_world.objects)
		{
			if (object.kind == GhostObject::Kind::Raw)
			{
				if (offers(caps, object, static_cast<int>(connection.profile().protocol)))
				{
					connection.setGhost(key, object);
					++count;
				}
				else
					++skipped;
			}
			++key;
		}
		if (count > 0)
			log::printLine(log::server, "Graal3D: {} gets {} raw objects.", account, count);
		if (skipped > 0)
			log::printLine(log::server, "Graal3D: {}'s build ({}) does not take the 5.x terrain or water layout: {} objects not sent.", account,
						   caps ? caps->label : "unknown", skipped);
	}

	void Service::ghostWorld(PlayerID playerId, const std::string& account, Connection& connection)
	{
		if (connection.profile().tgePackets)
		{
			// 3.153 and 4.04r2: the GameObject world is not theirs. Each gets a Sky
			// (the world's, else a plain one: 4.04r2 reads the scene's Sky on its
			// first 3D frame without a null check) and one stock TGE Player, or with
			// `tgecontrol camera` a Camera, at the spawn point as its control object.
			ObjectKey key = 1;
			bool sky = false;
			const Capabilities* tgeCaps = capabilitiesOf(playerId);
			for (const auto& object : m_world.objects)
			{
				if (object.kind == GhostObject::Kind::Sky)
				{
					GhostObject padded = object;
					padded.sky.materialList = tge::tgeSkyMaterialList(object.sky.materialList);
					connection.setGhost(key, padded);
					sky = true;
					// 3.153 asks for each sky texture as <stem>.gif (lab), so the
					// server's files need a .gif of each.
					if (tgeCaps && tgeCaps->skyGif && !object.sky.materialList.empty())
					{
						std::string names;
						std::string_view list = object.sky.materialList;
						while (!list.empty())
						{
							const auto comma = list.find(',');
							const auto name = list.substr(0, comma);
							if (!name.empty())
								names += (names.empty() ? "" : ", ") + tgeCaps->skyTextureRequestName(name);
							list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
						}
						log::printLine(log::server, "Graal3D: {}'s build ({}) asks for the sky textures as .gif: {}.", account, tgeCaps->label,
									   names);
					}
				}
				++key;
			}
			if (!sky)
			{
				GhostObject plain;
				plain.kind = GhostObject::Kind::Sky;
				plain.sky.bools = {0, 0, 0}; // no textures: the solid colour
				plain.sky.materialList = tge::tgeSkyMaterialList("");
				connection.setGhost(skyKey(), plain);
			}

			// 4.04r2 refuses a Player whose datablock has no shape it can load, so
			// without a tgeplayershape the client gets the Camera.
			const bool player = m_world.tgeControl == World::TgeControl::Player && !m_world.tgePlayerShape.empty();
			if (m_world.tgeControl == World::TgeControl::Player && !player)
				log::printLine(log::server, "Graal3D: no tgeplayershape in graal3dworld.txt, so {} (protocol 19) gets a Camera.", account);
			// Its own spawn point (see spawnPosition), set before ghostWorld.
			const auto own = m_avatars.find(playerId);
			const auto spawnAt = own != m_avatars.end() ? own->second.object.transform.position : m_world.spawn.position;
			GhostObject control;
			if (!player)
			{
				control.kind = GhostObject::Kind::TgeCamera;
				control.datablockId = tge::CameraDatablockId;
				control.tgeCamera.position = spawnAt;
			}
			else
			{
				control.kind = GhostObject::Kind::TgePlayer;
				control.datablockId = tge::PlayerDatablockId;
				control.tgePlayer.position = spawnAt;
				control.tgeShape = m_world.tgePlayerShape;
			}
			connection.setGhost(cameraKey(playerId), control);
			connection.setControlObject(cameraKey(playerId));
			for (const auto& [other, avatar] : m_avatars)
			{
				if (other != playerId)
					ghostAvatar(connection, avatarKey(other), avatar);
			}
			if (player)
			{
				// Its own object, for the others: where its Player starts.
				if (auto a = m_avatars.find(playerId); a != m_avatars.end())
					a->second.object.transform.position = spawnAt;
			}
			ghostRawObjects(account, connection, capabilitiesOf(playerId), 1);
			sendRawEvents(account, connection, false);
			log::printLine(log::server, "Graal3D: {} (protocol 19) gets a Sky and a {} at the spawn point as its control object.", account,
						   control.className());
			return;
		}

		ObjectKey key = 1;
		const Capabilities* caps = capabilitiesOf(playerId);
		bool sky = false;
		for (const auto& object : m_world.objects)
		{
			if (offers(caps, object, static_cast<int>(connection.profile().protocol)))
				connection.setGhost(key, object);
			sky = sky || object.kind == GhostObject::Kind::Sky;
			++key;
		}
		if (caps && caps->requiresSky && !sky)
			log::printLine(log::server, "Graal3D: {}'s build ({}) crashes without a Sky, and graal3dworld.txt has no sky line.", account,
						   caps->label);
		for (const auto& [other, avatar] : m_avatars)
			connection.setGhost(avatarKey(other), avatar); // its own included: the control object
		// The Sharp3D route's control object is the client's Camera (set below).
		if (!connection.profile().ghostAlwaysWorld)
			connection.setControlObject(avatarKey(playerId));

		// A Camera of its own. The 5.x client draws its 3D view only through
		// one: with just the control object the window stays black (lab
		// 2026-11-20, real 5.12 on Windows 10). It faces the spawn point from
		// the world's camera offset (DefaultCameraOffset without a camera
		// line); the client's scripts may move it (cameramode, position).
		GhostObject camera;
		camera.kind = GhostObject::Kind::Camera;
		camera.object.name = "G3DCamera_" + account;
		camera.object.transform = cameraTransform(m_world);
		connection.setGhost(cameraKey(playerId), camera);
		connection.setCameraObject(cameraKey(playerId));
		if (connection.profile().ghostAlwaysWorld)
			connection.setControlObject(cameraKey(playerId));
		log::printLine(log::server, "Graal3D: {} gets {} world objects and {} players.", account, m_world.objects.size(), m_avatars.size());
		sendRawEvents(account, connection, false);
	}

	void Service::setMutation(const std::string& text)
	{
		m_mutation = Mutation::None;
		if (text.empty() || text == "off")
			return;
		if (const auto parsed = parseMutation(text))
		{
			std::tie(m_mutation, m_mutationSeed) = *parsed;
			log::printLine(log::server, "Graal3D: LAB MUTATION {}:{} on every 3D link (graal3dmutate). Never on a public server.", mutationName(m_mutation),
						   m_mutationSeed);
		}
		else
			log::printLine(log::server, "** [Error] Graal3D: graal3dmutate {} is not a known mutation; off.", text);
	}

	void Service::setTrace(const std::string& path)
	{
		m_trace.reset();
		if (path.empty())
			return;
		auto file = std::make_unique<std::ofstream>(path, std::ios::app);
		if (!*file)
		{
			log::printLine(log::server, "** [Error] Graal3D: cannot open graal3dtrace file {}.", path);
			return;
		}
		m_trace = std::move(file);
		log::printLine(log::server, "Graal3D: tracing every 3D client to {}.", path);
	}

	void Service::trace(std::string_view account, int protocol, std::string_view what)
	{
		if (!m_trace)
			return;
		// Milliseconds, the account (or a player object's name), the protocol (0:
		// the server's own relay), what happened.
		*m_trace << nowMs() << '\t' << account << '\t' << protocol << '\t' << what << '\n';
		m_trace->flush();
	}

	void Service::forEachOtherConnection(PlayerID playerId, const std::function<void(Connection&)>& fn)
	{
		for (auto& [endpoint, session] : m_sessions)
		{
			if (session.player != playerId)
				fn(*session.connection);
		}
	}

	void Service::handleClientState(PlayerID playerId, const ClientState& state)
	{
		auto it = m_avatars.find(playerId);
		if (it == m_avatars.end())
			return;
		// Sharp3D route: the client controls its Camera, not its player object,
		// so what it reports is not where the player is. Its object moves with
		// the g3dpos trigger only.
		if (auto e = m_byPlayer.find(playerId); e != m_byPlayer.end())
		{
			if (auto s = m_sessions.find(e->second); s != m_sessions.end() && s->second.connection->profile().ghostAlwaysWorld)
				return;
		}

		// The client owns its object: take what it reports, and pass it on to
		// everyone else. Its own screen is never sent its position back.
		auto& object = it->second.object;
		const auto before = object;
		// After a g3dpos move the client goes on reporting where it was before
		// (it does not take the server's word for its own object), and that
		// undid the move (two 5.12 clients, lab 2026-11-25). Such reports are
		// set aside until the client reports somewhere else.
		bool serverMoved = false;
		if (const auto stale = m_staleReport.find(playerId); stale != m_staleReport.end())
		{
			if (std::hypot(state.position[0] - stale->second[0], state.position[1] - stale->second[1], state.position[2] - stale->second[2]) < 0.01f)
				serverMoved = true;
			else
				m_staleReport.erase(stale);
		}
		m_lastReport[playerId] = state.position;
		if (!serverMoved)
			object.transform.position = state.position;
		if (state.controlling)
		{
			if ((state.changed & GameObjectBits::Transform) && !serverMoved)
				object.transform = state.object.transform;
			if ((state.changed & GameObjectBits::Chat) && object.chat != state.object.chat)
			{
				object.chat = state.object.chat;
				// And to the 2D clients in its level, as its chat prop.
				if (auto player = m_server.getPlayer(playerId))
					player->setChat(CString(object.chat));
			}
			if (state.changed & GameObjectBits::Scale)
				object.scale = state.object.scale;
			// Its looks too, so another client sees a change of shape, skin,
			// attributes or nickname (and its colour). The name and flags stay the
			// server's.
			if (state.changed & GameObjectBits::Shape)
				object.shape = state.object.shape;
			if (state.changed & GameObjectBits::Skin)
				object.skin = state.object.skin;
			if (state.changed & GameObjectBits::Attrs)
				object.attr = state.object.attr;
			if (state.changed & GameObjectBits::Nickname)
			{
				object.nickname = state.object.nickname;
				object.nickColor = state.object.nickColor;
			}
		}
		if (object == before)
			return;
		GhostObject was = it->second;
		was.object = before;
		relayAvatar(playerId, was);
	}

	bool Service::moveAvatar(PlayerID playerId, std::string_view position)
	{
		auto it = m_avatars.find(playerId);
		if (it == m_avatars.end())
			return false;

		std::array<float, 3> to{};
		std::string text{position};
		std::istringstream in{text};
		for (auto& v : to)
		{
			if (!(in >> v) || !std::isfinite(v))
				return false;
		}

		auto& object = it->second.object;
		if (object.transform.position == to)
			return true;
		const GhostObject before = it->second;
		object.transform.position = to;
		const auto last = m_lastReport.find(playerId);
		m_staleReport[playerId] = last != m_lastReport.end() ? last->second : before.object.transform.position;

		// To the others (traced, and the TCP side follows), then to its own screen.
		relayAvatar(playerId, before);
		if (const auto e = m_byPlayer.find(playerId); e != m_byPlayer.end())
		{
			if (auto s = m_sessions.find(e->second); s != m_sessions.end() && !s->second.connection->profile().tgePackets)
				s->second.connection->setGhost(avatarKey(playerId), it->second);
		}
		return true;
	}

	void Service::onPlayerLeft(PlayerID player)
	{
		if (auto it = m_byPlayer.find(player); it != m_byPlayer.end())
			closeSession(it->second, "You have been disconnected from the game server.", true);
	}

	void Service::mirrorPacket(PlayerID player, const CString& packet)
	{
		if (packet.isEmpty())
			return;
		auto it = m_byPlayer.find(player);
		if (it == m_byPlayer.end())
			return;
		auto s = m_sessions.find(it->second);
		if (s == m_sessions.end())
			return;

		const auto id = static_cast<uint8_t>(packet[0]) - 32;
		if (id != PLO_FLAGSET && id != PLO_FLAGDEL)
			return;

		std::string body(packet.text() + 1, static_cast<size_t>(packet.length() - 1));
		while (!body.empty() && (body.back() == '\n' || body.back() == '\0'))
			body.pop_back();
		s->second.connection->sendText(TextMessage{"", id == PLO_FLAGSET ? "set" : "unset", "", body});
	}

	void Service::sendInitialFlags(PlayerID playerId, Connection& connection)
	{
		auto player = m_server.getPlayer(playerId);
		if (player == nullptr)
			return;

		// The same flags the TCP login sends, as one "events" text of `s` lines.
		std::string lines;
		for (const auto& [flag, value] : player->account.variables.store | variables::serializable)
		{
			if (auto serialized = player->account.variables.serializeModern(flag); serialized.has_value())
				lines += "s " + *serialized + "\n";
		}
		const bool npcServer = m_server.hasNPCServer();
		for (const auto& flag : m_server.Scripting.variables.store | std::views::keys)
		{
			if (npcServer && !flag.starts_with("serverr.")) continue;
			if (auto serialized = m_server.Scripting.variables.serializeModern(flag); serialized.has_value())
				lines += "s " + *serialized + "\n";
		}
		// Where the player's 3D object starts, for a script that moves it with
		// triggerserver (see moveAvatar).
		if (auto avatar = m_avatars.find(playerId); avatar != m_avatars.end())
		{
			const auto& p = avatar->second.object.transform.position;
			lines += std::format("s clientr.g3dpos={} {} {}\n", p[0], p[1], p[2]);
		}
		if (!lines.empty())
			connection.sendText(TextMessage{"", "events", "", lines});
	}

	std::string Service::compileClientScript(const std::string& name, std::string_view source)
	{
		const auto npcServer = m_server.getNPCServer();
		if (npcServer == nullptr)
			return {};

		// The file holds clientside code; drop a //#CLIENTSIDE line if it has one.
		if (const auto mark = source.find("//#CLIENTSIDE"); mark != std::string_view::npos)
		{
			const auto eol = source.find('\n', mark);
			source = eol == std::string_view::npos ? std::string_view{} : source.substr(eol + 1);
		}
		// Only GS2 compiles to bytecode, and a script without an engine line goes to
		// the default clientside engine, so ask for GS2 unless the file names one.
		std::string text{string::trim(source)};
		if (!text.starts_with("//#"))
			text = "//#GS2\n" + text;
		const auto compiled = npcServer->scripting.getCompiledClientScript(name, text);
		if (compiled == nullptr || compiled->script == nullptr || !compiled->script->has_value())
			return {};
		const auto* bytecode = std::any_cast<ScriptByteCode>(compiled->script.get());
		if (bytecode == nullptr)
			return {};
		return std::string(reinterpret_cast<const char*>(bytecode->data()), bytecode->size());
	}

	void Service::sendWorldWeapons(Connection& connection)
	{
		for (const auto& weapon : m_world.weapons)
		{
			std::string script;
			if (!weapon.file.empty())
			{
				CString text;
				if (text.load(CString() << weapon.file.c_str()))
					script.assign(text.text(), static_cast<size_t>(text.length()));
				// LF only: 5.12 ran nothing from a weapon3d saved with CRLF line ends
				// (lab, 2026-11-25).
				std::erase(script, '\r');
				// The 5.12 client runs only compiled GS2 here (lab: source text was
				// taken but never ran), so compile it when the NPC-server can.
				if (!script.empty())
				{
					if (auto bytecode = compileClientScript(weapon.name, script); !bytecode.empty())
						script = std::move(bytecode);
					else
						log::printLine(log::server, "** [Warning] Graal3D: weapon3d {}: not compiled (needs serverside = true, or a compile error); sending the source", weapon.name);
				}
			}
			else if (const auto w = m_server.getWeapon(weapon.name); w != nullptr)
			{
				if (const auto& bytecode = w->getScript().getClientSideByteCode(); !bytecode.empty())
					script.assign(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
				else
					script = std::string(w->getScript().getClientSideScript());
			}
			if (script.empty())
			{
				log::printLine(log::server, "** [Error] Graal3D: weapon3d {}: no script ({})", weapon.name,
							   weapon.file.empty() ? "no server weapon of that name" : "cannot read " + weapon.file);
				continue;
			}
			// The client reads characters 1 to 5 of addweapon's data as a five-GCHAR
			// number; all spaces read as 0 however they are counted.
			connection.sendText(TextMessage{"", "addweapon", weapon.name, std::string(6, ' ')});
			connection.sendText(TextMessage{"", "weaponscript", weapon.name, script});
		}
	}

	void Service::handleText(PlayerID playerId, const TextMessage& message)
	{
		auto player = m_server.getPlayer<PlayerClient>(playerId);
		if (player == nullptr)
			return;

		if (!message.weapon.empty() || message.type != "events")
		{
			log::printLine(log::server, "Graal3D {}: text ignored ({}, {}, {}).", player->account.name, message.weapon, message.type, message.option);
			return;
		}

		std::string_view rest = message.data;
		while (!rest.empty())
		{
			const auto end = rest.find('\n');
			std::string_view line = rest.substr(0, end);
			rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			if (line.size() < 3 || line[1] != ' ')
				continue;

			const std::string arg{line.substr(2)};
			switch (line[0])
			{
				case 's':
				{
					CString packet = CString() >> (char)PLI_FLAGSET << arg;
					packet.setRead(1);
					player->msgPLI_FLAGSET(packet);
					break;
				}
				case 'w':
					player->addWeapon(std::string_view{arg});
					break;
				case 't':
				{
					// triggerserver(<kind>, <weapon>, "g3dpos", x, y, z) from a 3D-mode script
					// moves the player's own object (see moveAvatar).
					if (const auto fields = string::fromCSV(arg); fields.size() == 6 && fields[2] == "g3dpos")
					{
						if (!moveAvatar(playerId, fields[3] + " " + fields[4] + " " + fields[5]))
							log::printLine(log::server, "Graal3D {}: g3dpos not moved ({}).", player->account.name, arg);
						break;
					}
					// `t weapon,<name>,<params>` is a serverside triggeraction from a weapon.
					if (!arg.starts_with("weapon,"))
					{
						log::printLine(log::server, "Graal3D {}: trigger ignored ({}).", player->account.name, arg);
						break;
					}
					CString packet = CString() >> (char)PLI_TRIGGERACTION;
					packet.writeGInt(0);
					packet.writeGChar(0);
					packet.writeGChar(0);
					packet << "serverside," << arg.substr(7);
					packet.setRead(1);
					player->msgPLI_TRIGGERACTION(packet);
					break;
				}
				default:
					break;
			}
		}
	}

} // namespace graal3d
