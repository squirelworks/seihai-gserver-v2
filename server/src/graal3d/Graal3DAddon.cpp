// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the Graal 3D add-on. See Graal3DAddon.h.

#include <graal3d/Graal3DAddon.h>

#include <array>
#include <cstdlib>
#include <string>

#include <Server.h>
#include <graal3d/Graal3DService.h>
#include <utilities/Log.h>

using namespace preagonal;
using namespace std::string_literals;

namespace graal3d
{
	Addon::Addon(Server& server)
		: m_server(server), m_service(std::make_unique<Service>(server))
	{
	}

	Addon::~Addon() = default;

	ServerAddonPtr Addon::create(Server& server)
	{
		if (!server.getSettings().get<bool>("graal3d").value_or(false))
			return nullptr;
		return std::make_unique<Addon>(server);
	}

	// A Torque UDP server on the listed port + 1 (or `graal3dport`), for the 3.151
	// to 5.x clients, and the world they are sent: config/graal3dworld.txt (see
	// bin/docs/graal3d.md).
	bool Addon::start(std::string_view interfaceIp)
	{
		auto& settings = m_server.getSettings();
		const int tcpPort = std::atoi(settings.get<std::string>("serverport").value_or("14802"s).c_str());
		int udpPort = settings.get<int>("graal3dport").value_or(0);
		if (udpPort <= 0)
			udpPort = tcpPort + 1;

		std::string worldText;
		for (const auto& line : CString::loadToken(CString() << "config/graal3dworld.txt", "\n", false))
			worldText.append(line.text(), static_cast<size_t>(line.length())).push_back('\n');
		auto world = parseWorld(worldText);
		for (const auto& error : world.errors)
			log::printLine(log::server, "** [Error] Graal3D: graal3dworld.txt {}", error);
		m_service->setWorld(std::move(world), settings.get<bool>("graal3dstringprefix").value_or(true));

		if (udpPort < 65536 && m_service->start(interfaceIp, static_cast<uint16_t>(udpPort)))
		{
			auto indent = log::server.indent();
			log::printLine(log::server, "Graal3D: listening on UDP port {}.", udpPort);
			return true;
		}
		log::printLine(log::server, "** [Error] Graal3D: could not open UDP port {}.", udpPort);
		return false;
	}

	void Addon::update()
	{
		m_service->update();
	}

	// A 3D link lives only as long as the TCP session.
	void Addon::onPlayerLeft(PlayerID player)
	{
		m_service->onPlayerLeft(player);
	}

	// A client in Graal 3D mode ignores flag packets on TCP; send them on its 3D link too.
	void Addon::onPacketToPlayer(PlayerID player, const CString& packet)
	{
		m_service->mirrorPacket(player, packet);
	}

	// A 3D server is listed at HQ level 4, which the listserver tags "3 ". The
	// 3.153 to 5.x clients open the 3D link on that tag alone, and Mac 3.153 reads
	// only the first tag, so a server left at the default bronze level ("H ") is
	// not seen as 3D even with "3 " in its name.
	std::optional<int> Addon::listHqLevel() const
	{
		return 4;
	}

	AddonClientNeeds Addon::clientNeeds(int versionId) const
	{
		// The 5.x 3D client asks for Torque textures (a Sky's, for one) without an
		// extension, and stores what it gets under the name it asked for.
		static constexpr std::array<const char*, 6> textureExtensions{".png", ".jpg", ".jpeg", ".bmp", ".gif", ".dds"};

		if (!Service::clientHasGraal3D(versionId))
			return {};

		AddonClientNeeds needs;
		// The 3D login waits until every file it asked for is answered.
		needs.answerMissingChecksumFiles = true;
		// A 5.x client joining a 3D server keeps its "Connecting..." window over
		// the 3D view until it gets PLO_SERVERLISTCONNECTED.
		needs.alwaysServerListConnected = true;
		needs.bareNameExtensions = textureExtensions;
		return needs;
	}

} // namespace graal3d
