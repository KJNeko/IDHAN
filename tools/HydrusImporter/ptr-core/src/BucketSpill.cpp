#include "ptr/flatten/BucketSpill.hpp"

#include <spdlog/spdlog.h>

#include <format>
#include <stdexcept>
#include <utility>

namespace idhan::hydrus::ptr
{

void BucketWriter::FileCloser::operator()( std::FILE* const file ) const noexcept
{
	if ( file != nullptr ) std::fclose( file );
}

std::filesystem::path bucketPath( const std::filesystem::path& dir, const std::size_t bucket )
{
	return dir / std::format( "{:04}.bucket", bucket );
}

std::vector< MappingEvent > readBucket( const std::filesystem::path& path )
{
	std::error_code ec;
	const auto size = std::filesystem::file_size( path, ec );
	if ( ec ) return {};

	if ( size % sizeof( MappingEvent ) != 0 )
		throw std::runtime_error(
			std::format( "Bucket {} is {} bytes, not a whole number of events", path.string(), size ) );

	std::vector< MappingEvent > events( size / sizeof( MappingEvent ) );
	if ( events.empty() ) return events;

	std::FILE* const file = std::fopen( path.c_str(), "rb" );
	if ( file == nullptr ) throw std::runtime_error( std::format( "Failed to open bucket {}", path.string() ) );

	const auto read = std::fread( events.data(), sizeof( MappingEvent ), events.size(), file );
	std::fclose( file );

	if ( read != events.size() )
		throw std::runtime_error(
			std::format( "Short read on bucket {}: got {} of {} events", path.string(), read, events.size() ) );

	return events;
}

BucketWriter::BucketWriter( std::filesystem::path dir, const std::size_t buffer_events ) :
  m_dir( std::move( dir ) ),
  m_buffer_events( buffer_events == 0 ? 1 : buffer_events )
{
	std::filesystem::create_directories( m_dir );

	m_files.resize( BUCKET_COUNT );
	m_buffers.resize( BUCKET_COUNT );
	for ( auto& buffer : m_buffers ) buffer.reserve( m_buffer_events );
}

BucketWriter::~BucketWriter()
{
	try
	{
		flush();
	}
	catch ( const std::exception& e )
	{
		spdlog::error( "BucketWriter failed to flush during destruction: {}", e.what() );
	}
}

void BucketWriter::write( const MappingEvent& event )
{
	const auto bucket = bucketFor( event.hash_id );
	m_buffers[ bucket ].push_back( event );
	++m_written;

	if ( m_buffers[ bucket ].size() >= m_buffer_events ) flushBucket( bucket );
}

void BucketWriter::flush()
{
	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket ) flushBucket( bucket );
}

void BucketWriter::flushBucket( const std::size_t bucket )
{
	auto& buffer = m_buffers[ bucket ];
	if ( buffer.empty() ) return;

	// Opened lazily: a corpus never touches every bucket evenly, and 4096 simultaneously open
	// descriptors is close enough to the default rlimit to be worth avoiding until needed.
	if ( m_files[ bucket ] == nullptr )
	{
		const auto path = bucketPath( m_dir, bucket );
		FilePtr file { std::fopen( path.c_str(), "ab" ) };
		if ( file == nullptr ) throw std::runtime_error( std::format( "Failed to open bucket {}", path.string() ) );
		m_files[ bucket ] = std::move( file );
	}

	const auto wrote = std::fwrite( buffer.data(), sizeof( MappingEvent ), buffer.size(), m_files[ bucket ].get() );
	if ( wrote != buffer.size() )
		throw std::runtime_error(
			std::format( "Short write on bucket {}: {} of {} events", bucket, wrote, buffer.size() ) );

	// The stream is buffered by libc too; flushing here keeps a reader that opens the file
	// immediately after flush() from seeing a partial tail.
	std::fflush( m_files[ bucket ].get() );

	buffer.clear();
}

} // namespace idhan::hydrus::ptr
