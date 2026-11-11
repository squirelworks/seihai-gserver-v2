// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: GameObject and Sun updates for the Graal 3D world. See Graal3DObjects.h.

#include <graal3d/Graal3DObjects.h>

#include <cstdlib>

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

	void GhostObject::writeUpdate(BitWriter& w, uint32_t mask) const
	{
		if (kind == Kind::Sun)
			writeSunUpdate(w, sun);
		else
			writeGameObjectUpdate(w, object, mask);
	}

	uint32_t GhostObject::changesFrom(const GhostObject& before) const
	{
		if (kind != before.kind)
			return fullMask();
		if (kind == Kind::Sun)
			return sun == before.sun ? 0u : 1u;
		return gameObjectChanges(before.object, object);
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
			else if (w[0] == "playershape" && w.size() == 2)
			{
				world.playerShape = w[1];
				ok = true;
			}
			else if (w[0] == "spawn" && w.size() == 4)
				ok = toFloats(w, 1, world.spawn.position.data(), 3);

			if (!ok)
				world.errors.push_back("line " + std::to_string(lineNo) + ": " + std::string(line));
		}
		return world;
	}

} // namespace graal3d
