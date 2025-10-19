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
	mutable int m_fd { -1 };
	mutable std::byte* m_data { nullptr };
	bool m_initalized { false };

	void openMMap() const;

  public:

	FGL_DELETE_ALL_RO5( FileMappedData );

	explicit FileMappedData( const std::filesystem::path& path_i );

	std::string extension() const { return m_path.extension().string(); }

	std::string name() const { return m_path.stem().string(); }

	std::size_t length() const { return m_length; }

	std::filesystem::path path() const { return m_path; }

	std::byte* data() const;

	std::string strpath() const { return m_path.string(); }

	~FileMappedData();
};

} // namespace idhan