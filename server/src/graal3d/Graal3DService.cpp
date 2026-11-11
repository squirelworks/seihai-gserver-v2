// Modified by the Seihai project, 2026-11-11: see CHANGES-SEIHAI.md.
// New file: the gserver's Graal 3D UDP side. See Graal3DService.h.

#include <graal3d/Graal3DService.h>

#include <chrono>
#include <cstring>
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
#include <player/PlayerClient.h>
#include <scripting/ScriptContainers.h>
#include <utilities/Log.h>
#include <utilities/StringUtils.h>

using namespace preagonal;

namespace graal3d
{
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

		// A client build with a Torque 3D side: 3.153 to 5.x. (3.04 speaks the stock
		// TGE demo protocol and is refused by the game string check.)
		bool hasGraal3D(int version)
		{
			return version >= CLVER_3_153 && version < CLVER_6_015;
		}
	} // namespace

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
				sendTo(from, buildConnectAccept(req->sequence, it->second.connection->profile()));
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
		const std::string& account = req->args[0];
		auto player = m_server.getPlayer(CString(account), PLTYPE_ANYCLIENT);
		if (player == nullptr || !player->isLoaded() || player->getSocket() == nullptr)
			return;

		const char* tcpIp = player->getSocket()->getRemoteIp();
		if (tcpIp == nullptr || plainIpv4(tcpIp) != ipString(from))
		{
			log::printLine(log::server, "Graal3D: {} asked for a 3D link from {}, but is logged in from another address.", account, ipString(from));
			sendTo(from, buildConnectReject(req->sequence, "CHR_INVALID"));
			return;
		}

		if (!hasGraal3D(player->getVersion()))
		{
			sendTo(from, buildConnectReject(req->sequence, "CHR_PROTOCOL"));
			return;
		}

		const auto* profile = profileForProtocol(req->currentProtocol);
		const PlayerID playerId = player->getId();

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
						sendInitialFlags(playerId, *s->second.connection);
				}
			}
			else if (state == MissionState::Done)
				log::printLine(log::server, "Graal3D: {} finished the mission start.", account);
		};

		Session session{from, playerId, std::make_unique<Connection>(*profile, req->sequence, now, std::move(cb))};
		m_sessions.emplace(from, std::move(session));
		m_byPlayer[playerId] = from;
		player->setGraal3DLinked(true);

		sendTo(from, buildConnectAccept(req->sequence, *profile));
		log::printLine(log::server, "Graal3D: {} linked from {} (protocol {}, {}).", account, ipString(from), profile->protocol, profile->clients);
	}

	void Service::closeSession(const Endpoint& endpoint, std::string_view reason, bool notifyClient)
	{
		auto it = m_sessions.find(endpoint);
		if (it == m_sessions.end())
			return;

		if (notifyClient)
			sendTo(endpoint, it->second.connection->disconnectPacket(reason));

		const PlayerID playerId = it->second.player;
		if (auto p = m_byPlayer.find(playerId); p != m_byPlayer.end() && p->second == endpoint)
		{
			m_byPlayer.erase(p);
			if (auto player = m_server.getPlayer(playerId))
				player->setGraal3DLinked(false);
		}
		log::printLine(log::server, "Graal3D: link from {} closed ({}).", ipString(endpoint), reason);
		m_sessions.erase(it);
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
		if (!lines.empty())
			connection.sendText(TextMessage{"", "events", "", lines});
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
					// `t weapon,<name>,<params>` is a serverside triggeraction from a weapon.
					if (!arg.starts_with("weapon,"))
						break;
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
