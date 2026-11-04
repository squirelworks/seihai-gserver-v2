// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: the kick text for a player an RC has just banned, kept apart from
// PlayerRC so it can be tested on its own.
#ifndef RCBANMESSAGE_H
#define RCBANMESSAGE_H

#include <string_view>

#include <CString.h>

namespace preagonal
{
	/// @brief Builds the PLO_DISCMESSAGE text for a player an RC has just banned.
	/// @param rcAccount The account name of the RC that set the ban.
	/// @param csvReason The ban reason as RC_PLAYERBANSET sends it: one CSV item per line.
	/// @return "<rc> has banned you.  Reason: <reason>", with the reason's lines joined by '\r'.
	inline CString rcBanDisconnectMessage(std::string_view rcAccount, const CString& csvReason)
	{
		// Each line of the RC's reason box is one CSV item. The client's message box
		// breaks lines at '\r'.
		// Upstream wrote replaceAll("", "\r") here. gs2lib's find() matches an empty
		// string at every position, so that never ended: the server spun at 100% CPU
		// and stopped sending to everyone whenever an online player was banned.
		CString reason = csvReason.isEmpty() ? CString() : csvReason.guntokenize();
		reason.replaceAllI("\n", "\r");
		return CString() << rcAccount << " has banned you.  Reason: " << reason;
	}
} // namespace preagonal

#endif // RCBANMESSAGE_H
