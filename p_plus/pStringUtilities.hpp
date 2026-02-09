#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <charconv>
#include <stdexcept>
#include <limits>
#include <cstring>
#include <cctype>
#include <iterator>

namespace pUtils {

	inline std::string toLowerCase(std::string_view input) {
		std::string out;
		out.reserve(input.size());
		std::transform(input.begin(), input.end(), std::back_inserter(out),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	inline constexpr unsigned char asciiLowerBT(unsigned char c) noexcept {
		return (c >= 'A' && c <= 'Z') ? char(c | 0x20) : c;
	}

	inline unsigned char asciiLower(unsigned char c) noexcept {
		return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c;
	}

	inline void mutateToLowerBT(std::string& s) noexcept {
		for (char* p = s.data(), *e = p + s.size(); p != e; ++p) {
			*p = static_cast<char>(asciiLowerBT(static_cast<unsigned char>(*p)));
		}
	}

	inline const char* findChar(const char* str, char chr) noexcept {
		return std::strchr(str, static_cast<unsigned char>(chr));
	}
	inline char* findChar(char* str, char chr) noexcept {
		return const_cast<char*>(findChar(static_cast<const char*>(str), chr));
	}

	// ----------------------------------------------------------------------------
	// Non-mutating formatter: returns string_view slices into the input
	// Layout:  "[path|] [*suffix*] value"
	// ----------------------------------------------------------------------------
	struct FormatParts {
		std::string_view path;   // empty if not present
		std::string_view value;  // required
		std::string_view suffix; // empty if not present
	};

	inline bool formatString(std::string_view in, FormatParts& out) noexcept {

		out = {};

		if (in.empty()) return false;

		// Split optional path (before first '|')
		const size_t pipePos = in.find('|');
		std::string_view rem;
		if (pipePos != std::string_view::npos) {
			out.path = in.substr(0, pipePos);
			if (pipePos + 1 >= in.size()) return false; // nothing after '|'
			rem = in.substr(pipePos + 1);
		}
		else {
			rem = in;
		}

		// Optional *suffix* prefix
		if (!rem.empty() && rem.front() == '*') {
			const size_t second = rem.find('*', 1);
			if (second != std::string_view::npos && second > 1) {
				out.suffix = rem.substr(1, second - 1);
				rem = rem.substr(second + 1);
			}
		}

		if (rem.empty()) return false;
		out.value = rem;
		return true;
	}

	// ----------------------------------------------------------------------------
	// Mutating variant: requires a writable buffer.
	// Splits in-place with '\0' terminators and returns raw pointers.
	// Signature changed to char* to reflect mutation (fixes UB).
	// ----------------------------------------------------------------------------
	inline bool formatString(char* toFormat, const char*& outPath, const char*& outValue, const char*& outSuffix) {

		outPath = nullptr;
		outValue = nullptr;
		outSuffix = nullptr;

		if (!toFormat || !*toFormat) return false;

		// Optional path before '|'
		char* pipe = findChar(toFormat, '|');
		char* rem = nullptr;

		if (pipe) {
			*pipe = '\0';
			outPath = toFormat;
			rem = pipe + 1;
			if (!*rem) return false;
		}
		else {
			rem = toFormat;
		}

		// Optional *suffix* at start of rem
		if (rem[0] == '*') {
			char* second = findChar(rem + 1, '*');
			if (second && second > rem + 1) {
				*second = '\0';
				outSuffix = rem + 1;
				rem = second + 1;
			}
		}

		if (!*rem) return false;
		outValue = rem;
		return true;
	}

}