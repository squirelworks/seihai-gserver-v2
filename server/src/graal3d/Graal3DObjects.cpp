// Modified by the Seihai project, 2026-11-12, 2026-11-13, 2026-11-19, 2026-11-20, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: GameObject and Sun updates for the Graal 3D world, and the lab's raw
// objects and events. See Graal3DObjects.h.

#include <graal3d/Graal3DObjects.h>

#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstring>

#include <zlib.h>

namespace graal3d
{
	namespace
	{
		using namespace GameObjectBits;

		// interpolate and rigid: position (3 floats), a quaternion (4 x signed
		// 8-bit floats), two vectors (6 x signed 9-bit ints), one flag.
		constexpr size_t RigidMoveBits = 3 * 32 + 4 * 8 + 6 * 9 + 1;
		// list: entries of {flag 1, 8 x 32 bits}, then flag 0.
		constexpr size_t ListEntryBits = 8 * 32;

		void skipMovementData(BitReader& r, std::string_view moveType)
		{
			if (moveType == "list")
			{
				while (r.readFlag() && !r.overrun())
					r.setBitPosition(r.bitPosition() + ListEntryBits);
				return;
			}
			r.setBitPosition(r.bitPosition() + RigidMoveBits);
		}

		void writeFloats(BitWriter& w, const float* values, size_t count)
		{
			for (size_t i = 0; i < count; ++i)
				w.writeF32(values[i]);
		}

		void readFloats(BitReader& r, float* values, size_t count)
		{
			for (size_t i = 0; i < count; ++i)
				values[i] = r.readF32();
		}
	} // namespace

	bool hasMovementObject(std::string_view moveType)
	{
		return moveType == "interpolate" || moveType == "list" || moveType == "rigid";
	}

	uint32_t gameObjectChanges(const GameObjectState& a, const GameObjectState& b)
	{
		uint32_t mask = 0;
		if (a.name != b.name)
			mask |= Name;
		if (a.velocity != b.velocity)
			mask |= Velocity;
		if (a.shape != b.shape)
			mask |= Shape;
		if (a.skin != b.skin)
			mask |= Skin;
		if (a.transform != b.transform)
			mask |= GameObjectBits::Transform;
		if (a.chat != b.chat)
			mask |= Chat;
		for (size_t i = 0; i < a.attr.size(); ++i)
		{
			if (a.attr[i] != b.attr[i])
				mask |= Attr1 << i;
		}
		if (a.nickname != b.nickname || a.nickColor != b.nickColor)
			mask |= Nickname;
		if (a.flags != b.flags)
			mask |= Flags;
		return mask;
	}

	uint32_t gameObjectFullMask(const GameObjectState& s)
	{
		// Name, transform and flags always; the rest when set.
		uint32_t mask = Name | GameObjectBits::Transform | Flags;
		if (s.velocity != std::array<float, 3>{})
			mask |= Velocity;
		if (!s.shape.empty())
			mask |= Shape;
		if (!s.skin.empty())
			mask |= Skin;
		if (!s.chat.empty())
			mask |= Chat;
		for (size_t i = 0; i < s.attr.size(); ++i)
		{
			if (!s.attr[i].empty())
				mask |= Attr1 << i;
		}
		if (!s.nickname.empty())
			mask |= Nickname;
		return mask;
	}

	void writeGameObjectUpdate(BitWriter& w, const GameObjectState& s, uint32_t mask)
	{
		if (w.writeFlag(mask & Name))
			w.writeString(s.name);
		if (w.writeFlag(mask & Velocity))
			writeFloats(w, s.velocity.data(), 3);
		if (w.writeFlag(mask & Shape))
			w.writeString(s.shape);
		if (w.writeFlag(mask & Skin))
			w.writeString(s.skin);
		if (w.writeFlag(mask & GameObjectBits::Transform))
		{
			writeFloats(w, s.transform.position.data(), 3);
			writeFloats(w, s.transform.axis.data(), 3);
			w.writeF32(s.transform.angle);
		}
		w.writeFlag(false); // 6 movetype: never sent
		w.writeFlag(false); // 7 movement data: no movement object
		w.writeFlag(false); // 8 shape data (animation threads): not sent
		if (w.writeFlag(mask & Chat))
			w.writeString(s.chat);
		w.writeFlag(false); // 10 script classes
		if (w.writeFlag(mask & Attrs))
		{
			for (size_t i = 0; i < s.attr.size(); ++i)
			{
				if (w.writeFlag(mask & (Attr1 << i)))
					w.writeString(s.attr[i]);
			}
		}
		if (w.writeFlag(mask & Nickname))
		{
			w.writeString(s.nickname);
			for (auto c : s.nickColor)
				w.writeInt(c, 8);
		}
		if (w.writeFlag(mask & Flags))
			w.writeInt(s.flags, 16);
	}

