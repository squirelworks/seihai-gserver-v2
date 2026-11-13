// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// The prop sets come from the 1.33 to 1.41r1 version profiles (Graal_analysis
// client-internals/version-profiles): 1.33 to 1.39 read other players' props
// 0, 2, 8-13, 15-21, 24, 30-32, 50 and 51, plus 34 from 1.34 and 33 from 1.39,
// and their own props up to 33 (34 from 1.39, 36 from 1.41).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>

#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>

#include <object/Player.h>
#include <player/LegacyPlayerProps.h>
#include <player/PlayerProps.h>
#include <utilities/CommonTypes.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::legacy
{
///////////////////////////////////////////////////////////////////////////////

namespace
{
	bool endsWithNoCase(std::string_view text, std::string_view suffix)
	{
		if (text.size() < suffix.size())
			return false;
		return std::equal(suffix.begin(), suffix.end(), text.end() - static_cast<std::ptrdiff_t>(suffix.size()), [](char a, char b)
		{
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
	}

	// "foo.png" -> "foo.gif". The 1.x clients load only GIF images.
	std::string toGifName(std::string name)
	{
		if (endsWithNoCase(name, ".png"))
			name.replace(name.size() - 3, 3, "gif");
		return name;
	}

	// For "sword3.png" with stem "sword", the number 3; -1 if the name is not a
	// stock image ("<stem><number>.png" or ".gif", any case).
	int stockImageNumber(std::string_view name, std::string_view stem)
	{
		if (name.size() <= stem.size() + 4 || !(endsWithNoCase(name, ".png") || endsWithNoCase(name, ".gif")))
			return -1;
		for (size_t i = 0; i < stem.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(name[i])) != stem[i])
				return -1;
		}
		const auto digits = name.substr(stem.size(), name.size() - stem.size() - 4);
		if (digits.empty() || digits.size() > 2 || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; }))
			return -1;
		return std::stoi(std::string{digits});
	}

	int selfPropsMax(int versionId)
	{
		if (versionId >= CLVER_1_4)
			return PROPID(PlayerProp::RATING);
		if (versionId >= CLVER_1_39)
			return PROPID(PlayerProp::ACCOUNTNAME);
		return PROPID(PlayerProp::ADDITFLAGS);
	}

	// Sword (8): c(power) for a stock sword 1 to 4, else c(power + 30) c(len) image.
	void rewriteSword(CString& in, CString& out)
	{
		const int first = in.readGUChar();
		if (first < 30)
		{
			out >> (char)PROPID(PlayerProp::SWORDIMAGE) >> (char)first;
			return;
		}
		const int power = first - 30;
		const std::string image = toGifName(in.readChars(in.readGUChar()).toString());
		if (const int n = stockImageNumber(image, "sword"); n >= 1 && n <= 4 && n == power)
		{
			out >> (char)PROPID(PlayerProp::SWORDIMAGE) >> (char)power;
			return;
		}
		out >> (char)PROPID(PlayerProp::SWORDIMAGE) >> (char)first >> (char)image.length() << image;
	}

	// Shield (9): c(power) for a stock shield 1 to 3, else c(power + 10) c(len) image.
	void rewriteShield(CString& in, CString& out)
	{
		const int first = in.readGUChar();
		if (first < 10)
		{
			out >> (char)PROPID(PlayerProp::SHIELDIMAGE) >> (char)first;
			return;
		}
		const int power = first - 10;
		if (in.bytesLeft() == 0)
		{
			out >> (char)PROPID(PlayerProp::SHIELDIMAGE) >> (char)first;
			return;
		}
		const std::string image = toGifName(in.readChars(in.readGUChar()).toString());
		if (const int n = stockImageNumber(image, "shield"); n >= 1 && n <= 3 && n == power)
		{
			out >> (char)PROPID(PlayerProp::SHIELDIMAGE) >> (char)power;
			return;
		}
		out >> (char)PROPID(PlayerProp::SHIELDIMAGE) >> (char)first >> (char)image.length() << image;
	}

	// Head (11): c(n) for the stock head n, else c(len + 100) image.
	void rewriteHead(CString& in, CString& out)
	{
		const int first = in.readGUChar();
		if (first < 100)
		{
			out >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)first;
			return;
		}
		const std::string image = toGifName(in.readChars(first - 100).toString());
		if (const int n = stockImageNumber(image, "head"); n >= 0 && n < 100)
		{
			out >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)n;
			return;
		}
		out >> (char)PROPID(PlayerProp::HEADIMAGE) >> (char)(100 + image.length()) << image;
	}
} // namespace

