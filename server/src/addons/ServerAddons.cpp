// Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the list of add-ons compiled in. To add one: give it a CMake option
// that defines SEIHAI_ADDON_<NAME> and adds its sources, and a create() that
// returns nullptr unless its server option is on, then list it below.

#include <addons/ServerAddons.h>

#include <Server.h>
#include <utilities/Log.h>

#ifdef SEIHAI_ADDON_GRAAL3D
	#include <graal3d/Graal3DAddon.h>
#endif

namespace preagonal
{
	namespace
	{
		template<typename T>
		void startAddon(std::vector<ServerAddonPtr>& running, Server& server, std::string_view interfaceIp)
		{
			for (const auto& a : running)
				if (dynamic_cast<T*>(a.get()) != nullptr)
					return;

			auto addon = T::create(server);
			if (addon == nullptr)
				return;
			if (!addon->start(interfaceIp))
			{
				log::printLine(log::server, "** [Error] Add-on {} did not start; running without it.", addon->name());
				return;
			}
			running.push_back(std::move(addon));
		}
	} // namespace

	void ServerAddons::start([[maybe_unused]] Server& server, [[maybe_unused]] std::string_view interfaceIp)
	{
#ifdef SEIHAI_ADDON_GRAAL3D
		startAddon<graal3d::Addon>(m_addons, server, interfaceIp);
#endif
	}

} // namespace preagonal
