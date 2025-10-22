//
// Created by kj16609 on 5/20/25.
//

#include "FileMappedData.hpp"

#include <filesystem>

namespace idhan
{

FileMappedData::FileMappedData( const std::filesystem::path& path_i ) :
  m_path( path_i ),
  m_length( std::filesystem::file_size( path_i ) )
{}

std::string FileMappedData::extension() const
{
	return m_path.extension().string();
}

std::string FileMappedData::name() const
{
	return m_path.stem().string();
}

std::size_t FileMappedData::length() const
{
	return m_length;
}

std::filesystem::path FileMappedData::path() const
{
	return m_path;
}

std::string FileMappedData::strpath() const
{
	return m_path.string();
}

FileMappedData::~FileMappedData() = default;

} // namespace idhan