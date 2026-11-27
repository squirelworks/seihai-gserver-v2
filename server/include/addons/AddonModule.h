// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: an add-on built as its own module (a shared library), which the
// server loads at start instead of having the add-on compiled in. See
// docs/addons.md, "Add-on modules".
//
// A module exports three C functions:
//
//   const SeihaiAddonModuleInfo* seihai_addon_module_info();
//   preagonal::ServerAddon*      seihai_addon_module_create(preagonal::AddonHost* host);
//   void                         seihai_addon_module_destroy(preagonal::ServerAddon* addon);
//
// The info is read first, and the module is used only if its API version, its
// toolchain and its client version table match the server's. The hooks and the
// host are C++ classes, so the module and the server must be built with the same
// kind of compiler and standard library (the toolchain string).

#ifndef SEIHAI_ADDONS_ADDONMODULE_H
#define SEIHAI_ADDONS_ADDONMODULE_H

#include <cstdint>
#include <string>

#include <IUtil.h>

#if defined(_WIN32)
	#define SEIHAI_ADDON_EXPORT extern "C" __declspec(dllexport)
#else
	#define SEIHAI_ADDON_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace preagonal
{
	class AddonHost;
	class ServerAddon;

	/// The add-on API: ServerAddon.h, AddonHost.h and this file. A server loads
	/// only modules built for the same number.
	inline constexpr uint32_t kAddonApiVersion = 1;

	/// "SHAI" in the first field of the info.
	inline constexpr uint32_t kAddonModuleMagic = 0x49414853u;

	/// The C++ ABI the hooks cross: compiler family, standard library and
	/// platform. Two builds with the same string can share C++ classes.
	inline std::string addonToolchain()
	{
		std::string s;
#if defined(_MSC_VER) && !defined(__clang__)
		s += "msvc";
#elif defined(__clang__)
		s += "clang";
#elif defined(__GNUC__)
		s += "gcc";
#else
		s += "cxx";
#endif
#if defined(_LIBCPP_VERSION)
		s += "/libc++";
#elif defined(__GLIBCXX__)
		s += "/libstdc++";
#elif defined(_MSVC_STL_VERSION)
		s += "/msvcstl";
#endif
#if defined(_WIN32)
		s += "/windows";
#elif defined(__APPLE__)
		s += "/macos";
#elif defined(__linux__)
		s += "/linux";
#endif
#if defined(__x86_64__) || defined(_M_X64)
		s += "/x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
		s += "/arm64";
#endif
#if defined(_DEBUG)
		s += "/debug-crt";
#endif
		return s;
	}

	/// The version ids (CLVER_*) the add-ons look at, folded into one number.
	/// The core hands add-ons gs2lib version ids, so a module built against a
	/// gs2lib that numbers them differently would misread every client.
	inline constexpr uint32_t addonVersionTableFingerprint()
	{
		constexpr int ids[] = {
			CLVER_3_151_LINUX, CLVER_3_153, CLVER_4_208, CLVER_6_015, CLVER_6_037, CLVER_6_037_LINUX, CLVER_WORLDS,
		};
		uint32_t h = 2166136261u;
		for (const int id : ids)
			h = (h ^ static_cast<uint32_t>(id)) * 16777619u;
		return h;
	}

	/// What a module says about itself. Plain C layout: read before anything else.
	struct SeihaiAddonModuleInfo
	{
		uint32_t magic;        // kAddonModuleMagic
		uint32_t apiVersion;   // kAddonApiVersion it was built for
		const char* name;      // "graal3d": matches the server option that loads it
		const char* version;   // the module's own version (graal3d-v<version> tags)
		const char* toolchain; // addonToolchain() of the module's build
		uint32_t versionTable; // addonVersionTableFingerprint() of the module's build
		const char* builtFrom; // the gserver-v2 commit it was built from, for logs
		const char* protocol;  // what it speaks, for logs ("Torque UDP 19/21/24; seihai3d none")
	};

	using SeihaiAddonModuleInfoFn = const SeihaiAddonModuleInfo* (*)();
	using SeihaiAddonModuleCreateFn = ServerAddon* (*)(AddonHost*);
	using SeihaiAddonModuleDestroyFn = void (*)(ServerAddon*);

} // namespace preagonal

#endif