	uint32_t readGameObjectUpdate(BitReader& r, GameObjectState& s, std::string& moveType)
	{
		uint32_t mask = 0;
		if (r.readFlag())
		{
			s.name = r.readString();
			mask |= Name;
		}
		if (r.readFlag())
		{
			readFloats(r, s.velocity.data(), 3);
			mask |= Velocity;
		}
		if (r.readFlag())
		{
			s.shape = r.readString();
			mask |= Shape;
		}
		if (r.readFlag())
		{
			s.skin = r.readString();
			mask |= Skin;
		}
		if (r.readFlag())
		{
			readFloats(r, s.transform.position.data(), 3);
			readFloats(r, s.transform.axis.data(), 3);
			s.transform.angle = r.readF32();
			mask |= GameObjectBits::Transform;
		}
		if (r.readFlag())
		{
			moveType = r.readString();
			mask |= MoveType;
		}
		// The reader only takes movement data while it has a movement object.
		if (r.readFlag() && hasMovementObject(moveType))
		{
			skipMovementData(r, moveType);
			mask |= MoveData;
		}
		if (r.readFlag())
		{
			const auto bits = r.readInt(14);
			r.setBitPosition(r.bitPosition() + bits);
			mask |= ShapeData;
		}
		if (r.readFlag())
		{
			s.chat = r.readString();
			mask |= Chat;
		}
		if (r.readFlag())
		{
			r.readString();
			mask |= Classes;
		}
		if (r.readFlag())
		{
			for (size_t i = 0; i < s.attr.size(); ++i)
			{
				if (r.readFlag())
				{
					s.attr[i] = r.readString();
					mask |= Attr1 << i;
				}
			}
		}
		if (r.readFlag())
		{
			s.nickname = r.readString();
			for (auto& c : s.nickColor)
				c = static_cast<uint8_t>(r.readInt(8));
			mask |= Nickname;
		}
		if (r.readFlag())
		{
			s.flags = static_cast<uint16_t>(r.readInt(16));
			mask |= Flags;
		}
		return mask;
	}

	void writeSunUpdate(BitWriter& w, const SunState& sun)
	{
		w.writeFlag(true);
		w.writeString(sun.name);
		writeFloats(w, sun.direction.data(), 3);
		writeFloats(w, sun.color.data(), 4);
		writeFloats(w, sun.ambient.data(), 4);
	}

	SunState readSunUpdate(BitReader& r)
	{
		SunState sun;
		if (!r.readFlag())
			return sun;
		sun.name = r.readString();
		readFloats(r, sun.direction.data(), 3);
		readFloats(r, sun.color.data(), 4);
		readFloats(r, sun.ambient.data(), 4);
		return sun;
	}

	void writeSkyUpdate(BitWriter& w, const SkyState& sky)
	{
		w.writeFlag(true); // init
		w.writeString(sky.name);
		w.writeFlag(true); // material list
		w.writeF32(sky.visibleDistance);
		w.writeString(sky.materialList);

		// The init part.
		writeFloats(w, sky.fogColor.data(), 3);
		w.writeU32(0); // no fog volumes
		w.writeInt(sky.bools[0], 8);
		w.writeInt(sky.bools[1], 8);
		writeFloats(w, sky.solidColor.data(), 3);
		w.writeInt(sky.bools[2], 8);
		w.writeFlag(false);
		for (size_t i = 0; i < 3; ++i)
		{
			w.writeString(""); // the client's own writer never sends a cloud texture
			w.writeF32(sky.cloudHeight[i]);
			w.writeF32(sky.cloudSpeed[i]);
		}
		writeFloats(w, sky.windVelocity.data(), 3);
		w.writeU32(0);
		w.writeFlag(false); // no storm fog running

		w.writeFlag(false); // mask 0x20
		w.writeFlag(false); // mask 0x40 (not with init)
		w.writeFlag(true);  // the writer repeats the init state here
		w.writeFlag(false); // mask 0x2
		w.writeFlag(false); // mask 0x4 (not with init)
		w.writeFlag(false); // mask 0x8
		w.writeFlag(false); // mask 0x10
	}

