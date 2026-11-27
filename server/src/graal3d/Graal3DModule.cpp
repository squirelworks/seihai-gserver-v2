// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the Graal 3D add-on as a module (the seihai-graal3d shared library),
// for a gserver built without it (-DGRAAL3D=OFF). The server loads it at start
// when `graal3d = true` (addons/AddonModule.h, docs/addons.md).

#include <addons/AddonModule.h>
#include <graal3d/Graal3DAddon.h>

#ifndef SEIHAI_GRAAL3D_MODULE_VERSION
	#define SEIHAI_GRAAL3D_MODULE_VERSION "0.0.0-dev"
#endif
#ifndef SEIHAI_GRAAL3D_BUILT_FROM
	#define SEIHAI_GRAAL3D_BUILT_FROM "unknown"
#endif

namespace
{
	const std::string& toolchain()
	{
		static const std::string s = preagonal::addonToolchain();
		return s;
	}
} // namespace

SEIHAI_ADDON_EXPORT const preagonal::SeihaiAddonModuleInfo* seihai_addon_module_info()
{
	static const preagonal::SeihaiAddonModuleInfo info{
		preagonal::kAddonModuleMagic,
		preagonal::kAddonApiVersion,
		"graal3d",
		SEIHAI_GRAAL3D_MODULE_VERSION,
		toolchain().c_str(),
		preagonal::addonVersionTableFingerprint(),
		SEIHAI_GRAAL3D_BUILT_FROM,
		"Torque UDP protocols 19, 21 and 24 (3.151 to 5.x); seihai3d: none",
	};
	return &info;
}

SEIHAI_ADDON_EXPORT preagonal::ServerAddon* seihai_addon_module_create(preagonal::AddonHost* host)
{
	if (host == nullptr)
		return nullptr;
	return graal3d::Addon::create(*host).release();
}

SEIHAI_ADDON_EXPORT void seihai_addon_module_destroy(preagonal::ServerAddon* addon)
{
	delete addon;
}