bool isPre21Client(const int versionId)
{
	return versionId >= CLVER_1_20 && versionId < CLVER_2_0;
}

bool pre21KnowsProp(const int versionId, const int propId, const bool forOtherPlayer)
{
	// Prop 10 is the bow for these clients; a 2.x server holds a gani there.
	if (propId == PROPID(PlayerProp::GANI))
		return false;

	if (!forOtherPlayer)
		return propId >= 0 && propId <= selfPropsMax(versionId);

	switch (static_cast<PlayerProp>(propId))
	{
		case PlayerProp::NICKNAME:
		case PlayerProp::HALFHEARTS:
		case PlayerProp::SWORDIMAGE:
		case PlayerProp::SHIELDIMAGE:
		case PlayerProp::HEADIMAGE:
		case PlayerProp::MESSAGE:
		case PlayerProp::COLORS:
		case PlayerProp::X:
		case PlayerProp::Y:
		case PlayerProp::SPRITE:
		case PlayerProp::STATUS:
		case PlayerProp::CARRYSPRITE:
		case PlayerProp::LEVEL:
		case PlayerProp::HORSEIMAGE:
		case PlayerProp::CARRYNPCID:
		case PlayerProp::IPADDR:
		case PlayerProp::UDPPORT:
		case PlayerProp::ALIGNMENT:
		case PlayerProp::JOINLEAVELVL:
		case PlayerProp::DISCONNECT:
			return true;
		case PlayerProp::ACCOUNTNAME:
			return versionId >= CLVER_1_34;
		case PlayerProp::ADDITFLAGS:
			return versionId >= CLVER_1_39;
		default:
			return false;
	}
}

CString rewritePropsForPre21(const CString& props, const int versionId, const bool forOtherPlayer, const std::set<int>& alsoDrop)
{
	CString in{props};
	in.setRead(0);
	CString out;

	while (in.bytesLeft() > 0)
	{
		const int propId = in.readGUChar();
		if (propId < 0 || propId >= PLAYERPROP_COUNT)
			break;

		const bool keep = pre21KnowsProp(versionId, propId, forOtherPlayer) && !alsoDrop.contains(propId);
		try
		{
			if (keep && propId == PROPID(PlayerProp::SWORDIMAGE))
				rewriteSword(in, out);
			else if (keep && propId == PROPID(PlayerProp::SHIELDIMAGE))
				rewriteShield(in, out);
			else if (keep && propId == PROPID(PlayerProp::HEADIMAGE))
				rewriteHead(in, out);
			else
			{
				// The 2.x reader of the prop tells where its value ends.
				const int start = in.readPos();
				auto prop = Player::constructPropFor(static_cast<PlayerProp>(propId));
				prop->deserialize(in);
				if (!keep)
					continue;

				int length = in.readPos() - start;
				if (propId == PROPID(PlayerProp::COLORS))
					length = std::min(length, 5);

				out >> (char)propId;
				if (propId == PROPID(PlayerProp::HORSEIMAGE))
				{
					const std::string image = toGifName(props.subString(start + 1, length - 1).toString());
					out >> (char)image.length() << image;
				}
				else
					out << props.subString(start, length);
			}
		}
		catch (const std::exception&)
		{
			// A value we cannot read: stop here, as the client would.
			break;
		}
	}

	return out;
}

CString rewritePacketForPre21(const CString& packet, const int versionId, const std::set<int>& alsoDrop)
{
	if (packet.length() < 1)
		return packet;

	const int packetId = static_cast<unsigned char>(packet[0]) - 32;
	if (packetId == PLO_PLAYERPROPS)
		return CString() >> (char)PLO_PLAYERPROPS << rewritePropsForPre21(packet.subString(1), versionId, false, alsoDrop);

	if (packetId == PLO_OTHERPLPROPS && packet.length() >= 3)
		return CString() >> (char)PLO_OTHERPLPROPS << packet.subString(1, 2) << rewritePropsForPre21(packet.subString(3), versionId, true, alsoDrop);

	return packet;
}

std::set<int> parseDropList(const std::string_view list)
{
	std::set<int> ids;
	int value = -1;
	for (const char c : list)
	{
		if (c >= '0' && c <= '9')
			value = (value < 0 ? 0 : value * 10) + (c - '0');
		else
		{
			if (value >= 0 && value < 256)
				ids.insert(value);
			value = -1;
		}
	}
	if (value >= 0 && value < 256)
		ids.insert(value);
	return ids;
}

} // namespace preagonal::legacy
