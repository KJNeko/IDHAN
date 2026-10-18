#include "filesystem/renameNoReplace.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <barrier>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>

namespace idhan::test
{

class RenameFixture
{
  protected:

	std::filesystem::path root;

	RenameFixture() :
	  root(
		  std::filesystem::temp_directory_path()
		  / ( "idhan-rename-test-" + std::to_string( std::random_device {}() ) ) )
	{
		REQUIRE( std::filesystem::create_directory( root ) );
	}

	~RenameFixture()
	{
		std::error_code error;
		std::filesystem::remove_all( root, error );
	}

	void write( const std::string& name, const std::string& contents ) const
	{
		std::ofstream file { root / name };
		file << contents;
		file.close();
		REQUIRE( file.good() );
	}

	std::string read( const std::string& name ) const
	{
		std::ifstream file { root / name };
		return { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };
	}
};

TEST_CASE_METHOD( RenameFixture, "Renaming to an absent destination preserves contents", "[filesystem][rename]" )
{
	write( "source", "source bytes" );
	REQUIRE( filesystem::renameNoReplace( root / "source", root / "destination" ).has_value() );
	CHECK_FALSE( std::filesystem::exists( root / "source" ) );
	CHECK( read( "destination" ) == "source bytes" );
}

TEST_CASE_METHOD( RenameFixture, "Renaming refuses an existing destination", "[filesystem][rename]" )
{
	write( "source", "source bytes" );
	write( "destination", "destination bytes" );
	const auto result { filesystem::renameNoReplace( root / "source", root / "destination" ) };
	REQUIRE_FALSE( result.has_value() );
	CHECK( result.error() == std::errc::file_exists );
	CHECK( read( "source" ) == "source bytes" );
	CHECK( read( "destination" ) == "destination bytes" );
}

TEST_CASE_METHOD( RenameFixture, "Renaming refuses a dangling destination symlink", "[filesystem][rename]" )
{
	write( "source", "source bytes" );
	std::filesystem::create_symlink( "missing", root / "destination" );
	const auto result { filesystem::renameNoReplace( root / "source", root / "destination" ) };
	REQUIRE_FALSE( result.has_value() );
	CHECK( result.error() == std::errc::file_exists );
	CHECK( read( "source" ) == "source bytes" );
	CHECK( std::filesystem::read_symlink( root / "destination" ) == "missing" );
}

TEST_CASE_METHOD( RenameFixture, "Concurrent renames preserve the losing source", "[filesystem][rename]" )
{
	write( "first", "first bytes" );
	write( "second", "second bytes" );
	std::barrier start { 2 };
	std::array< std::expected< void, std::error_code >, 2 > results;
	{
		std::jthread first {
			[ & ]()
			{
				start.arrive_and_wait();
				results[ 0 ] = filesystem::renameNoReplace( root / "first", root / "destination" );
			}
		};
		std::jthread second {
			[ & ]()
			{
				start.arrive_and_wait();
				results[ 1 ] = filesystem::renameNoReplace( root / "second", root / "destination" );
			}
		};
	}
	REQUIRE( results[ 0 ].has_value() != results[ 1 ].has_value() );
	const auto winner { results[ 0 ] ? "first" : "second" };
	const auto loser { results[ 0 ] ? "second" : "first" };
	CHECK( read( "destination" ) == std::string { winner } + " bytes" );
	CHECK_FALSE( std::filesystem::exists( root / winner ) );
	CHECK( read( loser ) == std::string { loser } + " bytes" );
	CHECK( results[ results[ 0 ] ? 1 : 0 ].error() == std::errc::file_exists );
}

TEST_CASE_METHOD( RenameFixture, "Renaming reports a missing source", "[filesystem][rename]" )
{
	const auto result { filesystem::renameNoReplace( root / "missing", root / "destination" ) };
	REQUIRE_FALSE( result.has_value() );
	CHECK( result.error() == std::errc::no_such_file_or_directory );
	CHECK_FALSE( std::filesystem::exists( root / "destination" ) );
}

} // namespace idhan::test
