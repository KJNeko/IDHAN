#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace idhan::downloader
{

class HttpHeaders
{
	std::vector< std::pair< std::string, std::string > > m_entries {};

  public:

	void add( std::string name, std::string value );
	void set( std::string name, std::string value );
	std::size_t erase( std::string_view name );

	[[nodiscard]] bool contains( std::string_view name ) const;
	[[nodiscard]] std::string_view get( std::string_view name ) const;
	[[nodiscard]] std::vector< std::string_view > all( std::string_view name ) const;

	[[nodiscard]] auto begin() const { return m_entries.begin(); }

	[[nodiscard]] auto end() const { return m_entries.end(); }

	[[nodiscard]] bool empty() const { return m_entries.empty(); }

	[[nodiscard]] std::size_t size() const { return m_entries.size(); }

	void clear() { m_entries.clear(); }
};

} // namespace idhan::downloader
