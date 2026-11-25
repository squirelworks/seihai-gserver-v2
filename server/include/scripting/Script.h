// Modified by the Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
#ifndef SCRIPT_H
#define SCRIPT_H

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <scripting/ScriptContainers.h>
#include <scripting/ScriptSystem.h>
#include <scripting/ScriptTypes.h>
#include <utilities/CommonTypes.h>
#include <utilities/std/generator.h>
#include <utilities/StringUtils.h>

using namespace std::literals;

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

using ScriptByteCode = std::vector<uint8_t>;
using ScriptByteCodePtr = std::shared_ptr<ScriptByteCode>;

class Script
{
public:
	Script() = default;
	Script(const std::string_view who, const ScriptType type, const std::string& src) noexcept : Script(who, type, std::string{ src }) {}
	Script(const std::string_view who, const ScriptType type, const std::string_view src) noexcept : Script(who, type, std::string{ src }) {}
	Script(const Script& o) noexcept { *this = o; }
	Script(Script&& o) noexcept { *this = std::move(o); }

	Script(const std::string_view who, const ScriptType type, std::string&& src) noexcept
	{
		setOriginalSource(who, type, std::move(src));
	}

	[[a::inline]] Script& operator=(const Script& o) noexcept;
	[[a::inline]] Script& operator=(Script&& o) noexcept;

public:
	[[a::inline]] [[nodiscard]] size_t getHash() const noexcept;
	[[a::inline]] [[nodiscard]] ScriptType getType() const noexcept;
	[[a::inline]] [[nodiscard]] const std::string& getOriginalSource() const noexcept;
	[[a::inline]] [[nodiscard]] const std::string& getModifiedSource() const noexcept;
	[[a::inline]] [[nodiscard]] std::string_view getClientSideSource() const noexcept;
	[[a::inline]] [[nodiscard]] std::string_view getServerSideSource() const noexcept;

public:
	[[a::inline]] [[nodiscard]] std::string_view getClientSideScript() const noexcept;
	[[nodiscard]] const ScriptByteCode& getClientSideByteCode() const noexcept;
	void calculateClientByteCodeChecksums(std::string& outDesKeyPacket, uint32_t& outPlainChecksum, uint32_t& outEncryptedChecksum) const noexcept;

public:
	[[a::inline]] Script& setOriginalSource(std::string_view who, ScriptType type, std::string&& source) noexcept;
	[[a::inline]] Script& setOriginalSource(std::string_view who, ScriptType type, const std::string& source) noexcept;
	[[a::inline]] Script& setModifiedSource(const std::string& source) noexcept;
	[[a::inline]] Script& setClientCompiledScript(CompiledScriptResultPtr script) noexcept;
	[[a::inline]] Script& setServerCompiledScript(CompiledScriptResultPtr script) noexcept;

public:
	[[nodiscard]] std::generator<decltype(ScriptExecutionContext::joinedClasses)::const_reference> getServerJoinedClasses() const noexcept;

public:
	void executeEvents(ScriptContainer& container, const ScriptObject& source) const;
	void executeEvents(ScriptEventQueue& events, const ScriptObject& source) const;
	void executeEvents(clear_container_t, ScriptContainer& container, const ScriptObject& source) const;
	void executeEvents(clear_container_t, ScriptEventQueue& events, const ScriptObject& source) const;
	bool runUserDefinedFunction(std::string_view functionName, ScriptEvent& event, const ScriptObject& source) const;

public:
	static std::string minify(const std::string& src) noexcept;

private:
	std::string m_who;
	std::string m_original_source;
	std::string m_modified_source;
	std::string_view m_clientside_source;
	std::string_view m_serverside_source;
	std::string_view m_clientside_script;
	CompiledScriptResultPtr m_client_compiled;
	CompiledScriptResultPtr m_server_compiled;
	size_t m_hash = 0;
	ScriptType m_type = ScriptType::SERVER;

	void split(std::string& source) noexcept;

	// Seihai: where each source view sits in m_modified_source, so a copy or move can rebuild them in its own string.
	struct ViewOffset
	{
		size_t pos = 0;
		size_t len = 0;
		bool set = false;
	};
	using ViewOffsets = std::array<ViewOffset, 3>; // clientside source, serverside source, clientside script

