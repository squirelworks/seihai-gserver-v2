// Modified by the Seihai project, 2026-10-31: see CHANGES-SEIHAI.md.
//
// macOS file watcher. Upstream left this file empty and macOS compiled the
// Linux (inotify) watcher instead, which cannot build there.
//
// For now this is a no-op: the server runs, but files changed on disk while it
// is running are not picked up until a restart (or an RC/NC command reloads them).
// TODO(seihai): implement with kqueue (EVFILT_VNODE per directory) or FSEvents,
// mirroring the event fix-ups in FileWatchOS_Linux.cpp. See docs/SEIHAI-BUILD.md.
#ifdef __APPLE__

#include <vector>

#include <filesystem/watch/FileWatch.h>
#include <utilities/Log.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::fs::watch
{
///////////////////////////////////////////////////////////////////////////////

struct Watch
{
	uint32_t watch_id = 0;
	std::filesystem::path dir;
	watch_cb callback;
};

struct WatchOS
{
};

FileWatch::FileWatch()
	: m_last_id(0)
{
	m_watch_os = std::make_unique<WatchOS>();
}

FileWatch::~FileWatch()
{
	removeAll();
}

uint32_t FileWatch::add(const std::filesystem::path& directory, watch_cb callback, const bool /*recursive*/)
{
	DEBUGPRINT("[FS] File watching is not implemented on macOS yet: {}", directory.string());

	auto watch = new Watch;
	watch->watch_id = ++m_last_id;
	watch->dir = directory;
	watch->callback = std::move(callback);
	m_watchers.insert(std::make_pair(watch->watch_id, watch));
	return watch->watch_id;
}

void FileWatch::remove(const std::filesystem::path& directory)
{
	std::vector<uint32_t> ids;
	for (const auto& [id, watch] : m_watchers)
	{
		if (watch->dir == directory)
			ids.push_back(id);
	}
	for (const auto id : ids)
		remove(id);
}

void FileWatch::remove(const uint32_t watch_id)
{
	if (const auto i = m_watchers.find(watch_id); i != m_watchers.end())
	{
		delete i->second;
		m_watchers.erase(i);
	}
}

void FileWatch::removeAll()
{
	for (const auto& [id, watch] : m_watchers)
		delete watch;
	m_watchers.clear();
}

void FileWatch::update()
{
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal::fs::watch

#endif // __APPLE__
