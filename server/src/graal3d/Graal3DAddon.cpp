// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the Graal 3D add-on. See Graal3DAddon.h.

#include <graal3d/Graal3DAddon.h>

#include <cstdlib>
#include <string>

#include <Server.h>
#include <graal3d/Graal3DObjects.h>
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
		// graal3dshapecheck = skip (default), warn or off: what to do with a world
		// object whose .3ds would crash a client (see bin/docs/graal3d.md).
		const auto shapeCheck = settings.get<std::string>("graal3dshapecheck").value_or("skip"s);
		m_service->setShapeCheck(shapeCheck == "warn" ? Service::ShapeCheck::Warn
								 : shapeCheck == "off" ? Service::ShapeCheck::Off
													   : Service::ShapeCheck::Skip);
		if (shapeCheck != "skip" && shapeCheck != "warn" && shapeCheck != "off")
			log::printLine(log::server, "** [Error] Graal3D: graal3dshapecheck {} is not skip, warn or off; using skip.", shapeCheck);
		// graal3dtrace = <file>: a line for everything each 3D client sends (see bin/docs/graal3d.md).
		m_service->setTrace(settings.get<std::string>("graal3dtrace").value_or(""s));
		// graal3dmutate = <kind>[:<seed>]: lab fuzzing only (see bin/docs/graal3d.md).
		m_service->setMutation(settings.get<std::string>("graal3dmutate").value_or(""s));
		// graal3dterrainall, graal3dpushterrain: lab switches for streamed terrain (see bin/docs/graal3d.md).
		m_service->setTerrainForAll(settings.get<bool>("graal3dterrainall").value_or(false));
		m_service->setPushTerrainFiles(settings.get<bool>("graal3dpushterrain").value_or(false));
		m_service->setWorld(std::move(world), settings.get<bool>("graal3dstringprefix").value_or(true));
		// graal3droute = sharp3d: the route of a server real 5.12 joined (see bin/docs/graal3d.md).
		const auto route = settings.get<std::string>("graal3droute").value_or("mission"s);
		m_service->setSharp3DRoute(route == "sharp3d");
		if (route != "mission" && route != "sharp3d")
			log::printLine(log::server, "** [Error] Graal3D: graal3droute {} is not mission or sharp3d; using mission.", route);
		else if (route == "sharp3d")
			log::printLine(log::server, "Graal3D: Sharp3D route for protocol 24 clients.");

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

		if (!Service::clientHasGraal3D(versionId))
			return {};

		AddonClientNeeds needs;
		// The 3D login waits until every file it asked for is answered.
		needs.answerMissingChecksumFiles = true;
		// A 5.x client joining a 3D server keeps its "Connecting..." window over
		// the 3D view until it gets PLO_SERVERLISTCONNECTED.
		needs.alwaysServerListConnected = true;
		// The 5.x 3D client asks for every texture by its bare, lower-cased name and
		// stores what it gets under that name.
		needs.bareNameExtensions = kImageExtensions;
		return needs;
	}

} // namespace graal3d
