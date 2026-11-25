// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the add-ons a server runs, and the list of add-ons compiled in.
// See addons/ServerAddon.h and docs/addons.md.

#ifndef SEIHAI_ADDONS_SERVERADDONS_H
#define SEIHAI_ADDONS_SERVERADDONS_H

#include <optional>
#include <string_view>
#include <vector>

#include <addons/ServerAddon.h>

namespace preagonal
{
	class ServerAddons
	{
	public:
		/// Makes every compiled-in add-on whose server option is on, and starts it.
		/// An add-on that fails to start is dropped. Safe to call more than once:
		/// add-ons already running are kept.
		void start(Server& server, std::string_view interfaceIp);
		/// Stops and drops every add-on.
		void clear() { m_addons.clear(); }

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

		void onPacketToPlayer(PlayerID player, const CString& packet)
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
		std::vector<ServerAddonPtr> m_addons;
	};

} // namespace preagonal

#endif
