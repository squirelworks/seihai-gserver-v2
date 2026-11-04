// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: the folder rights text sent to an RC in PLO_RC_PLAYERRIGHTSGET, kept
// apart from PlayerRC so it can be tested on its own.
#ifndef RCFOLDERRIGHTS_H
#define RCFOLDERRIGHTS_H

#include <string>
#include <vector>

#include <CString.h>

namespace preagonal
{
	/// @brief Encodes an account's folder rights the way the RC sends them in RC_PLAYERRIGHTSSET.
	/// @param folders One entry per line, such as "rw levels/*".
	/// @return Graal's tokenized text: entries joined by commas, each quoted when it holds a
	///         space, a comma, a slash or a byte outside 33..126. Empty when there are no folders.
	inline std::string rcFolderRightsText(const std::vector<std::string>& folders)
	{
		// string::toCSV only quotes on '"', ',' and '\', so "rw levels/*" went out bare.
		// The real RC then splits the reply at the space and shows two lines ("rw" and
		// "levels/*"), and saving from it stores the wrong rights. gtokenize quotes the
		// way the RC does.
		if (folders.empty())
			return {};

		CString lines;
		for (const auto& folder : folders)
			lines << folder << "\n";
		return lines.gtokenize().toString();
	}
} // namespace preagonal

#endif // RCFOLDERRIGHTS_H
