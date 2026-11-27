// Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: the Graal 3D add-on's log lines go through the add-on host
// (addons/AddonHost.h), so the same code logs to the server's log when built in
// and when built as a module.

#ifndef SEIHAI_GRAAL3D_GRAAL3DLOG_H
#define SEIHAI_GRAAL3D_GRAAL3DLOG_H

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace preagonal
{
	class AddonHost;
}

namespace graal3d
{
	/// Where logLine writes; set by the Service. With none, lines are dropped.
	void setLogHost(preagonal::AddonHost* host);
	void logText(std::string_view text);

	template<typename... Args>
	void logLine(std::format_string<Args...> fmt, Args&&... args)
	{
		logText(std::format(fmt, std::forward<Args>(args)...));
	}

} // namespace graal3d

#endif
