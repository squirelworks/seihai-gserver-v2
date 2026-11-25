// New file (Seihai project, 2026-11-25): lab mutations of what the server sends a
// 3D client. See Graal3DMutation.h.

#include <graal3d/Graal3DMutation.h>

#include <array>
#include <charconv>
#include <random>

namespace graal3d
{
	namespace
	{
		struct Named
		{
			Mutation mutation;
			std::string_view name;
		};
		constexpr std::array<Named, 9> kNames{{
			{Mutation::AcceptProtocol, "accept-protocol"},
			{Mutation::AcceptClassTable, "accept-classtable"},
			{Mutation::EventUnknownClass, "event-unknown-class"},
			{Mutation::EventBadLength, "event-bad-length"},
			{Mutation::GhostUncreated, "ghost-uncreated"},
			{Mutation::GhostDoubleDelete, "ghost-double-delete"},
			{Mutation::GhostIndexWidth, "ghost-index-width"},
			{Mutation::ControlWrongClass, "control-wrong-class"},
			{Mutation::ControlDeleted, "control-deleted"},
		}};
	} // namespace

	std::optional<std::pair<Mutation, uint32_t>> parseMutation(std::string_view text)
	{
		uint32_t seed = 1;
		if (const auto colon = text.find(':'); colon != std::string_view::npos)
		{
			const auto number = text.substr(colon + 1);
			const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), seed);
			if (error != std::errc{} || end != number.data() + number.size())
				return std::nullopt;
			text = text.substr(0, colon);
		}
		for (const auto& n : kNames)
		{
			if (n.name == text)
				return std::pair{n.mutation, seed};
		}
		return std::nullopt;
	}

	std::string_view mutationName(Mutation mutation)
	{
		for (const auto& n : kNames)
		{
			if (n.mutation == mutation)
				return n.name;
		}
		return "none";
	}

	AcceptTweak acceptTweak(Mutation mutation, uint32_t seed, const Profile& profile, std::string* what)
	{
		AcceptTweak tweak;
		std::mt19937 rng(seed);
		std::string said;
		if (mutation == Mutation::AcceptProtocol)
		{
			const std::array<uint32_t, 5> values{profile.protocol - 1, profile.protocol + 1, 0u, 0xFFFFFFFFu, profile.protocol + 100};
			tweak.protocol = values[rng() % values.size()];
			said = "protocol " + std::to_string(*tweak.protocol) + " in place of " + std::to_string(profile.protocol);
		}
		else if (mutation == Mutation::AcceptClassTable)
		{
			switch (rng() % 3)
			{
				case 0:
					tweak.dropEventClasses = 1 + rng() % 3;
					said = "the last " + std::to_string(tweak.dropEventClasses) + " event classes left out";
					break;
				case 1:
					tweak.extraEventClasses = {"SeihaiLabEvent"};
					said = "an extra event class SeihaiLabEvent";
					break;
				default:
					if (!profile.objectClasses.empty())
					{
						const size_t i = rng() % profile.objectClasses.size();
						tweak.renameObject = std::pair{i, std::string("SeihaiLabObject")};
						said = "object class " + std::to_string(i) + " (" + std::string(profile.objectClasses[i]) + ") renamed SeihaiLabObject";
					}
					break;
			}
		}
		if (what)
			*what = said;
		return tweak;
	}
} // namespace graal3d
