// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
//
// AP recharge threshold index for serveroptions aptime0..aptime5.
#ifndef APSYSTEM_H
#define APSYSTEM_H

#include <cstddef>
#include <cstdint>

////////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
////////////////////////////////////////////////////////////////////////////////

/// @brief Number of `aptime#` server options (aptime0 .. aptime5).
inline constexpr std::size_t APSystemThresholdCount = 6;

/// @brief Maps current AP (0..100) to the aptime# band used for the next recharge.
///
/// Bands keep the historical 20-point steps for aptime0..aptime3, and split the
/// old aptime4 top band so aptime5 is reachable without changing lower ranges:
///   aptime0: 0..19
///   aptime1: 20..39
///   aptime2: 40..59
///   aptime3: 60..79
///   aptime4: 80..89
///   aptime5: 90..100
[[nodiscard]] constexpr std::size_t apSystemThresholdIndex(const uint8_t ap) noexcept
{
	if (ap < 20)
		return 0;
	if (ap < 40)
		return 1;
	if (ap < 60)
		return 2;
	if (ap < 80)
		return 3;
	if (ap < 90)
		return 4;
	return 5;
}

////////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // APSYSTEM_H
