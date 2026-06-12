//
// Created by kj16609 on 5/24/23.
//

#pragma once
#ifndef ATLASGAMEMANAGER_COLUMN_HPP
#define ATLASGAMEMANAGER_COLUMN_HPP

#include <string>

#include "Transaction.hpp"
#include "core/fgl/string_literal.hpp"

namespace atlas::database::utility
{
	template < auto Data >
	consteval const auto& make_static()
	{
		return Data;
	}

	template < fgl::string_literal column, fgl::string_literal table_name, fgl::string_literal table_key_name >
	static consteval std::string_view update_query()
	{
		constexpr fgl::string_literal begin { "UPDATE " };
		constexpr fgl::string_literal set { " SET " };
		constexpr fgl::string_literal where { " = ? WHERE " };
		constexpr fgl::string_literal end { " = ?" };
		constexpr auto& static_data {
			make_static< begin + table_name + set + column + where + table_key_name + end >()
		};
		return std::string_view( static_data.begin(), static_data.size() - 1 );
	}

	template < fgl::string_literal column, fgl::string_literal table_name, fgl::string_literal table_key_name >
	static consteval std::string_view select_query()
	{
		constexpr fgl::string_literal begin { "SELECT " };
		constexpr fgl::string_literal from { " FROM " };
		constexpr fgl::string_literal where { " WHERE " };
		constexpr fgl::string_literal end { " = ?" };

		constexpr auto& static_data {
			make_static< begin + column + from + table_name + where + table_key_name + end >()
		};

		return std::string_view( static_data.begin(), static_data.size() - 1 );
	}

	template < fgl::string_literal str, fgl::string_literal last >
	consteval auto combineStringLiteralCSV()
	{
		constexpr fgl::string_literal comma { ", " };
		return str + comma + last;
	}

	template < fgl::string_literal str, fgl::string_literal... rest >
		requires( sizeof...( rest ) > 1 )
	consteval auto combineStringLiteralCSV()
	{
		constexpr fgl::string_literal comma { ", " };
		return str + comma + combineStringLiteralCSV< rest... >();
	}

	template < fgl::string_literal table, fgl::string_literal table_key_name, fgl::string_literal... columns >
	static consteval std::string_view select_query_t()
	{
		constexpr fgl::string_literal begin { "SELECT " };
		constexpr fgl::string_literal from { " FROM " };
		constexpr fgl::string_literal where { " WHERE " };
		constexpr fgl::string_literal end { " = ?" };

		constexpr auto& static_data { make_static<
			begin + combineStringLiteralCSV< columns... >() + from + table + where + table_key_name + end >() };

		return std::string_view( static_data.begin(), static_data.size() - 1 );
	}
} // namespace atlas::database::utility

#endif //ATLASGAMEMANAGER_COLUMN_HPP