	[[nodiscard]] ViewOffsets viewOffsets() const noexcept
	{
		const auto offset = [this](std::string_view v) -> ViewOffset {
			const auto base = reinterpret_cast<std::uintptr_t>(m_modified_source.data());
			const auto at = reinterpret_cast<std::uintptr_t>(v.data());
			if (v.data() == nullptr || at < base || at - base > m_modified_source.size() || v.size() > m_modified_source.size() - (at - base))
				return {};
			return {static_cast<size_t>(at - base), v.size(), true};
		};
		return {offset(m_clientside_source), offset(m_serverside_source), offset(m_clientside_script)};
	}

	void setViews(const ViewOffsets& views) noexcept
	{
		const auto view = [this](const ViewOffset& v) -> std::string_view {
			return v.set ? std::string_view{m_modified_source}.substr(v.pos, v.len) : std::string_view{};
		};
		m_clientside_source = view(views[0]);
		m_serverside_source = view(views[1]);
		m_clientside_script = view(views[2]);
	}
	void compileScript() noexcept;
};

//----------------------------

inline size_t Script::getHash() const noexcept
{
	return m_hash;
}

inline  ScriptType Script::getType() const noexcept
{
	return m_type;
}

inline Script& Script::operator=(const Script& o) noexcept
{
	if (this == &o)
		return *this;

	// Seihai: the three views point into o.m_modified_source. Take their offsets there and point them into our
	// own copy; copying the views themselves left them pointing into o, which dangles once o goes away.
	const auto views = o.viewOffsets();
	m_who = o.m_who;
	m_original_source = o.m_original_source;
	m_modified_source = o.m_modified_source;
	setViews(views);
	m_client_compiled = o.m_client_compiled;
	m_server_compiled = o.m_server_compiled;
	m_hash = o.m_hash;
	return *this;
}

inline Script& Script::operator=(Script&& o) noexcept
{
	if (this == &o)
		return *this;

	// Seihai: take the views' offsets before the move. A short source lives inside the std::string itself (the
	// small-string buffer), so after the move o's views point into o, not into our m_modified_source.
	const auto views = o.viewOffsets();
	m_who = std::move(o.m_who);
	m_original_source = std::move(o.m_original_source);
	m_modified_source = std::move(o.m_modified_source);
	setViews(views);
	m_client_compiled = std::move(o.m_client_compiled);
	m_server_compiled = std::move(o.m_server_compiled);
	m_hash = o.m_hash;
	return *this;
}

inline const std::string& Script::getOriginalSource() const noexcept
{
	return m_original_source;
}

inline const std::string& Script::getModifiedSource() const noexcept
{
	return m_modified_source;
}

inline std::string_view Script::getClientSideSource() const noexcept
{
	return m_clientside_source;
}

inline std::string_view Script::getServerSideSource() const noexcept
{
	return m_serverside_source;
}

inline std::string_view Script::getClientSideScript() const noexcept
{
	return m_clientside_script;
}

//----------------------------

inline Script& Script::setOriginalSource(const std::string_view who, const ScriptType type, std::string&& source) noexcept
{
	m_who = who;
	m_type = type;
	m_original_source = std::move(source);
	m_hash = string::string_hash{}(m_original_source);
	return setModifiedSource(m_original_source);
}

inline Script& Script::setOriginalSource(const std::string_view who, const ScriptType type, const std::string& source) noexcept
{
	m_who = who;
	m_type = type;
	m_original_source = source;
	m_hash = string::string_hash{}(m_original_source);
	return setModifiedSource(m_original_source);
}

inline Script& Script::setModifiedSource(const std::string& source) noexcept
{
	m_modified_source = minify(source);
	split(m_modified_source);
	compileScript();
	return *this;
}

inline Script& Script::setClientCompiledScript(CompiledScriptResultPtr script) noexcept
{
	m_client_compiled = std::move(script);
	return *this;
}

inline Script& Script::setServerCompiledScript(CompiledScriptResultPtr script) noexcept
{
	m_server_compiled = std::move(script);
	return *this;
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // SCRIPT_H
