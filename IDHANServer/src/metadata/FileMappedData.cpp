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

void FileMappedData::openMMap() const
{
	m_fd = open( m_path.c_str(), O_RDONLY );
	if ( m_fd == -1 ) throw std::system_error( errno, std::system_category() );
	m_data = static_cast< std::byte* >( mmap( nullptr, m_length, PROT_READ, MAP_SHARED, m_fd, 0 ) );
	close( m_fd );
}

FileMappedData::FileMappedData( const std::filesystem::path& path_i ) :
  m_path( path_i ),
  m_length( std::filesystem::file_size( path_i ) )
{}

std::byte* FileMappedData::data() const
{
	if ( !m_initalized ) openMMap();
	return m_data;
}

FileMappedData::~FileMappedData()
{
	if ( m_data ) munmap( m_data, m_length );
}

} // namespace idhan