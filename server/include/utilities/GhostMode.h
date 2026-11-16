// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
//
// Ghost-mode appearance policy for ghostmodeenabled / ghostmodefornotstaff.
#ifndef GHOSTMODE_H
#define GHOSTMODE_H

#include <string_view>

////////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
////////////////////////////////////////////////////////////////////////////////

/// @brief Default gani name (no extension) used when a player should appear as a ghost.
inline constexpr std::string_view GhostAppearanceGani = "ghostani";

/// @brief Corner text sent with PLO_GHOSTTEXT while the client is in ghost mode.
inline constexpr std::string_view GhostModeCornerText = "Ghost Mode";

/// @brief Stock-style matrix: nobody / only non-staff / everybody appears as a ghost.
///
/// | ghostmodeenabled | ghostmodefornotstaff | Who appears as ghost |
/// |---|---|---|
/// | false | * | nobody |
/// | true | true | non-staff only |
/// | true | false | everybody (staff included) |
[[nodiscard]] constexpr bool playerAppearsAsGhost(
	const bool ghostModeEnabled,
	const bool ghostModeForNonStaff,
	const bool isStaff) noexcept
{
	if (!ghostModeEnabled)
		return false;
	if (ghostModeForNonStaff)
		return !isStaff;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // GHOSTMODE_H
