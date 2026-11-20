// Modified by the Seihai project, 2026-11-20: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).

#ifndef GS1INPUTSTREAM_H
#define GS1INPUTSTREAM_H

#include <string>
#include <string_view>

#include <ANTLRInputStream.h>
#include <misc/Interval.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::gs1
{
///////////////////////////////////////////////////////////////////////////////

/// An ANTLR input stream over GS1 source as bytes.
///
/// GS1 scripts are Latin-1 (Windows-1252) text, not UTF-8. The stock
/// `ANTLRInputStream` decodes UTF-8: a lenient load turns every byte over 127
/// that is not part of a UTF-8 sequence into U+FFFD, and a strict load throws.
/// Here each byte is one code point (0 to 255), and token text is given back
/// as the same bytes, so `é` (0xE9) in a `message` or a sign stays 0xE9.
/// Valid UTF-8 survives too: its bytes are passed through unchanged.
class GS1InputStream : public antlr4::ANTLRInputStream
{
public:
	explicit GS1InputStream(std::string_view bytes)
	{
		_data.clear();
		_data.reserve(bytes.size());
		for (const char c : bytes)
			_data.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
		p = 0;
	}

	std::string getText(const antlr4::misc::Interval& interval) override
	{
		if (interval.a < 0 || interval.b < 0)
			return {};

		const auto start = static_cast<size_t>(interval.a);
		auto stop = static_cast<size_t>(interval.b);
		if (start >= _data.size())
			return {};
		if (stop >= _data.size())
			stop = _data.size() - 1;
		if (stop < start)
			return {};

		std::string out;
		out.reserve(stop - start + 1);
		for (size_t i = start; i <= stop; ++i)
			out.push_back(_data[i] <= 0xFF ? static_cast<char>(static_cast<unsigned char>(_data[i])) : '?');
		return out;
	}
};

} // namespace preagonal::gs1

#endif // GS1INPUTSTREAM_H
