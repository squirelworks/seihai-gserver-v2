// Seihai project, 2026-11-25, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the list of add-ons compiled in, and the loader for add-ons built as
// modules. To add one: give it a CMake option that defines SEIHAI_ADDON_<NAME>
// and adds its sources, and a create() that returns nullptr unless its server
// option is on, then list it below and in kModuleAddons.

#include <addons/ServerAddons.h>

#include <algorithm>
#include <filesystem>
#include <format>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

#include <Server.h>
#include <addons/AddonModule.h>
#include <addons/ServerAddonHost.h>
#include <utilities/Log.h>

#ifdef SEIHAI_ADDON_GRAAL3D
	#include <graal3d/Graal3DAddon.h>
#endif

namespace preagonal
{
	namespace
	{
		/// Add-ons that may come from a module: the name is the server option that
		/// turns it on and the module's file name (seihai-<name>).
		constexpr std::string_view kModuleAddons[] = {"graal3d"};

		template<typename T>
		void startAddon(std::vector<ServerAddonPtr>& running, AddonHost& host, std::string_view interfaceIp)
		{
			for (const auto& a : running)
				if (dynamic_cast<T*>(a.get()) != nullptr)
					return;

			auto addon = T::create(host);
			if (addon == nullptr)
				return;
			if (!addon->start(interfaceIp))
			{
				log::printLine(log::server, "** [Error] Add-on {} did not start; running without it.", addon->name());
				return;
			}
			running.push_back(std::move(addon));
		}

		std::string lower(std::string_view s)
		{
			std::string out(s);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::filesystem::path executableFolder()
		{
#ifdef _WIN32
			wchar_t buf[MAX_PATH];
			const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
			if (n > 0 && n < MAX_PATH)
				return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
			std::error_code ec;
			if (auto self = std::filesystem::read_symlink("/proc/self/exe", ec); !ec)
				return self.parent_path();
#endif
			return std::filesystem::current_path();
		}

		std::string moduleFileName(std::string_view name)
		{
#if defined(_WIN32)
			return std::format("seihai-{}.dll", name);
#elif defined(__APPLE__)
			return std::format("libseihai-{}.dylib", name);
#else
			return std::format("libseihai-{}.so", name);
#endif
		}
	} // namespace

	/// A loaded module: its library handle and entry points. Unloaded when the
	/// last add-on made from it is gone (ServerAddons destroys add-ons first).
	struct ServerAddons::Module
	{
		std::string name;
		std::filesystem::path path;
		void* handle = nullptr;
		SeihaiAddonModuleDestroyFn destroy = nullptr;

		~Module()
		{
			if (handle == nullptr)
				return;
#ifdef _WIN32
			FreeLibrary(static_cast<HMODULE>(handle));
#else
			dlclose(handle);
#endif
		}

