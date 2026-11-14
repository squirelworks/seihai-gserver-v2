// Modified by the Seihai project, 2026-11-15: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// One chunk of a file, in the form the file queue should carry it.
// Clients before 2.1 keep every byte the RAWDATA count names, and their FILE
// body has no modtime and no trailing line feed. The queue counts one extra
// line feed on a raw packet, so those clients get a normal packet whose
// RAWDATA count is the FILE body alone. From 2.1 the body includes the
// modtime, and the queue's counted line feed is the trailing line feed that
// body ends with.

#ifndef FILESENDLAYOUT_H
#define FILESENDLAYOUT_H

#include <string>
#include <string_view>

#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

struct QueuedFileChunk
{
	/// True: queue it with addRawPacket, which counts a trailing line feed.
	/// False: queue it with addPacket. The bytes are already a RAWDATA envelope.
	bool raw = false;
	CString packet;
};

inline QueuedFileChunk queueFileChunk(const int versionId, const bool client, const long long modTime, const std::string_view filename, const std::string_view data)
{
	CString body;
	body >> (char)PLO_FILE;
	// Below 2.1, including an unknown token (version 0), the body is the name
	// and the bytes. From 2.1 it starts with the modtime.
	const bool before21 = client && versionId < CLVER_2_1;
	if (!before21)
		body >> modTime;
	body >> (char)filename.length() << std::string(filename) << std::string(data);

	if (!before21)
		return QueuedFileChunk{true, body};

	CString envelope;
	envelope >> (char)PLO_RAWDATA >> (int)body.length() << '\n' << body;
	return QueuedFileChunk{false, envelope};
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // FILESENDLAYOUT_H
