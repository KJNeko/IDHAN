#pragma once

#include <curl/curl.h>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#include "IDHANTypes.hpp"

namespace idhan::test
{
class JobEventStream
{
	std::mutex m_mutex {};
	std::condition_variable m_readable {};
	std::vector< std::pair< std::string, Json::Value > > m_events {};
	std::string m_pending {};
	std::string m_content_encoding {};
	std::atomic< bool > m_stop {};
	const std::string m_url;
	const std::string m_auth;
	const std::string m_accept_encoding;
	std::jthread m_thread;

	static std::size_t receiveHeader(
		char* data,
		const std::size_t size,
		const std::size_t count,
		void* context ) noexcept
	{
		try
		{
			const std::string_view header { data, size * count };
			constexpr std::string_view name { "Content-Encoding:" };
			if ( header.size() < name.size() ) return header.size();

			if ( !curl_strnequal( data, name.data(), name.size() ) ) return header.size();

			const auto value { header.substr( name.size() ) };
			const auto first { value.find_first_not_of( " \t\r\n" ) };
			if ( first == std::string_view::npos ) return header.size();

			auto& stream { *static_cast< JobEventStream* >( context ) };
			std::lock_guard lock { stream.m_mutex };
			stream.m_content_encoding = value.substr( first, value.find_last_not_of( " \t\r\n" ) - first + 1 );
			return header.size();
		}
		catch ( ... )
		{
			return 0;
		}
	}

	static std::size_t receive( char* data, const std::size_t size, const std::size_t count, void* context ) noexcept
	{
		auto& stream { *static_cast< JobEventStream* >( context ) };
		try
		{
			stream.m_pending.append( data, size * count );
			for ( auto end = stream.m_pending.find( "\n\n" ); end != std::string::npos;
			      end = stream.m_pending.find( "\n\n" ) )
			{
				std::istringstream frame { stream.m_pending.substr( 0, end ) };
				stream.m_pending.erase( 0, end + 2 );
				std::string name;
				std::string json;
				for ( std::string line; std::getline( frame, line ); )
				{
					if ( line.starts_with( "event: " ) ) name = line.substr( 7 );
					if ( line.starts_with( "data: " ) ) json += line.substr( 6 );
				}
				if ( json.empty() ) continue;

				Json::Value value;
				std::istringstream document { json };
				document >> value;
				std::lock_guard lock { stream.m_mutex };
				stream.m_events.emplace_back( std::move( name ), std::move( value ) );
				stream.m_readable.notify_all();
			}
			return size * count;
		}
		catch ( ... )
		{
			return 0;
		}
	}

	static int cancelled( void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t ) noexcept
	{
		return static_cast< JobEventStream* >( context )->m_stop ? 1 : 0;
	}

	void run()
	{
		std::unique_ptr< CURL, decltype( &curl_easy_cleanup ) > curl { curl_easy_init(), curl_easy_cleanup };
		std::unique_ptr< curl_slist, decltype( &curl_slist_free_all ) > headers {
			curl_slist_append( nullptr, m_auth.c_str() ), curl_slist_free_all
		};
		curl_easy_setopt( curl.get(), CURLOPT_URL, m_url.c_str() );
		curl_easy_setopt( curl.get(), CURLOPT_HTTPHEADER, headers.get() );
		curl_easy_setopt( curl.get(), CURLOPT_ACCEPT_ENCODING, m_accept_encoding.c_str() );
		curl_easy_setopt( curl.get(), CURLOPT_HEADERFUNCTION, receiveHeader );
		curl_easy_setopt( curl.get(), CURLOPT_HEADERDATA, this );
		curl_easy_setopt( curl.get(), CURLOPT_WRITEFUNCTION, receive );
		curl_easy_setopt( curl.get(), CURLOPT_WRITEDATA, this );
		curl_easy_setopt( curl.get(), CURLOPT_NOPROGRESS, 0L );
		curl_easy_setopt( curl.get(), CURLOPT_XFERINFOFUNCTION, cancelled );
		curl_easy_setopt( curl.get(), CURLOPT_XFERINFODATA, this );
		curl_easy_setopt( curl.get(), CURLOPT_TIMEOUT, 20L );
		(void)curl_easy_perform( curl.get() );
	}

  public:

	JobEventStream(
		const std::uint16_t port,
		const std::string& key,
		const std::string_view path = "/events?types=job.status",
		std::string accept_encoding = "identity" ) :
	  m_url( std::format( "http://127.0.0.1:{}{}", port, path ) ),
	  m_auth( "X-API-Key: " + key ),
	  m_accept_encoding( std::move( accept_encoding ) ),
	  m_thread( [ this ] { run(); } )
	{}

	~JobEventStream()
	{
		m_stop = true;
		m_thread.join();
	}

	[[nodiscard]] std::string contentEncoding()
	{
		std::lock_guard lock { m_mutex };
		return m_content_encoding;
	}

	Json::Value waitFor(
		const std::string& event,
		const std::optional< JobID > job = {},
		const std::string& stage = {} )
	{
		std::unique_lock lock { m_mutex };
		const auto matches = [ &event, job, &stage ]( const auto& item )
		{
			if ( item.first != event ) return false;
			if ( job && item.second[ "job_id" ].template as< JobID >() != *job ) return false;
			if ( stage.empty() ) return true;
			for ( const auto& task : item.second[ "progress" ][ "tasks" ] )
				if ( task[ "stage" ].asString() == stage ) return true;
			return false;
		};
		const auto ready = [ this, &matches ] { return std::ranges::any_of( m_events, matches ); };
		if ( !m_readable.wait_for( lock, std::chrono::seconds { 12 }, ready ) ) return {};

		const auto found { std::ranges::find_if( m_events, matches ) };
		auto result { std::move( found->second ) };
		m_events.erase( found );
		return result;
	}
};

} // namespace idhan::test
