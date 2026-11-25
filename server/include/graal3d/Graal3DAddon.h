// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the Graal 3D side as a server add-on (addons/ServerAddon.h). Built
// with the CMake option GRAAL3D, run when the server option `graal3d` is true.
// Owns the Torque UDP service (Graal3DService.h) and answers the core's hooks:
// the UDP port and world at start, a poll each frame, closing a player's 3D link
// with its TCP session, mirroring flag packets to a linked player, listing the
// server at HQ level 4, and the 3.151 to 5.x clients' file and login needs.

#ifndef SEIHAI_GRAAL3D_GRAAL3DADDON_H
#define SEIHAI_GRAAL3D_GRAAL3DADDON_H

#include <memory>

#include <addons/ServerAddon.h>

namespace graal3d
{
	class Service;

	class Addon final : public preagonal::ServerAddon
	{
	public:
		explicit Addon(preagonal::Server& server);
		~Addon() override;

		/// The add-on when the server option `graal3d` is true, else nullptr.
		static preagonal::ServerAddonPtr create(preagonal::Server& server);

		[[nodiscard]] std::string_view name() const override { return "Graal3D"; }
		bool start(std::string_view interfaceIp) override;
		void update() override;
		void onPlayerLeft(preagonal::PlayerID player) override;
		void onPacketToPlayer(preagonal::PlayerID player, const CString& packet) override;
		[[nodiscard]] std::optional<int> listHqLevel() const override;
		[[nodiscard]] preagonal::AddonClientNeeds clientNeeds(int versionId) const override;

		[[nodiscard]] Service& service() { return *m_service; }

	private:
		preagonal::Server& m_server;
		std::unique_ptr<Service> m_service;
	};

} // namespace graal3d

#endif