	SkyState readSkyUpdate(BitReader& r)
	{
		SkyState sky;
		const bool init = r.readFlag();
		if (init)
			sky.name = r.readString();
		if (r.readFlag())
		{
			sky.visibleDistance = r.readF32();
			sky.materialList = r.readString();
		}
		if (init)
		{
			readFloats(r, sky.fogColor.data(), 3);
			const uint32_t volumes = r.readU32();
			sky.bools[0] = static_cast<uint8_t>(r.readInt(8));
			sky.bools[1] = static_cast<uint8_t>(r.readInt(8));
			readFloats(r, sky.solidColor.data(), 3);
			sky.bools[2] = static_cast<uint8_t>(r.readInt(8));
			r.readFlag();
			for (uint32_t i = 0; i < volumes && i < 64 && !r.overrun(); ++i)
			{
				for (int f = 0; f < 6; ++f)
					r.readF32();
				r.readInt(8);
			}
			for (size_t i = 0; i < 3; ++i)
			{
				r.readString();
				sky.cloudHeight[i] = r.readF32();
				sky.cloudSpeed[i] = r.readF32();
			}
			readFloats(r, sky.windVelocity.data(), 3);
			r.readU32();
			if (r.readFlag())
				for (int i = 0; i < 5; ++i)
					r.readU32();
		}
		if (r.readFlag())
			r.readInt(8);
		if (r.readFlag())
			r.readInt(8);
		r.readFlag();
		if (r.readFlag())
		{
			r.readU32();
			r.readU32();
		}
		if (r.readFlag())
			for (int i = 0; i < 3; ++i)
				r.readU32();
		if (r.readFlag())
			for (int i = 0; i < 4; ++i)
				r.readU32();
		if (r.readFlag())
			for (int i = 0; i < 3; ++i)
				r.readU32();
		return sky;
	}

	void GhostObject::writeUpdate(BitWriter& w, uint32_t mask, bool controlled) const
	{
		if (kind == Kind::Sun)
		{
			writeSunUpdate(w, sun);
			return;
		}
		if (kind == Kind::Sky)
		{
			writeSkyUpdate(w, sky);
			return;
		}
		if (kind == Kind::TgeCamera)
		{
			tge::writeCameraUpdate(w, tgeCamera, datablockId, (mask & TgeDatablock) != 0);
			return;
		}
		if (kind == Kind::TgePlayer)
		{
			tge::writePlayerUpdate(w, tgePlayer, datablockId, (mask & TgeDatablock) != 0, controlled);
			return;
		}
		if (kind == Kind::Raw)
		{
			writeRawFields(w, raw);
			return;
		}
		writeGameObjectUpdate(w, object, mask);
		if (kind == Kind::Camera)
		{
			w.writeFlag(false); // camera mode: left to the client
			w.writeFlag(false); // followed object: none
		}
	}

	uint32_t GhostObject::changesFrom(const GhostObject& before) const
	{
		if (kind != before.kind)
			return fullMask();
		if (kind == Kind::Sun)
			return sun == before.sun ? 0u : 1u;
		if (kind == Kind::Sky)
			return sky == before.sky ? 0u : 1u;
		if (kind == Kind::TgeCamera)
		{
			const auto& a = tgeCamera;
			const auto& b = before.tgeCamera;
			return a.position == b.position && a.pitch == b.pitch && a.yaw == b.yaw ? 0u : TgeMove;
		}
		if (kind == Kind::TgePlayer)
			return tgePlayer == before.tgePlayer ? 0u : TgeMove;
		if (kind == Kind::Raw)
			return rawClass == before.rawClass && raw == before.raw ? 0u : 1u;
		return gameObjectChanges(before.object, object);
	}

	// --- Raw lab fields ---------------------------------------------------------

