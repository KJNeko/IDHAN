//
// Created by kj16609 on 5/20/25.
//

#include "FileMappedData.hpp"

#include <sys/mman.h>

#include <fcntl.h>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace idhan
{

FileMappedData::FileMappedData( const std::filesystem::path& path_i ) :
  m_path( path_i ),
  m_length( std::filesystem::file_size( path_i ) ),
  m_fd( open( m_path.c_str(), O_RDONLY ) ),
  m_data( static_cast< std::byte* >( mmap( nullptr, m_length, PROT_READ, MAP_SHARED, m_fd, 0 ) ) )
{
	close( m_fd );
}

FileMappedData::~FileMappedData()
{
	if ( m_data ) munmap( m_data, m_length );
}

} // namespace idhan