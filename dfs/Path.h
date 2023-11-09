/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef DFS_PATH_H
#define DFS_PATH_H

#include <algorithm>
#include <array>
#include <ranges>
#include <variant>
#include <vector>
#include <stdexcept>
#include <string>

#include <dfs/overloaded.h>

namespace dfs {

/**
 * A fixed size string type used for passing string as non-type template
 * parameter.
 *
 * \sa parse_path
 */
template <std::size_t N>
struct static_string
{
    char data[N];
    constexpr static_string(const char (&str)[N]) {
        std::ranges::copy(str, data);
    }

    constexpr operator std::string_view() const {
        return {data, N-1};
    }

    constexpr std::string_view str() const {
        return {data, N-1};
    }

    constexpr auto begin() const { return &data[0]; }
    constexpr auto end() const { return &data[N-1]; }
};

/**
 * \defgroup path Path
 *
 * A path is a range of \ref path::item locating a member of type or global
 * object. This is specified by the concept Path.
 *
 * \ref parse_path functions can create a path from a string using the following syntax:
 *  - `.name`: to create a \ref path::identifier
 *  - `.(name)`: to create a \ref path::container_of
 *  - `[index]`: to create a \ref path::index
 *
 *  If the path would begin with a `.`, it should be omitted.
 *
 *  For example, `"global_name.container[1].member"` will create the path:
 *  \code
 *  {
 *      dfs::path::identifier{"global_name"},
 *      dfs::path::identifier{"container"},
 *      dfs::path::index{1},
 *      dfs::path::identifier{"member"}
 *  }
 *  \endcode
 *
 *  \{
 */
namespace path {
/**
 * An identifier for a global object, type or member.
 *
 * \sa item
 */
struct identifier { std::string_view identifier; };
/**
 * For identifying the outermost possibly anonymous type/member containing the
 * member named \c member.
 *
 * \sa item
 */
struct container_of { std::string_view member; };
/**
 * Index in a container. It can be an integer or the name of an enum value if
 * the container is indexed by an enum.
 *
 * \sa item
 */
using index = std::variant<std::size_t, std::string_view>;
/**
 * Any type of path item.
 */
using item = std::variant<identifier, container_of, index>;

}

/**
 * Specifies a path.
 *
 * \sa path
 */
template <typename T>
concept Path =
	std::ranges::input_range<T> &&
	std::convertible_to<std::ranges::range_value_t<T>, path::item>;


namespace path {
/**
 * Make a string from a \ref path according to the syntax from \ref parse_path.
 */
template <Path T>
std::string to_string(T &&path)
{
	std::string r;
	bool first = true;
	for (const auto &item: path) {
		visit(overloaded{
			[&](const path::identifier &id) {
				if (!first)
					r += ".";
				r += id.identifier;
			},
			[&](const path::container_of &c) {
				r += ".(";
				r += c.member;
				r += ")";
			},
			[&](const path::index &idx) {
				r += "[";
				r += visit(overloaded{
					[](std::size_t i) { return std::to_string(i); },
					[](std::string_view i) { return std::string(i); }
				}, idx);
				r += "]";
			},
		}, item);
		first = false;
	}
	return r;
}

};

namespace parser_details {

constexpr bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

constexpr bool is_alpha(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

constexpr bool is_identifier(char c) {
	return c == '_' || is_digit(c) || is_alpha(c);
}

template <typename InputIt>
constexpr std::tuple<std::size_t, InputIt> parse_int(InputIt begin, InputIt end)
{
	std::size_t res = 0;
	InputIt it;
	for (it = begin; it != end; ++it) {
		if (!is_digit(*it))
			break;
		res = 10*res + (*it-'0');
	}
	return {res, it};
}

template <typename F>
constexpr auto parse_path_impl(std::string_view str, F &&f)
{

	bool first = true;
	auto it = str.begin();
	auto end = str.end();
	while (it != end) {
		if (first || *it == '.') {
			if (first)
				first = false;
			else
				++it;
			if (it == end)
				throw std::invalid_argument("unexpected end after '.'");
			bool is_container_of = false;
			if (*it == '(') {
				++it;
				if (it == end)
					throw std::invalid_argument("unexpected end after '(')");
				is_container_of = true;
			}
			if (!is_alpha(*it))
				throw std::invalid_argument("identifer must begin with alphabetic character");
			auto identifier_end = std::find_if_not(it, end, is_identifier);
			if (is_container_of)
				f(std::in_place_type<path::container_of>, std::string_view(it, identifier_end));
			else
				f(std::in_place_type<path::identifier>, std::string_view(it, identifier_end));
			it = identifier_end;
			if (is_container_of) {
				if (it == end || *it != ')')
					throw std::invalid_argument("')' expected");
				++it;
			}
		}
		else if (*it == '[') {
			++it;
			if (it == end)
				throw std::invalid_argument("unexpected end after '['");
			if (is_digit(*it)) {
				auto [value, value_end] = parse_int(it, end);
				f(std::in_place_type<path::index>, value);
				it = value_end;
			}
			else if (is_alpha(*it)) {
				auto identifier_end = std::find_if_not(it, end, is_identifier);
				f(std::in_place_type<path::index>, std::string_view(it, identifier_end));
				it = identifier_end;
			}
			else
				throw std::invalid_argument("index expected");
			if (it == end || *it != ']')
				throw std::invalid_argument("] expected");
			++it;
		}
		else
			throw std::invalid_argument("unexpected character");
	}
	return f;
}

} // namespace parser_details

/**
 * Parse a path from a compile-time \c static_string.
 *
 * \sa dfs::literals::operator""_path.
 */
template <static_string Str>
consteval auto parse_path()
{
	using namespace parser_details;
	constexpr auto count = parse_path_impl(Str, [count = 0](auto &&...) mutable { return count++; })();
	std::array<path::item, count> array;
	parse_path_impl(Str, [it = array.begin()]<typename T>(std::in_place_type_t<T>, auto &&... args) mutable {
		(*it++).template emplace<T>(std::forward<decltype(args)>(args)...);
	});
	return array;
}

/**
 * Parse a path from a run-time string.
 */
std::vector<path::item> parse_path(std::string_view str);

namespace literals
{
/**
 * Create a literal \ref path.
 */
template <static_string Str>
constexpr auto operator""_path() { return parse_path<Str>(); }
}

/// \}

} // namespace dfs

#endif
