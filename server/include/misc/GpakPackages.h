// Modified by the Seihai project, 2026-11-06, 2026-11-07: see CHANGES-SEIHAI.md.
// New file: resource packages (.gpak, .gpake) and the PLO_FILEDECRYPTKEY key packet that
// lets a client open an encrypted one. Kept apart from Player so it can be tested on
// its own.
//
// What the clients do (real-client lab runs, 2026-11-06):
// - A package is a plain ZIP. The client saves it under levels/gpaks/ and finds every
//   file in it by its bare name, as if the file lay loose in the levels folder.
// - In a .gpake every entry is DES-encrypted. The client opens it only with the key the
//   server sent in PLO_FILEDECRYPTKEY *before* the file (PLO_FILE, or PLO_LARGEFILESTART ...
//   PLO_LARGEFILEEND) or before PLO_FILEUPTODATE for that name.
// - 6.03r7 loads .gpak and .gpake. 4.20r8 loads .gpake, but saves a plain .gpak without
//   ever loading it. 4.04r2 loads no packages; clients before 4.x have none.
// The packages are built with `seihai-servertools gpak pack`, which also writes the key
// file read here.
#ifndef GPAKPACKAGES_H
#define GPAKPACKAGES_H

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <CString.h>
#include <IEnums.h>
#include <IUtil.h>

namespace preagonal
{
	using GpakKey = std::array<uint8_t, 8>;

	enum class GpakKind
	{
		None,      // not a package
		Plain,     // .gpak
		Encrypted, // .gpake
	};

	/// @brief The kind of package a file name is, by its extension (any case).
	inline GpakKind gpakKind(std::string_view filename)
	{
		auto endsWith = [filename](std::string_view ext)
		{
			if (filename.size() < ext.size())
				return false;
			const auto tail = filename.substr(filename.size() - ext.size());
			for (size_t i = 0; i < ext.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(tail[i])) != ext[i])
					return false;
			}
			return true;
		};
		if (endsWith(".gpake"))
			return GpakKind::Encrypted;
		if (endsWith(".gpak"))
			return GpakKind::Plain;
		return GpakKind::None;
	}

	/// @brief Whether a client of this version loads a package of this kind.
	/// Encrypted: 4.20r8 and every 5.x and 6.x build (4.20r8 and 6.03r7 checked with the real
	/// clients). Plain: 5.x and 6.x only, since 4.20r8 never loads a .gpak (6.03r7 checked;
	/// 5.x not run yet). Not 4.04r2 and older, not the iPhone builds. Anything that is not a
	/// package: every client.
	inline bool clientLoadsGpak(int versionId, GpakKind kind)
	{
		switch (kind)
		{
			case GpakKind::None:
				return true;
			case GpakKind::Encrypted:
				return versionId >= CLVER_4_208 && versionId <= CLVER_6_037_LINUX;
			case GpakKind::Plain:
				return versionId >= CLVER_5_07 && versionId <= CLVER_6_037_LINUX;
		}
		return false;
	}

	/// @brief Reads 16 hex digits into a key.
	inline std::optional<GpakKey> parseGpakKey(std::string_view hex)
	{
		if (hex.size() != 16)
			return std::nullopt;
		GpakKey key{};
		for (size_t i = 0; i < 8; ++i)
		{
			int value = 0;
			for (size_t j = 0; j < 2; ++j)
			{
				const char c = hex[2 * i + j];
				int digit;
				if (c >= '0' && c <= '9')
					digit = c - '0';
				else if (c >= 'a' && c <= 'f')
					digit = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					digit = c - 'A' + 10;
				else
					return std::nullopt;
				value = value * 16 + digit;
			}
			key[i] = static_cast<uint8_t>(value);
		}
		return key;
	}

	/// @brief The key for a package in the text of config/gpakkeys.txt.
	/// One "name.gpake HEX16" line per package, as `seihai-servertools gpak pack` writes it;
	/// '#' starts a comment line, names match in any case.
	inline std::optional<GpakKey> gpakKeyFromText(std::string_view text, std::string_view package)
	{
		auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
		while (!text.empty())
		{
			const auto end = text.find('\n');
			std::string_view line = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);

			size_t p = 0;
			while (p < line.size() && isSpace(line[p])) ++p;
			size_t q = p;
			while (q < line.size() && !isSpace(line[q])) ++q;
			const auto name = line.substr(p, q - p);
			if (name.empty() || name[0] == '#' || name.size() != package.size())
				continue;
			bool same = true;
			for (size_t i = 0; i < name.size() && same; ++i)
				same = std::tolower(static_cast<unsigned char>(name[i])) == std::tolower(static_cast<unsigned char>(package[i]));
			if (!same)
				continue;

			while (q < line.size() && isSpace(line[q])) ++q;
			size_t r = q;
			while (r < line.size() && !isSpace(line[r])) ++r;
			return parseGpakKey(line.substr(q, r - q));
		}
		return std::nullopt;
	}

	/// @brief PLO_FILEDECRYPTKEY for a package: {GSTRING name}{GCHAR 1}{GINT5 lo}{GINT5 hi}.
	/// name is the package's file name in lower case with no folder; lo and hi are key bytes
	/// 0-3 and 4-7, each read as a little-endian 32-bit value. The client looks the key up by
	/// that name when the package arrives. The 1 is what 6.03r7 needs for a loose .enc file;
	/// packages ignore it.
	inline CString gpakKeyPacket(std::string_view package, const GpakKey& key)
	{
		std::string name;
		const auto slash = package.find_last_of("/\\");
		for (char c : (slash == std::string_view::npos ? package : package.substr(slash + 1)))
			name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		if (name.size() > 223)
			name.resize(223);

		auto half = [&key](size_t at)
		{
			return static_cast<uint32_t>(key[at]) | (static_cast<uint32_t>(key[at + 1]) << 8) |
				(static_cast<uint32_t>(key[at + 2]) << 16) | (static_cast<uint32_t>(key[at + 3]) << 24);
		};

		CString packet;
		packet >> (char)PLO_FILEDECRYPTKEY >> (char)name.length() << name >> (char)1;
		packet.writeGInt5(half(0));
		packet.writeGInt5(half(4));
		return packet;
	}
} // namespace preagonal

#endif // GPAKPACKAGES_H
