//
// Created by kj16609 on 5/20/25.
//
#pragma once

#include <filesystem>

namespace idhan
{

class FileMappedData
{
	std::filesystem::path m_path;
	std::size_t m_length;
	int m_fd;
	std::byte* m_data;

  public:

	FileMappedData() = delete;

	explicit FileMappedData( const std::filesystem::path& path_i );

	std::string extension() const { return m_path.extension().string(); }

	std::string name() const { return m_path.stem().string(); }

	std::size_t length() const { return m_length; }

	std::byte* data() const { return m_data; }

	std::string strpath() const { return m_path.string(); }

	~FileMappedData();
};

} // namespace idhan