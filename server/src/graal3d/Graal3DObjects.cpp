// Modified by the Seihai project, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
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

	void GhostObject::writeUpdate(BitWriter& w, uint32_t mask) const
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
			else if (w[0] == "camera" && w.size() == 4)
			{
				std::array<float, 3> offset{};
				ok = toFloats(w, 1, offset.data(), 3);
				if (ok)
					world.cameraOffset = offset;
			}

			if (!ok)
				world.errors.push_back("line " + std::to_string(lineNo) + ": " + std::string(line));
		}
		return world;
	}

} // namespace graal3d
