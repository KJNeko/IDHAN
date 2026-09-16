#include "prescan.hpp"

#include <fcntl.h>

#include "MimeIDs.hpp"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "signatures.hpp"

namespace idhan::mime
{

static runtime::Task< bool > testRule( const Rule& rule, const MimeReader& reader, std::size_t& cursor )
{
	if ( rule.offset != SCAN )
	{
		const auto offset { static_cast< std::size_t >( rule.offset ) };

		for ( const auto pattern : rule.patterns )
		{
			if ( !co_await reader.matchAt( offset, pattern ) ) continue;

			cursor = offset + pattern.size();
			co_return true;
		}

		co_return false;
	}

	for ( const auto pattern : rule.patterns )
	{
		const auto found { co_await reader.find( pattern, cursor, rule.limit ) };

		if ( !found ) continue;

		cursor = *found + pattern.size();
		co_return true;
	}

	co_return false;
}

static runtime::Task< bool > testSignature( const Signature& signature, const MimeReader& reader )
{
	std::size_t cursor { 0 };

	for ( const auto& rule : signature.rules )
	{
		if ( !co_await testRule( rule, reader, cursor ) ) co_return false;
	}

	co_return true;
}

runtime::Task< MimeID > prescanMime( MimeReader reader )
{
	for ( const auto& signature : signatures() )
	{
		if ( !co_await testSignature( signature, reader ) ) continue;

		log::debug( "Pre-scan matched mime id {}", signature.mime_id );
		co_return signature.mime_id;
	}

	log::debug( "Pre-scan matched no signature" );
	co_return mime_ids::UNKNOWN;
}

runtime::Task< MimeID > prescanMimeForPath( std::filesystem::path path )
{
	co_return co_await prescanMime( MimeReader { std::make_shared< FileIOUring >( path ) } );
}

runtime::Task< MimeID > prescanMimeForFd( const int fd, const std::size_t size )
{
	const auto duplicate { ::fcntl( fd, F_DUPFD_CLOEXEC, 0 ) };
	if ( duplicate < 0 ) co_return mime_ids::UNKNOWN;

	co_return co_await prescanMime( MimeReader { std::make_shared< FileIOUring >( duplicate, size ) } );
}

} // namespace idhan::mime
