//
// Created by kj16609 on 7/23/24.
//

#include "log.hpp"

namespace idhan::log
{

namespace
{
std::shared_ptr< spdlog::logger > g_server_logger {};
std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > g_server_ring_buffer_sink {};
} // namespace

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
