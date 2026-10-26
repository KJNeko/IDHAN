#include "idhan/http/HTTPConfig.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace idhan::http
{
HTTPConfig HTTPConfig::defaults()
{
	HTTPConfig config;
	const auto hardware_threads { std::max( 1U, std::thread::hardware_concurrency() ) };
	config.io_threads = std::max( 2U, hardware_threads / 4U );
	config.application_threads = std::max( 2U, hardware_threads / 2U );
	config.heavy_threads = std::max( 1U, hardware_threads / 4U );
	return config;
}

void HTTPConfig::validate() const
{
	if ( ipv4_listen.empty() && ipv6_listen.empty() )
		throw std::invalid_argument( "At least one HTTP listener is required" );

	if ( io_threads == 0 || application_threads == 0 || heavy_threads == 0 )
		throw std::invalid_argument( "HTTP executor counts must be positive" );

	if ( upload_idle_timeout.count() < 0 || response_idle_timeout.count() < 0 || keepalive_timeout.count() < 0 )
		throw std::invalid_argument( "HTTP idle timeouts must not be negative" );

	if ( max_concurrent_streams == 0 )
		throw std::invalid_argument( "HTTP maximum concurrent streams must be positive" );

	if ( stream_receive_window == 0 || stream_receive_window > 0x7fffffffU )
		throw std::invalid_argument( "HTTP stream receive window is outside the supported range" );

	if ( connection_receive_window < 65535 || connection_receive_window > 0x7fffffffU )
		throw std::invalid_argument( "HTTP connection receive window is outside the supported range" );

	if ( gzip_level < 0 || gzip_level > 9 ) throw std::invalid_argument( "HTTP gzip level must be between 0 and 9" );
}
} // namespace idhan::http
