#include "log.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/pattern_formatter.h>
#pragma GCC diagnostic pop

#include <string>

namespace idhan::log
{

//! Writes the message, indenting every line after the first by the width of the prefix already written,
//! so multi-line messages stay aligned under their first line.
class AlignedPayloadFlag final : public spdlog::custom_flag_formatter
{
  public:

	void format( const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest ) override
	{
		const std::string padding( dest.size(), ' ' );
		const std::string_view payload { msg.payload.data(), msg.payload.size() };
		std::size_t start {};

		while ( true )
		{
			const auto end { payload.find( '\n', start ) };
			const auto line { payload.substr( start, end == std::string_view::npos ? end : end - start ) };

			dest.append( line.data(), line.data() + line.size() );

			if ( end == std::string_view::npos ) break;

			dest.push_back( '\n' );
			start = end + 1;

			// A trailing newline does not get a line of padding after it.
			if ( start < payload.size() ) dest.append( padding.data(), padding.data() + padding.size() );
		}
	}

	[[nodiscard]] std::unique_ptr< custom_flag_formatter > clone() const override
	{
		return std::make_unique< AlignedPayloadFlag >();
	}
};

std::unique_ptr< spdlog::formatter > makeServerFormatter()
{
	auto formatter { std::make_unique< spdlog::pattern_formatter >() };
	formatter->add_flag< AlignedPayloadFlag >( '*' ).set_pattern( "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %*" );
	return formatter;
}

std::shared_ptr< spdlog::logger > g_server_logger {};
std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > g_server_ring_buffer_sink {};

std::shared_ptr< spdlog::logger > getServerLogger()
{
	return g_server_logger;
}

std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > getServerRingBufferSink()
{
	return g_server_ring_buffer_sink;
}

void setServerLogger(
	std::shared_ptr< spdlog::logger > logger,
	std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > ring_buffer_sink )
{
	g_server_logger = std::move( logger );
	g_server_ring_buffer_sink = std::move( ring_buffer_sink );
}

} // namespace idhan::log
