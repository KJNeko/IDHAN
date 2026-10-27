#pragma once

#include <sys/mman.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unistd.h>
#include <utility>

namespace idhan::downloader
{
class DownloadBuffer
{
	struct FileCloser
	{
		void operator()( std::FILE* file ) const noexcept { std::fclose( file ); }
	};

	std::unique_ptr< std::FILE, FileCloser > m_file {};
	bool m_discarded {};

	std::expected< void, std::string > open()
	{
		if ( m_discarded ) return std::unexpected( "The download was discarded" );

		if ( m_file ) return {};

		std::string name { ( std::filesystem::temp_directory_path() / "idhan-download-XXXXXX" ).string() };
		const int descriptor { ::mkstemp( name.data() ) };
		if ( descriptor < 0 ) return std::unexpected( std::string { std::strerror( errno ) } );

		// Unlink immediately: the descriptor owns the file even if importing throws or the process exits.
		if ( ::unlink( name.c_str() ) != 0 )
		{
			const std::string error { std::strerror( errno ) };
			::close( descriptor );
			return std::unexpected( error );
		}

		m_file.reset( ::fdopen( descriptor, "wb+" ) );
		if ( !m_file )
		{
			const std::string error { std::strerror( errno ) };
			::close( descriptor );
			return std::unexpected( error );
		}

		return {};
	}

  public:

	struct Unmap
	{
		std::size_t size {};

		void operator()( const std::byte* bytes ) const noexcept
		{
			::munmap( const_cast< std::byte* >( bytes ), size );
		}
	};

	using Mapping = std::unique_ptr< const std::byte, Unmap >;

	std::expected< void, std::string > write( const std::span< const std::byte > bytes )
	{
		const auto opened_e { open() };
		if ( !opened_e.has_value() ) return std::unexpected( opened_e.error() );

		if ( std::fwrite( bytes.data(), 1, bytes.size(), m_file.get() ) != bytes.size() )
			return std::unexpected( std::string { std::strerror( errno ) } );

		return {};
	}

	std::expected< Mapping, std::string > map( const std::size_t size )
	{
		if ( m_discarded ) return std::unexpected( "The download was discarded" );

		if ( !m_file )
		{
			if ( size == 0 ) return Mapping { nullptr, Unmap {} };

			return std::unexpected( "Nothing was written for the download" );
		}

		if ( std::fflush( m_file.get() ) != 0 )
			return std::unexpected( std::string { "Unable to flush download: " } + std::strerror( errno ) );

		struct stat status {};
		if ( ::fstat( ::fileno( m_file.get() ), &status ) != 0 )
			return std::unexpected( std::string { std::strerror( errno ) } );

		if ( status.st_size < 0 || static_cast< std::uintmax_t >( status.st_size ) != size )
			return std::unexpected( "Downloaded file size does not match received bytes" );

		if ( size == 0 ) return Mapping { nullptr, Unmap {} };

		void* mapping { ::mmap( nullptr, size, PROT_READ, MAP_PRIVATE, ::fileno( m_file.get() ), 0 ) };
		if ( mapping == MAP_FAILED ) return std::unexpected( std::string { std::strerror( errno ) } );

		return Mapping { static_cast< const std::byte* >( mapping ), Unmap { size } };
	}

	void discard() noexcept
	{
		m_discarded = true;
		m_file.reset();
	}
};
} // namespace idhan::downloader
