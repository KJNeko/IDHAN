//
// Created by kj16609 on 12/18/24.
//
#pragma once
#include <condition_variable>
#include <memory>

#include "MimeInfo.hpp"

namespace idhan::mime
{

class MimeDatabase
{
	MimeDatabase();

	friend std::shared_ptr< MimeDatabase > getInstance();

	std::condition_variable update_condition {};
	std::mutex mutex {};
	std::atomic< bool > updating_flag {};
	std::atomic< int > active_counter {};

  public:

	/**
	 * @brief Scans the given data for a valid mime signature and populates an info struct.
	 * @param data
	 * @param len
	 * @return
	 */
	std::optional< MimeInfo > scan( const std::byte* data, std::size_t len );

	inline std::optional< MimeInfo > scan( const std::vector< std::byte >& data )
	{
		return scan( data.data(), data.size() );
	}

	//! Reloads all the 3rd party mime parsers
	void reloadMimeParsers();
};

std::shared_ptr< MimeDatabase > getInstance();

} // namespace idhan::mime