		void* symbol(const char* sym) const
		{
#ifdef _WIN32
			return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), sym));
#else
			return dlsym(handle, sym);
#endif
		}
	};

	ServerAddons::ServerAddons() = default;

	ServerAddons::~ServerAddons()
	{
		clear();
	}

	void ServerAddons::clear()
	{
		// An add-on from a module must go before its module is unloaded, and the
		// host it was given after both.
		m_addons.clear();
		m_modules.clear();
	}

	AddonHost& ServerAddons::host(Server& server)
	{
		if (m_host == nullptr)
			m_host = std::make_unique<ServerAddonHost>(server);
		return *m_host;
	}

	std::vector<std::string> ServerAddons::moduleNames() const
	{
		std::vector<std::string> names;
		for (const auto& m : m_modules)
			names.push_back(m->name);
		return names;
	}

	bool ServerAddons::running(std::string_view name) const
	{
		const auto want = lower(name);
		return std::ranges::any_of(m_addons, [&](const ServerAddonPtr& a) { return lower(a->name()) == want; });
	}

	void ServerAddons::start(Server& server, std::string_view interfaceIp)
	{
		auto& h = host(server);
#ifdef SEIHAI_ADDON_GRAAL3D
		startAddon<graal3d::Addon>(m_addons, h, interfaceIp);
#endif
		// Add-ons not compiled in come from their modules, as an NPC-server is
		// started beside the gserver: `<name> = true` turns it on, and the module
		// is `<name>module` (a path), else seihai-<name> in the server's folder,
		// its addons/ folder or the program's folder.
		for (const auto name : kModuleAddons)
		{
			if (!h.settingBool(name).value_or(false) || running(name))
				continue;
			loadModule(server, name, interfaceIp);
		}
	}

	void ServerAddons::loadModule(Server& server, std::string_view name, std::string_view interfaceIp)
	{
		auto& h = host(server);
		const auto option = std::string(name) + "module";
		const auto setting = h.settingString(option).value_or("");
		if (lower(setting) == "off")
		{
			log::printLine(log::server, "Add-on {}: {} = true, but {} = off; not loading a module. Running without it.", name, name, option);
			return;
		}

		std::vector<std::filesystem::path> candidates;
		if (!setting.empty() && lower(setting) != "auto")
			candidates.emplace_back(setting);
		else
		{
			const auto file = moduleFileName(name);
			const auto exe = executableFolder();
			candidates = {std::filesystem::path(file), std::filesystem::path("addons") / file, exe / file, exe / "addons" / file};
		}

		std::filesystem::path path;
		for (const auto& c : candidates)
		{
			std::error_code ec;
			if (std::filesystem::is_regular_file(c, ec))
			{
				path = std::filesystem::absolute(c, ec);
				break;
			}
		}
		if (path.empty())
		{
			log::printLine(log::server, "** [Error] Add-on {}: {} = true, but this server has no {} built in and no {} was found ({}). Running without it.", name, name,
						   name, moduleFileName(name), setting.empty() ? "set " + option + " to its path" : option + " = " + setting);
			return;
		}

		auto module = std::make_shared<Module>();
		module->name = std::string(name);
		module->path = path;
#ifdef _WIN32
		module->handle = LoadLibraryW(path.wstring().c_str());
		if (module->handle == nullptr)
		{
			log::printLine(log::server, "** [Error] Add-on {}: cannot load {} (error {}). Running without it.", name, path.string(), GetLastError());
			return;
		}
#else
		module->handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
		if (module->handle == nullptr)
		{
			const char* why = dlerror();
			log::printLine(log::server, "** [Error] Add-on {}: cannot load {} ({}). Running without it.", name, path.string(), why != nullptr ? why : "unknown");
			return;
		}
#endif

		const auto infoFn = reinterpret_cast<SeihaiAddonModuleInfoFn>(module->symbol("seihai_addon_module_info"));
		const auto createFn = reinterpret_cast<SeihaiAddonModuleCreateFn>(module->symbol("seihai_addon_module_create"));
		module->destroy = reinterpret_cast<SeihaiAddonModuleDestroyFn>(module->symbol("seihai_addon_module_destroy"));
		const SeihaiAddonModuleInfo* info = infoFn != nullptr ? infoFn() : nullptr;
		if (info == nullptr || createFn == nullptr || module->destroy == nullptr || info->magic != kAddonModuleMagic)
		{
			log::printLine(log::server, "** [Error] Add-on {}: {} is not a Seihai add-on module. Running without it.", name, path.string());
			return;
		}

		const auto str = [](const char* s) { return s != nullptr ? std::string(s) : std::string("?"); };
		const auto toolchain = addonToolchain();
		std::string refuse;
		if (lower(str(info->name)) != lower(name))
			refuse = std::format("it is the {} module", str(info->name));
		else if (info->apiVersion != kAddonApiVersion)
			refuse = std::format("it was built for add-on API {}, this server has API {}", info->apiVersion, kAddonApiVersion);
		else if (str(info->toolchain) != toolchain)
			refuse = std::format("it was built with {}, this server with {}", str(info->toolchain), toolchain);
		else if (info->versionTable != addonVersionTableFingerprint())
			refuse = "it numbers client versions differently (built against another gs2lib)";
		if (!refuse.empty())
		{
			log::printLine(log::server, "** [Error] Add-on {}: {} not loaded: {}. Rebuild the module against this server, or use one from the same release. Running without it.", name,
						   path.string(), refuse);
			return;
		}

		ServerAddon* raw = createFn(&h);
		if (raw == nullptr)
			return;
		// The add-on is destroyed by the module that made it, and keeps the module loaded.
		ServerAddonPtr addon(raw);
		struct Holder final : ServerAddon
		{
			std::shared_ptr<Module> module;
			ServerAddon* inner;
			Holder(std::shared_ptr<Module> m, ServerAddon* a) : module(std::move(m)), inner(a) {}
			~Holder() override { module->destroy(inner); }
			std::string_view name() const override { return inner->name(); }
			bool start(std::string_view ip) override { return inner->start(ip); }
			void update() override { inner->update(); }
			void onPlayerLeft(PlayerID id) override { inner->onPlayerLeft(id); }
			void onPacketToPlayer(PlayerID id, std::string_view packet) override { inner->onPacketToPlayer(id, packet); }
			std::optional<int> listHqLevel() const override { return inner->listHqLevel(); }
			AddonClientNeeds clientNeeds(int versionId) const override { return inner->clientNeeds(versionId); }
		};
		auto holder = std::make_unique<Holder>(module, addon.release());
		log::printLine(log::server, "Add-on {}: module {} {} (add-on API {}, {}; {}; built from {}).", name, path.filename().string(), str(info->version), info->apiVersion,
					   toolchain, str(info->protocol), str(info->builtFrom));
		if (!holder->start(interfaceIp))
		{
			log::printLine(log::server, "** [Error] Add-on {} did not start; running without it.", holder->name());
			return;
		}
		m_modules.push_back(module);
		m_addons.push_back(std::move(holder));
	}

} // namespace preagonal
