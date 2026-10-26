#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "filesystem/ThumbnailCache.hpp"

namespace idhan::test
{

class ThumbnailPurgeFixture
{
  protected:

	std::filesystem::path root {
		std::filesystem::temp_directory_path() / ( "idhan-purge-" + std::to_string( std::random_device {}() ) )
	};
	const std::string hash { "ab" + std::string( 62, '1' ) };

	ThumbnailPurgeFixture() { REQUIRE( std::filesystem::create_directory( root ) ); }

	~ThumbnailPurgeFixture()
	{
		std::error_code error {};
		std::filesystem::remove_all( root, error );
	}

	void write( const std::filesystem::path& relative ) const
	{
		std::filesystem::create_directories( ( root / relative ).parent_path() );
		std::ofstream file { root / relative };
		file << "contents";
		file.close();
		REQUIRE( file.good() );
	}
};

TEST_CASE_METHOD(
	ThumbnailPurgeFixture,
	"Thumbnail purge removes only recognized regular files",
	"[filesystem][purge]" )
{
	const auto expected { std::filesystem::path { "tab" } / ( hash + ".256.webp" ) };
	write( expected );
	write( std::filesystem::path { "tab" } / ( hash + ".1.webp" ) );
	write( std::filesystem::path { "tab" } / ( hash + ".4096.webp" ) );
	const auto retained_directory { std::filesystem::path { "tab" } / ( hash + ".512.webp" ) };
	write( retained_directory / "user.txt" );
	const std::vector< std::filesystem::path > retained {
		"user.txt",
		"tab/user.webp",
		"tab/nested/user.txt",
		"tzz/user.txt",
		std::filesystem::path { "tab" } / ( hash + ".0.webp" ),
		std::filesystem::path { "tab" } / ( hash + ".0256.webp" ),
		std::filesystem::path { "tab" } / ( hash + ".4097.webp" ),
		std::filesystem::path { "tab" } / ( hash + ".256.webp.bak" ),
		std::filesystem::path { "tab" } / ( "." + hash + ".256.webp.0.tmp" ),
		std::filesystem::path { "tab/nested" } / ( hash + ".256.webp" ),
		std::filesystem::path { "t00" } / ( hash + ".256.webp" )
	};
	for ( const auto& path : retained ) write( path );
	const auto result { filesystem::clearThumbnailCache( root ) };
	REQUIRE( result.has_value() );
	CHECK( *result == 3 );
	CHECK_FALSE( std::filesystem::exists( root / expected ) );
	CHECK( std::filesystem::is_directory( root / "tab" ) );
	CHECK( std::filesystem::is_directory( root / retained_directory ) );
	CHECK( std::filesystem::is_regular_file( root / retained_directory / "user.txt" ) );
	for ( const auto& path : retained ) CHECK( std::filesystem::is_regular_file( root / path ) );
	CHECK( filesystem::clearThumbnailCache( root ).value() == 0 );
}

TEST_CASE_METHOD( ThumbnailPurgeFixture, "Thumbnail purge preserves symlinks and their targets", "[filesystem][purge]" )
{
	write( "outside/user.txt" );
	std::filesystem::create_directory( root / "tab" );
	std::filesystem::create_symlink( root / "outside/user.txt", root / "tab" / ( hash + ".256.webp" ) );
	const std::string outside_hash { "ac" + std::string( 62, '1' ) };
	write( std::filesystem::path { "outside" } / ( outside_hash + ".256.webp" ) );
	std::filesystem::create_directory_symlink( root / "outside", root / "tac" );
	const auto result { filesystem::clearThumbnailCache( root ) };
	REQUIRE( result.has_value() );
	CHECK( *result == 0 );
	CHECK( std::filesystem::is_symlink( root / "tab" / ( hash + ".256.webp" ) ) );
	CHECK( std::filesystem::is_symlink( root / "tac" ) );
	CHECK( std::filesystem::is_regular_file( root / "outside/user.txt" ) );
	CHECK( std::filesystem::is_regular_file( root / "outside" / ( outside_hash + ".256.webp" ) ) );
	std::filesystem::create_directory_symlink( root / "outside", root / "linked-root" );
	CHECK_FALSE( filesystem::clearThumbnailCache( root / "linked-root" ).has_value() );
}

TEST_CASE_METHOD( ThumbnailPurgeFixture, "Thumbnail purge treats a missing cache as empty", "[filesystem][purge]" )
{
	const auto result { filesystem::clearThumbnailCache( root / "missing" ) };
	REQUIRE( result.has_value() );
	CHECK( *result == 0 );
	CHECK_FALSE( std::filesystem::exists( root / "missing" ) );
}

} // namespace idhan::test
