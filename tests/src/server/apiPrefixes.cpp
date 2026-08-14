
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "api/apiPrefixes.hpp"



//! Extracts the quoted string literals from the API_PREFIXES array in IDHANWeb/src/api/prefixes.ts.
std::set< std::string > readTypeScriptPrefixes()
{
	const std::filesystem::path ts_path { IDHAN_WEB_PREFIXES_TS };

	std::ifstream file { ts_path };
	EXPECT_TRUE( file.is_open() ) << "Could not open " << ts_path;

	const std::string source { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };

	// Narrow to the API_PREFIXES array so WS_PREFIXES and the doc comment can't contribute entries.
	const auto begin { source.find( "export const API_PREFIXES" ) };
	EXPECT_NE( begin, std::string::npos ) << "API_PREFIXES not found in " << ts_path;
	const auto end { source.find( "];", begin ) };
	EXPECT_NE( end, std::string::npos ) << "API_PREFIXES array is not terminated";

	const std::string array_body { source.substr( begin, end - begin ) };

	// Every entry is a quoted path starting with '/'.
	const std::regex entry_pattern { R"~('(/[^']*)')~" };

	std::set< std::string > prefixes {};
	for ( std::sregex_iterator it { array_body.begin(), array_body.end(), entry_pattern }, last {}; it != last; ++it )
	{
		prefixes.insert( ( *it )[ 1 ].str() );
	}

	return prefixes;
}

std::set< std::string > cppPrefixes()
{
	std::set< std::string > prefixes {};
	for ( const auto prefix : idhan::api::api_prefixes ) prefixes.emplace( prefix );
	return prefixes;
}


TEST( ApiPrefixes, TypeScriptAndCppListsAgree )
{
	const auto cpp { cppPrefixes() };
	const auto ts { readTypeScriptPrefixes() };

	ASSERT_FALSE( ts.empty() ) << "Parsed no prefixes out of the TypeScript file; the parser is broken";

	std::vector< std::string > only_in_cpp {};
	std::ranges::set_difference( cpp, ts, std::back_inserter( only_in_cpp ) );

	std::vector< std::string > only_in_ts {};
	std::ranges::set_difference( ts, cpp, std::back_inserter( only_in_ts ) );

	EXPECT_TRUE( only_in_cpp.empty() ) << "In apiPrefixes.hpp but missing from IDHANWeb/src/api/prefixes.ts: "
									   << testing::PrintToString( only_in_cpp )
									   << " — these endpoints will 404 behind the Vite dev proxy.";

	EXPECT_TRUE( only_in_ts.empty() ) << "In IDHANWeb/src/api/prefixes.ts but missing from apiPrefixes.hpp: "
									  << testing::PrintToString( only_in_ts )
									  << " — the SPA fallback will answer these with index.html instead of a 404.";
}

TEST( ApiPrefixes, PrefixesAreSortedAndWellFormed )
{
	// Sorted purely so that additions land in a predictable spot rather than at the end.
	EXPECT_TRUE( std::ranges::is_sorted( idhan::api::api_prefixes ) ) << "api_prefixes should be kept sorted";

	for ( const auto prefix : idhan::api::api_prefixes )
	{
		EXPECT_TRUE( prefix.starts_with( '/' ) ) << prefix << " should start with '/'";
		EXPECT_FALSE( prefix.ends_with( '/' ) ) << prefix << " should not have a trailing '/'";
	}
}

TEST( ApiPrefixes, MatchesOnSegmentBoundaries )
{
	// Exact and sub-path matches are API paths.
	EXPECT_TRUE( idhan::api::isApiPath( "/search" ) );
	EXPECT_TRUE( idhan::api::isApiPath( "/records/1/info" ) );
	EXPECT_TRUE( idhan::api::isApiPath( "/hyapi/get_files/file" ) );

	EXPECT_FALSE( idhan::api::isApiPath( "/searching" ) );
	EXPECT_FALSE( idhan::api::isApiPath( "/tagsomething" ) );

	// Routes the SPA owns.
	EXPECT_FALSE( idhan::api::isApiPath( "/" ) );
	EXPECT_FALSE( idhan::api::isApiPath( "/browse/42" ) );
	EXPECT_FALSE( idhan::api::isApiPath( "/settings" ) );
}
