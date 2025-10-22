//
// Created by kj16609 on 5/20/25.
//
#pragma once

#include <filesystem>

#include "fgl/defines.hpp"

namespace idhan
{

class FileMappedData
{
	std::filesystem::path m_path;
	std::size_t m_length;

  public:

	FGL_DELETE_ALL_RO5( FileMappedData );

	explicit FileMappedData( const std::filesystem::path& path_i );

	std::string extension() const;

	std::string name() const;

	std::size_t length() const;

	std::filesystem::path path() const;

	std::string strpath() const;

	~FileMappedData();
};

} // namespace idhan