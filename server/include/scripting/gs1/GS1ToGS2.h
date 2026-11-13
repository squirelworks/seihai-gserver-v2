// Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
#ifndef GS1TOGS2_H
#define GS1TOGS2_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::gs1
{
///////////////////////////////////////////////////////////////////////////////

// Seihai: GS1 client scripts for clients that only run GS2 bytecode.
//
// A 2.x/3.x-style world sends an NPC's clientside GS1 as text in the script prop. Clients after 5.07 do not run
// that text: they run GS2 bytecode sent with PLO_NPCBYTECODE (the five-client lab, #27, saw a GS1 level NPC do
// nothing on 5.24r4 and 6.x). This translates the GS1 text into GS2 source with the same meaning, which the gs2-parser
// compiler then turns into bytecode.
//
// It is a C++ port of the GS1 front end of seihai-core's GS2 compiler (scripting/gs2/compile/gs1.rs, seihai-core #6)
// and follows its rules:
//   - top-level event tests become event functions: `if (playerenters) {..}` is `function onPlayerEnters() {..}`, and
//     several blocks testing the same event are merged in source order;
//   - `if (event && guard)` keeps the guard inside the function;
//   - GS1 commands become GS2 calls (`setimg door.png;` is `setimg("door.png");`), or the GS2 statements that do the
//     same (`setstring this.a,x;`, `tokenize`, `setcharprop`, the string-list commands);
//   - message codes in text become joins (`"hi #n"` is `"hi " @ player.nick`);
//   - GS1 names whose GS2 spelling differs are renamed (`timeout` is `this.timeout`, `playerx` is `player.x`).
// A form with no faithful GS2 counterpart is an error rather than a guess, and the script is then not compiled.

struct GS2Translation
{
	std::string source;
	std::vector<std::string> errors;

	[[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Translate a GS1 client script into GS2 source.
[[nodiscard]] GS2Translation translateToGS2(std::string_view gs1Source);

struct ClientByteCode
{
	std::vector<uint8_t> bytecode;
	std::string gs2Source;
	std::vector<std::string> errors;

	[[nodiscard]] bool ok() const noexcept { return !bytecode.empty(); }
};

// Compile a clientside script for a bytecode client. A script that starts with `//#GS2` is already GS2 and is compiled
// as written; anything else is read as GS1 and translated first. The NPC-server's \xa7 line separators are accepted.
[[nodiscard]] ClientByteCode compileClientScript(std::string_view clientSource);

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal::gs1

#endif // GS1TOGS2_H
