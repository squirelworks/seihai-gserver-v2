// Seihai project, 2026-11-25, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the add-ons a server runs, and the list of add-ons compiled in.
// See addons/ServerAddon.h and docs/addons.md.

#ifndef SEIHAI_ADDONS_SERVERADDONS_H
#define SEIHAI_ADDONS_SERVERADDONS_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <addons/AddonHost.h>
#include <addons/ServerAddon.h>

namespace preagonal
{
	class ServerAddons
	{
	public:
		ServerAddons();
		~ServerAddons();
		ServerAddons(const ServerAddons&) = delete;
		ServerAddons& operator=(const ServerAddons&) = delete;

		/// Makes every compiled-in add-on whose server option is on, and starts it.
		/// An add-on whose option is on but which is not compiled in is loaded from
		/// its module, if one is found (addons/AddonModule.h). An add-on that fails
		/// to start is dropped. Safe to call more than once: add-ons already
		/// running are kept.
		void start(Server& server, std::string_view interfaceIp);
		/// Stops and drops every add-on, then unloads their modules.
		void clear();

		/// The core's side of the add-on API for this server, made on first use.
		AddonHost& host(Server& server);

		/// The names of the running add-ons that came from modules.
		[[nodiscard]] std::vector<std::string> moduleNames() const;

		[[nodiscard]] bool empty() const { return m_addons.empty(); }

		void update()
		{
			for (auto& a : m_addons)
				a->update();
		}

		void onPlayerLeft(PlayerID player)
		{
			for (auto& a : m_addons)
				a->onPlayerLeft(player);
		}

		void onPacketToPlayer(PlayerID player, std::string_view packet)
		{
			for (auto& a : m_addons)
				a->onPacketToPlayer(player, packet);
		}

		[[nodiscard]] std::optional<int> listHqLevel() const
		{
			for (const auto& a : m_addons)
				if (auto level = a->listHqLevel(); level.has_value())
					return level;
			return std::nullopt;
		}

		[[nodiscard]] AddonClientNeeds clientNeeds(int versionId) const
		{
			AddonClientNeeds needs;
			for (const auto& a : m_addons)
				needs |= a->clientNeeds(versionId);
			return needs;
		}

		/// The running add-on of type T, or nullptr.
		template<typename T>
		[[nodiscard]] T* find() const
		{
			for (const auto& a : m_addons)
				if (auto* t = dynamic_cast<T*>(a.get()); t != nullptr)
					return t;
			return nullptr;
		}

	private:
		struct Module;
		bool running(std::string_view name) const;
		void loadModule(Server& server, std::string_view name, std::string_view interfaceIp);

		// Destroyed in reverse order: the add-ons, then their modules, then the host.
		std::unique_ptr<AddonHost> m_host;
		std::vector<std::shared_ptr<Module>> m_modules;
		std::vector<ServerAddonPtr> m_addons;
	};

} // namespace preagonal

#endif