	namespace
	{
		std::optional<uint32_t> parseInt(std::string_view text, int bits)
		{
			if (text.empty())
				return std::nullopt;
			const std::string word{text};
			char* end = nullptr;
			const bool negative = word[0] == '-';
			const long long v = negative ? std::strtoll(word.c_str(), &end, 0) : static_cast<long long>(std::strtoull(word.c_str(), &end, 0));
			if (end != word.c_str() + word.size())
				return std::nullopt;
			const unsigned long long limit = bits >= 32 ? 0xFFFFFFFFull : ((1ull << bits) - 1);
			if (negative)
			{
				// Two's complement in `bits` bits.
				if (-v > static_cast<long long>(limit / 2 + 1))
					return std::nullopt;
				return static_cast<uint32_t>(static_cast<unsigned long long>(v) & limit);
			}
			if (static_cast<unsigned long long>(v) > limit)
				return std::nullopt;
			return static_cast<uint32_t>(v);
		}

		std::optional<std::string> hexDecode(std::string_view text)
		{
			if (text.size() % 2 != 0)
				return std::nullopt;
			std::string out;
			for (size_t i = 0; i < text.size(); i += 2)
			{
				const auto b = parseInt(std::string("0x") + std::string(text.substr(i, 2)), 8);
				if (!b || !std::isxdigit(static_cast<unsigned char>(text[i])) || !std::isxdigit(static_cast<unsigned char>(text[i + 1])))
					return std::nullopt;
				out.push_back(static_cast<char>(*b));
			}
			return out;
		}

