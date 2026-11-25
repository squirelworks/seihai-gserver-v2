// New file (Seihai project, 2026-11-25): one-shot, seeded mutations of what the
// server sends a 3D client, for fuzzing the real clients in a lab VM
// (`graal3dmutate`, see bin/docs/graal3d.md, "Lab: mutations"). Off by default.

#ifndef GRAAL3DMUTATION_H
#define GRAAL3DMUTATION_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <graal3d/Graal3DProtocol.h>

namespace graal3d
{
	enum class Mutation
	{
		None,
		AcceptProtocol,    // the accept's protocol number is wrong
		AcceptClassTable,  // the accept's class table drops, adds or renames a class
		EventUnknownClass, // an event with a class id the table does not have
		EventBadLength,    // an event whose int14 length does not match its payload
		GhostUncreated,    // an update for a ghost index never created
		GhostDoubleDelete, // two deletes of an index that holds no ghost
		GhostIndexWidth,   // a ghost section with the wrong 4-bit index width
		ControlWrongClass, // the control object names a ghost of the wrong class
		ControlDeleted,    // the control object names an index that holds no ghost
	};

	// "accept-protocol", "ghost-uncreated:42", ...: the kind and its seed (default 1).
	std::optional<std::pair<Mutation, uint32_t>> parseMutation(std::string_view text);
	std::string_view mutationName(Mutation mutation);
	[[nodiscard]] inline bool isAcceptMutation(Mutation m) { return m == Mutation::AcceptProtocol || m == Mutation::AcceptClassTable; }

	// The accept tweak for an accept mutation and seed (empty for the others),
	// and a line saying what it changed.
	AcceptTweak acceptTweak(Mutation mutation, uint32_t seed, const Profile& profile, std::string* what = nullptr);
} // namespace graal3d

#endif
