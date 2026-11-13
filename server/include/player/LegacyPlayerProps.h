// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// Player props for the 1.x clients (1.20 to 1.41r1) on a server that is not
// `generation = classic`. Such a server writes props in the 2.x forms, which a
// 1.x client reads only up to the first prop it does not know: prop 10 as a
// gani string, BODYIMAGE (35) or any later id ends its parse, and every prop
// after it, X and Y included, is lost (five-client lab, #27).

#ifndef LEGACYPLAYERPROPS_H
#define LEGACYPLAYERPROPS_H

#include <set>
#include <string_view>

#include <CString.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::legacy
{
///////////////////////////////////////////////////////////////////////////////

/// @brief True for the client versions before 2.1 (1.20 to 1.41r1).
bool isPre21Client(int versionId);

/// @brief Whether a 1.x client can read prop id `propId`.
/// @param forOtherPlayer true for PLO_OTHERPLPROPS (another player), false for PLO_PLAYERPROPS (itself).
bool pre21KnowsProp(int versionId, int propId, bool forOtherPlayer);

/// @brief Rewrite a props stream (prop id, value, prop id, value, ...) written in
/// the 2.x forms into what a 1.x client reads. Props the client does not know are
/// dropped and the rest keep their order. Prop 10 (a gani for 2.x) is dropped.
/// A sword, shield or head that is a stock image (`sword1.png`, `head0.png`) goes
/// as its preset number, so the client draws its own built-in GIF; any other
/// `.png` image name becomes `.gif`. Colors keep the first five.
/// Status (18) loses bit 4 (PLSTATUS_MALE) unless `fromPre21Client`: from 2.0
/// on that bit is "male", but a 1.x client reads it as "swimming" and draws
/// the player as a head only (#27 rerun, 2026-11-13). When the props are those
/// of a 1.x player, the bit is theirs and means swimming, so it is kept.
CString rewritePropsForPre21(const CString& props, int versionId, bool forOtherPlayer, const std::set<int>& alsoDrop = {}, bool fromPre21Client = false);

/// @brief Rewrite a whole PLO_PLAYERPROPS or PLO_OTHERPLPROPS packet for a 1.x
/// client. Any other packet is returned unchanged.
/// `fromPre21Client` is as for rewritePropsForPre21: whether the player the
/// props describe is itself a 1.x client.
CString rewritePacketForPre21(const CString& packet, int versionId, const std::set<int>& alsoDrop = {}, bool fromPre21Client = false);

/// @brief The serveroptions.txt `legacyprops_drop` list (prop ids, comma
/// separated): props also left out for 1.x clients. A lab switch, to find
/// which prop a 1.x build mishandles without a rebuild. Empty by default.
std::set<int> parseDropList(std::string_view list);

} // namespace preagonal::legacy

#endif