		std::optional<std::string> percentDecode(std::string_view text)
		{
			if (text == "-")
				return std::string{};
			std::string out;
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] != '%')
				{
					out.push_back(text[i]);
					continue;
				}
				if (i + 2 >= text.size())
					return std::nullopt;
				auto hex = hexDecode(text.substr(i + 1, 2));
				if (!hex)
					return std::nullopt;
				out += *hex;
				i += 2;
			}
			return out;
		}

	} // namespace

	std::optional<std::vector<RawField>> parseRawFields(const std::vector<std::string_view>& tokens, std::string& error)
	{
		std::vector<RawField> out;
		for (const auto token : tokens)
		{
			RawField f;
			const auto colon = token.find(':');
			if (token == "f0" || token == "f1")
			{
				f.type = RawField::Type::Flag;
				f.value = token == "f1" ? 1 : 0;
				out.push_back(std::move(f));
				continue;
			}
			if (colon == std::string_view::npos)
			{
				error = "no ':' in raw field " + std::string(token);
				return std::nullopt;
			}
			auto kind = token.substr(0, colon);
			const auto value = token.substr(colon + 1);

			// Bytes fields may carry a length prefix: name@bits.
			int prefix = 0;
			if (const auto at = kind.find('@'); at != std::string_view::npos)
			{
				const auto width = parseInt(kind.substr(at + 1), 8);
				if (!width || *width < 1 || *width > 32)
				{
					error = "bad length width in raw field " + std::string(token);
					return std::nullopt;
				}
				prefix = static_cast<int>(*width);
				kind = kind.substr(0, at);
			}

			bool ok = true;
			if (kind == "hex" || kind == "file" || kind == "zfile")
			{
				f.type = RawField::Type::Bytes;
				f.bits = prefix;
				if (kind == "hex")
				{
					auto bytes = hexDecode(value);
					ok = bytes.has_value();
					if (ok)
						f.data = std::move(*bytes);
				}
				else
				{
					f.file = value;
					f.compress = kind == "zfile";
					ok = !value.empty();
				}
			}
			else if (prefix != 0)
				ok = false;
			else if (kind == "f32")
			{
				f.type = RawField::Type::F32;
				const std::string word{value};
				char* end = nullptr;
				f.f32 = std::strtof(word.c_str(), &end);
				ok = !word.empty() && end == word.c_str() + word.size();
			}
			else if (kind == "str")
			{
				f.type = RawField::Type::String;
				auto text = percentDecode(value);
				ok = text.has_value() && text->size() <= 255;
				if (ok)
					f.data = std::move(*text);
			}
			else
			{
				int bits = 0;
				if (kind == "u8")
					bits = 8;
				else if (kind == "u16")
					bits = 16;
				else if (kind == "u32")
					bits = 32;
				else if (kind.size() > 1 && kind[0] == 'i')
				{
					const auto width = parseInt(kind.substr(1), 8);
					bits = width ? static_cast<int>(*width) : 0;
				}
				ok = bits >= 1 && bits <= 32;
				if (ok)
				{
					const auto v = parseInt(value, bits);
					ok = v.has_value();
					f.type = RawField::Type::Int;
					f.bits = bits;
					f.value = v.value_or(0);
				}
			}
			if (!ok)
			{
				error = "bad raw field " + std::string(token);
				return std::nullopt;
			}
			out.push_back(std::move(f));
		}
		return out;
	}

	void writeRawFields(BitWriter& w, const std::vector<RawField>& fields)
	{
		for (const auto& f : fields)
		{
			switch (f.type)
			{
				case RawField::Type::Flag: w.writeFlag(f.value != 0); break;
				case RawField::Type::Int: w.writeInt(f.value, f.bits); break;
				case RawField::Type::F32: w.writeF32(f.f32); break;
				case RawField::Type::String: w.writeString(f.data); break;
				case RawField::Type::Bytes:
					if (f.bits > 0)
						w.writeInt(static_cast<uint32_t>(f.data.size()), f.bits);
					w.writeBytes(std::string_view(f.data));
					break;
			}
		}
	}

	size_t rawFieldBits(const std::vector<RawField>& fields)
	{
		BitWriter w;
		writeRawFields(w, fields);
		return w.bitPosition();
	}

	std::vector<std::string> loadRawFiles(std::vector<RawField>& fields,
										  const std::function<std::optional<std::string>(const std::string&)>& read)
	{
		std::vector<std::string> errors;
		for (auto& f : fields)
		{
			if (f.type != RawField::Type::Bytes || f.file.empty())
				continue;
			auto bytes = read(f.file);
			if (!bytes)
			{
				errors.push_back("cannot read " + f.file);
				continue;
			}
			if (f.compress)
			{
				uLongf size = compressBound(static_cast<uLong>(bytes->size()));
				std::string packed(size, '\0');
				if (compress2(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(bytes->data()),
							  static_cast<uLong>(bytes->size()), Z_BEST_COMPRESSION) != Z_OK)
				{
					errors.push_back("cannot compress " + f.file);
					continue;
				}
				packed.resize(size);
				bytes = std::move(packed);
			}
			if (f.bits > 0 && f.bits < 32 && bytes->size() >= (size_t{1} << f.bits))
			{
				errors.push_back(f.file + " is " + std::to_string(bytes->size()) + " bytes, too long for an int" + std::to_string(f.bits) +
								 " length");
				continue;
			}
			f.data = std::move(*bytes);
		}
		return errors;
	}

	namespace
	{
		std::vector<std::string_view> words(std::string_view line)
		{
			std::vector<std::string_view> out;
			size_t pos = 0;
			while (pos < line.size())
			{
				while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
					++pos;
				const size_t start = pos;
				while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
					++pos;
				if (pos > start)
					out.push_back(line.substr(start, pos - start));
			}
			return out;
		}

		bool toFloats(const std::vector<std::string_view>& w, size_t first, float* out, size_t count)
		{
			if (w.size() < first + count)
				return false;
			for (size_t i = 0; i < count; ++i)
			{
				// strtof rather than from_chars: older libc++ has no float from_chars.
				const std::string word{w[first + i]};
				char* end = nullptr;
				out[i] = std::strtof(word.c_str(), &end);
				if (end != word.c_str() + word.size())
					return false;
			}
			return true;
		}
	} // namespace

	World parseWorld(std::string_view text)
	{
		World world;
		size_t lineNo = 0;
		while (!text.empty())
		{
			++lineNo;
			const auto end = text.find('\n');
			auto line = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
			if (const auto hash = line.find('#'); hash != std::string_view::npos)
				line = line.substr(0, hash);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);

			const auto w = words(line);
			if (w.empty())
				continue;

			bool ok = false;
			if (w[0] == "object" && (w.size() == 6 || w.size() == 10))
			{
				GhostObject obj;
				obj.object.name = w[1];
				obj.object.shape = w[2];
				ok = toFloats(w, 3, obj.object.transform.position.data(), 3);
				if (ok && w.size() == 10)
					ok = toFloats(w, 6, obj.object.transform.axis.data(), 3) && toFloats(w, 9, &obj.object.transform.angle, 1);
				if (ok)
					world.objects.push_back(std::move(obj));
			}
			else if (w[0] == "sun" && w.size() == 12)
			{
				GhostObject sun;
				sun.kind = GhostObject::Kind::Sun;
				ok = toFloats(w, 1, sun.sun.direction.data(), 3) && toFloats(w, 4, sun.sun.color.data(), 4) &&
					 toFloats(w, 8, sun.sun.ambient.data(), 4);
				if (ok)
					world.objects.push_back(std::move(sun));
			}
			else if (w[0] == "sky" && (w.size() == 2 || w.size() == 3 || w.size() == 6))
			{
				GhostObject sky;
				sky.kind = GhostObject::Kind::Sky;
				if (w[1] != "-")
					sky.sky.materialList = w[1];
				ok = true;
				if (w.size() >= 3)
					ok = toFloats(w, 2, &sky.sky.visibleDistance, 1);
				if (ok && w.size() == 6)
					ok = toFloats(w, 3, sky.sky.fogColor.data(), 3);
				if (ok)
					world.objects.push_back(std::move(sky));
			}
			else if (w[0] == "skyoptions" && w.size() == 7)
			{
				std::array<float, 3> b{};
				std::array<float, 3> solid{};
				ok = toFloats(w, 1, b.data(), 3) && toFloats(w, 4, solid.data(), 3);
				GhostObject* sky = nullptr;
				for (auto& o : world.objects)
					if (o.kind == GhostObject::Kind::Sky)
						sky = &o;
				ok = ok && sky != nullptr; // after the sky line
				if (ok)
				{
					for (size_t i = 0; i < 3; ++i)
						sky->sky.bools[i] = b[i] != 0.0f ? 1 : 0;
					sky->sky.solidColor = solid;
				}
			}
			else if (w[0] == "weapon3d" && (w.size() == 2 || w.size() == 3))
			{
				world.weapons.push_back({std::string(w[1]), w.size() == 3 ? std::string(w[2]) : std::string{}});
				ok = true;
			}
			else if (w[0] == "playershape" && w.size() == 2)
			{
				world.playerShape = w[1];
				ok = true;
			}
			else if (w[0] == "spawn" && w.size() == 4)
				ok = toFloats(w, 1, world.spawn.position.data(), 3);
			else if (w[0] == "tgecontrol" && w.size() == 2 && (w[1] == "player" || w[1] == "camera"))
			{
				world.tgeControl = w[1] == "camera" ? World::TgeControl::Camera : World::TgeControl::Player;
				ok = true;
			}
			else if (w[0] == "tgeplayershape" && w.size() == 2)
			{
				world.tgePlayerShape = w[1];
				ok = true;
			}
			else if (w[0] == "rawobject" && w.size() >= 2)
			{
				std::string why;
				auto fields = parseRawFields({w.begin() + 2, w.end()}, why);
				if (!fields)
				{
					world.errors.push_back("line " + std::to_string(lineNo) + ": " + why);
					continue;
				}
				GhostObject raw;
				raw.kind = GhostObject::Kind::Raw;
				raw.rawClass = w[1];
				raw.raw = std::move(*fields);
				world.objects.push_back(std::move(raw));
				ok = true;
			}
			else if (w[0] == "water" && w.size() >= 7 && w.size() <= 9)
			{
				WaterSettings water;
				ok = toFloats(w, 1, water.position.data(), 3) && toFloats(w, 4, water.scale.data(), 3);
				if (ok && w.size() >= 8)
					water.surfaceTexture = w[7];
				if (ok && w.size() == 9)
				{
					char* end = nullptr;
					const std::string word(w[8]);
					water.liquidType = static_cast<uint32_t>(std::strtoul(word.c_str(), &end, 10));
					ok = end && *end == '\0';
				}
				if (ok)
				{
					GhostObject block;
					block.kind = GhostObject::Kind::Raw;
					block.rawClass = "WaterBlock";
					block.raw = waterBlockFields(water);
					block.layout512 = true;
					world.objects.push_back(std::move(block));
				}
			}
			else if (w[0] == "terrain" && w.size() >= 2 && w.size() <= 4 && !world.terrain)
			{
				Terrain terrain;
				terrain.folder = w[1];
				ok = true;
				for (size_t i = 2; i < w.size() && ok; ++i)
				{
					if (w[i] == "repeat")
						terrain.repeat = true;
					else
					{
						char* end = nullptr;
						const std::string word(w[i]);
						const unsigned long size = std::strtoul(word.c_str(), &end, 10);
						ok = end && *end == '\0' && size >= 1 && size <= 1024;
						terrain.squareSize = static_cast<uint32_t>(size);
					}
				}
				if (ok)
				{
					GhostObject block;
					block.kind = GhostObject::Kind::Raw;
					block.rawClass = "TerrainBlock";
					block.raw = terrainBlockFields(terrain);
					block.layout512 = true;
					world.objects.push_back(std::move(block));
					world.terrain = std::move(terrain);
				}
			}
			else if (w[0] == "rawevent" && w.size() >= 2)
			{
				const bool late = w[1] == "late";
				const size_t cls = late ? 2 : 1;
				std::string why = "no event class";
				std::optional<std::vector<RawField>> fields;
				if (w.size() > cls)
					fields = parseRawFields({w.begin() + static_cast<std::ptrdiff_t>(cls) + 1, w.end()}, why);
				if (!fields)
				{
					world.errors.push_back("line " + std::to_string(lineNo) + ": " + why);
					continue;
				}
				world.rawEvents.push_back({std::string(w[cls]), std::move(*fields), late});
				ok = true;
			}
			else if (w[0] == "camera" && (w.size() == 4 || w.size() == 8))
			{
				std::array<float, 3> offset{};
				std::array<float, 4> rotation{};
				ok = toFloats(w, 1, offset.data(), 3);
				if (ok && w.size() == 8)
					ok = toFloats(w, 4, rotation.data(), 4);
				if (ok)
				{
					world.cameraOffset = offset;
					world.cameraRotation.reset();
					if (w.size() == 8)
						world.cameraRotation = rotation;
				}
			}

			if (!ok)
				world.errors.push_back("line " + std::to_string(lineNo) + ": " + std::string(line));
		}
		return world;
	}

	uint32_t terrainStamp(std::string_view plain)
	{
		const auto crc = crc32(0L, reinterpret_cast<const Bytef*>(plain.data()), static_cast<uInt>(plain.size()));
		return static_cast<uint32_t>(crc) ^ 0xFFFFFFFFu;
	}

	std::vector<RawField> terrainBlockFields(const Terrain& terrain)
	{
		const auto u32 = [](uint32_t v) { return RawField{RawField::Type::Int, 32, v}; };
		const auto f32 = [](float v) {
			RawField f{RawField::Type::F32};
			f.f32 = v;
			return f;
		};
		const auto str = [](std::string text) {
			RawField f{RawField::Type::String};
			f.data = std::move(text);
			return f;
		};
		return {
			RawField{RawField::Type::Flag, 1, 1},
			str("Terrain"),
			u32(0),
			str(""),
			str(""),
			f32(1.0f),
			f32(0.01f),
			u32(8),
			u32(terrain.squareSize),
			RawField{RawField::Type::Int, 8, terrain.repeat ? 1u : 0u},
		};
	}

	std::vector<RawField> waterBlockFields(const WaterSettings& water)
	{
		std::vector<RawField> f;
		const auto flag = [&](bool v) { f.push_back(RawField{RawField::Type::Flag, 1, v ? 1u : 0u}); };
		const auto num = [&](uint32_t v, int bits) { f.push_back(RawField{RawField::Type::Int, bits, v}); };
		const auto f32 = [&](float v) {
			RawField r{RawField::Type::F32};
			r.f32 = v;
			f.push_back(r);
		};
		const auto str = [&](std::string text) {
			RawField r{RawField::Type::String};
			r.data = std::move(text);
			f.push_back(r);
		};
		flag(true);
		str("Water");
		for (const float v : water.position)
			f32(v);
		f32(0.0f), f32(0.0f), f32(0.0f); // quaternion x, y, z: no rotation
		flag(false);                      // w is not negative
		for (const float v : water.scale)
			f32(v);
		str(water.surfaceTexture);
		for (int i = 0; i < 4; ++i)
			str(""); // specular mask, environment maps over and under, shore
		str(""), str(""); // submerge textures
		num(water.liquidType, 32);
		// Density, viscosity, wave magnitude, surface opacity, environment map intensity.
		for (const float v : {1.0f, 1.0f, 0.3f, 0.75f, 0.4f})
			f32(v);
		num(0, 8), num(0, 8); // remove wet edges, use depth mask
		// Surface and shore tessellation, parallax, flow angle and rate, distortion
		// grid scale, magnitude and time, shore depth, minimum and maximum alpha,
		// depth gradient.
		for (const float v : {50.0f, 60.0f, 0.5f, 0.0f, 0.0f, 0.1f, 0.05f, 0.5f, 20.0f, 0.03f, 1.0f, 1.0f})
			f32(v);
		for (int i = 0; i < 4; ++i)
			num(255, 8); // specular colour
		f32(6.0f);       // specular power
		num(0, 8);       // ignore terrain
		return f;
	}

	std::vector<std::string> loadTerrain(Terrain& terrain,
										 const std::function<std::optional<std::string>(const std::string&)>& read)
	{
		std::vector<std::string> errors;
		for (int y = 0; y < 16; ++y)
		{
			for (int x = 0; x < 16; ++x)
			{
				auto& slot = terrain.chunks[static_cast<size_t>(y * 16 + x)];
				slot.reset();
				const std::string name = "heights" + std::to_string(x) + "-" + std::to_string(y) + ".dat";
				auto packed = read(terrain.folder + "/" + name);
				if (!packed)
					continue;
				if (packed->empty() || packed->size() > 1023)
				{
					errors.push_back(name + " is " + std::to_string(packed->size()) + " bytes; a chunk takes 1 to 1023");
					continue;
				}
				std::string plain(4096, '\0');
				uLongf size = static_cast<uLongf>(plain.size());
				if (uncompress(reinterpret_cast<Bytef*>(plain.data()), &size, reinterpret_cast<const Bytef*>(packed->data()),
							   static_cast<uLong>(packed->size())) != Z_OK)
				{
					errors.push_back(name + " does not inflate");
					continue;
				}
				plain.resize(size);
				slot = TerrainChunk{std::move(*packed), terrainStamp(plain)};
			}
		}
		return errors;
	}

	graal3d::Transform facing(const std::array<float, 3>& position, const std::array<float, 3>& target)
	{
		graal3d::Transform t;
		t.position = position;
		const float dx = target[0] - position[0], dy = target[1] - position[1], dz = target[2] - position[2];
		const float flat = std::hypot(dx, dy);
		if (flat == 0.0f && dz == 0.0f)
			return t;
		const float yaw = flat == 0.0f ? 0.0f : std::atan2(-dx, dy);
		const float pitch = std::atan2(dz, flat);
		const float sz = std::sin(yaw / 2), cz = std::cos(yaw / 2);
		const float sx = std::sin(pitch / 2), cx = std::cos(pitch / 2);
		// The quaternion of the turn times the tilt.
		const float w = cz * cx;
		const std::array<float, 3> v{cz * sx, sz * sx, sz * cx};
		const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (len < 1e-6f)
			return t;
		t.axis = {v[0] / len, v[1] / len, v[2] / len};
		t.angle = -2.0f * std::atan2(len, w); // the clients' sense (see the header)
		return t;
	}

	std::array<float, 3> forwardOf(const graal3d::Transform& t)
	{
		const auto [x, y, z] = t.axis;
		const float s = std::sin(-t.angle), c = std::cos(t.angle), k = 1.0f - c;
		// The middle column of the axis-angle matrix.
		return {k * x * y - s * z, k * y * y + c, k * y * z + s * x};
	}

	graal3d::Transform cameraTransform(const World& world)
	{
		const auto offset = world.cameraOffset.value_or(DefaultCameraOffset);
		std::array<float, 3> at = world.spawn.position;
		for (size_t i = 0; i < 3; ++i)
			at[i] += offset[i];
		if (!world.cameraRotation)
			return facing(at, world.spawn.position);
		graal3d::Transform t;
		t.position = at;
		const auto& r = *world.cameraRotation;
		t.axis = {r[0], r[1], r[2]};
		t.angle = r[3];
		return t;
	}

} // namespace graal3d
