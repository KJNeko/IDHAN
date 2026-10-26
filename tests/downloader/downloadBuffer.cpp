#include <catch2/catch_test_macros.hpp>
#include <sys/resource.h>
#include <sys/wait.h>
#include <csignal>
#include <string_view>
#include "../../IDHANServer/src/downloader/DownloadBuffer.hpp"

using idhan::downloader::DownloadBuffer;

TEST_CASE( "Download buffers validate size and retain mapped bytes after closing", "[downloader][buffer]" )
{
	DownloadBuffer buffer {};
	const std::string_view payload { "downloaded bytes" };
	REQUIRE( buffer.write( std::as_bytes( std::span { payload } ) ).has_value() );
	CHECK_FALSE( buffer.map( payload.size() + 1 ).has_value() );
	auto mapping { buffer.map( payload.size() ) };
	REQUIRE( mapping.has_value() );
	buffer.discard();
	CHECK( std::string_view { reinterpret_cast< const char* >( mapping->get() ), payload.size() } == payload );
	CHECK_FALSE( buffer.map( payload.size() ).has_value() );
}

TEST_CASE( "A deferred disk write failure is rejected before mapping", "[downloader][buffer]" )
{
	// Limit only the child, so this real flush failure cannot affect other tests or build files.
	const pid_t child { ::fork() };
	REQUIRE( child >= 0 );
	if ( child == 0 )
	{
		std::signal( SIGXFSZ, SIG_IGN );
		const rlimit limit { 0, 0 };
		if ( ::setrlimit( RLIMIT_FSIZE, &limit ) != 0 ) ::_exit( 2 );

		DownloadBuffer buffer {};
		const std::string_view payload { "buffered" };
		const auto written { buffer.write( std::as_bytes( std::span { payload } ) ) };
		if ( !written ) ::_exit( 3 );

		const auto mapping { buffer.map( payload.size() ) };
		::_exit( !mapping && mapping.error().contains( "flush" ) ? 0 : 4 );
	}

	int status {};
	REQUIRE( ::waitpid( child, &status, 0 ) == child );
	REQUIRE( WIFEXITED( status ) );
	CHECK( WEXITSTATUS( status ) == 0 );
}
