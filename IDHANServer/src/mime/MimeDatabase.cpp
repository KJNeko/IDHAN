//
// Created by kj16609 on 12/18/24.
//
#include "MimeDatabase.hpp"

#include "idhanScanMime.hpp"

namespace idhan::mime
{

inline static std::shared_ptr< MimeDatabase > mime_db { nullptr };

MimeDatabase::MimeDatabase()
{}

std::optional< MimeInfo > MimeDatabase::scan( const std::byte* data, std::size_t len )
{
	//TODO: Start scanning using 3rd party scanners first. This allows the user to override information provided to us.

	// We've reached a point where none of the 3rd-party scanners have found a file they accept. Instead we will try out hardcoded scanners.

	return idhanScanMime( data, len );
}

void MimeDatabase::reloadMimeParsers()
{
	updating_flag.store( true );

	update_condition.notify_all();
	updating_flag.store( false );
}

std::shared_ptr< MimeDatabase > getInstance()
{
	if ( mime_db ) return mime_db;

	return mime_db = std::shared_ptr< MimeDatabase >( new MimeDatabase() );
}

} // namespace idhan::mime