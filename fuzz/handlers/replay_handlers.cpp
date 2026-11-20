// Seihai project, 2026-11-20: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// Packet replay harness (#87). Feeds recorded sessions to the real packet handlers
// on a Server with no socket, the same way fuzz_handlers does, but from files and
// with any compiler, so the sanitizer job (or a lab machine) can run it under
// ASan/UBSan without libFuzzer. Built from the main project with
// -DREPLAY_HANDLERS=ON; see fuzz/README.md.
//
//   replay_handlers [--seeds DIR] FILE...
//
// A FILE ending in .session is a text session (tools/lab/netdump2session.py writes
// them from a PACKET_LOGGING server log):
//   # a comment
//   login <name> <type byte, hex> <version token>
//   <name> <packet id> [<body byte, hex> ...]
// Each login adds a player that stays until the end of the file, so players in one
// session meet in the same level. Any other FILE is a fuzz_handlers input.
//
// --seeds DIR also writes each session player's packets to DIR as fuzz_handlers
// inputs, to seed the fuzzer. Bodies longer than 255 bytes are cut there.

#include "harness.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

namespace
{
	struct Record
	{
		uint8_t id;
		std::string body;
	};

	struct SessionPlayer
	{
		std::shared_ptr<Player> player;
		std::function<void(uint8_t, CString&)> handle;
		std::vector<Record> sent;
		int type = 0;
		int version = 0;
	};

	std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	// The fuzz_handlers kind byte nearest to a replayed client, for --seeds.
	uint8_t kindByteFor(int type, int version)
	{
		if (type & PLTYPE_ANYRC)
			return static_cast<uint8_t>(std::size(kKinds));
		if (type & PLTYPE_ANYNC)
			return static_cast<uint8_t>(std::size(kKinds) + 1);
		uint8_t best = 0;
		for (uint8_t i = 0; i < std::size(kKinds); ++i)
			if (kKinds[i].type == type && kKinds[i].version <= version)
				best = i;
		return best;
	}

	template <typename P>
	void addPlayer(Server* server, SessionPlayer& sp, const std::string& name)
	{
		auto p = makePlayer<P>(server, sp.type, sp.version, name.c_str());
		sp.handle = [p](uint8_t id, CString& body) { p->handlePacket(id, body); };
		sp.player = p;
	}

	// Returns the number of packets replayed, or -1 on a malformed file.
	int replaySession(const std::filesystem::path& path, const std::filesystem::path& seeds)
	{
		Server* server = theServer();
		std::map<std::string, SessionPlayer> players;
		std::istringstream lines(readFile(path));
		std::string line;
		int lineNo = 0, packets = 0;
		while (std::getline(lines, line))
		{
			++lineNo;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::istringstream words(line);
			std::string first;
			if (!(words >> first) || first[0] == '#')
				continue;

			if (first == "login")
			{
				std::string name, typeHex, token;
				if (!(words >> name >> typeHex >> token) || players.contains(name))
				{
					std::cerr << path.string() << ":" << lineNo << ": bad login line\n";
					return -1;
				}
				SessionPlayer& sp = players[name];
				sp.type = 1 << ((std::stoi(typeHex, nullptr, 16) - 32) & 15);
				const CString tok(token.c_str());
				if (sp.type & PLTYPE_ANYRC)
				{
					sp.version = getRCVersionID(tok);
					addPlayer<FuzzRC>(server, sp, name);
					sp.player->account.adminRights = 0xFFFFFFFFu;
				}
				else if (sp.type & PLTYPE_ANYNC)
				{
					sp.version = getNCVersionID(tok);
					addPlayer<FuzzNC>(server, sp, name);
					sp.player->account.adminRights = 0xFFFFFFFFu;
				}
				else
				{
					sp.version = getVersionID(tok);
					addPlayer<FuzzClient>(server, sp, name);
					sp.player->warp("fuzz.nw", LocalPixelPosition{static_cast<int16_t>(30 * 16), static_cast<int16_t>(30 * 16)});
				}
				continue;
			}

			auto it = players.find(first);
			int id = -1;
			if (it == players.end() || !(words >> id) || id < 0 || id >= 224)
			{
				std::cerr << path.string() << ":" << lineNo << ": bad packet line\n";
				return -1;
			}
			std::string body, hex;
			while (words >> hex)
				body.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
			it->second.sent.push_back({static_cast<uint8_t>(id), body});
			if (id == PLI_RAWDATA)
				continue;
			CString packet;
			if (!body.empty())
				packet.write(body.data(), static_cast<int>(body.size()));
			it->second.handle(static_cast<uint8_t>(id), packet);
			++packets;
		}

		for (auto& [name, sp] : players)
		{
			if (!seeds.empty())
			{
				std::string seed(1, static_cast<char>(kindByteFor(sp.type, sp.version)));
				for (const Record& r : sp.sent)
				{
					const size_t len = std::min<size_t>(r.body.size(), 255);
					seed.push_back(static_cast<char>(r.id));
					seed.push_back(static_cast<char>(len));
					seed.append(r.body, 0, len);
				}
				writeFile(seeds / (path.stem().string() + "-" + name + ".bin"), seed);
			}
			server->deletePlayer(sp.player);
		}
		return packets;
	}
}

int main(int argc, char** argv)
{
	std::filesystem::path seeds;
	std::vector<std::filesystem::path> files;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--seeds" && i + 1 < argc)
			seeds = argv[++i];
		else
			files.emplace_back(arg);
	}
	if (files.empty())
	{
		std::cerr << "usage: replay_handlers [--seeds DIR] FILE...\n";
		return 2;
	}

	int total = 0;
	for (const auto& file : files)
	{
		if (file.extension() == ".session")
		{
			const int n = replaySession(file, seeds);
			if (n < 0)
				return 2;
			total += n;
		}
		else
		{
			const std::string data = readFile(file);
			runFuzzInput(reinterpret_cast<const uint8_t*>(data.data()), data.size());
		}
		std::cout << "replayed " << file.string() << "\n";
	}
	std::cout << files.size() << " files, " << total << " session packets\n";
	return 0;
}
