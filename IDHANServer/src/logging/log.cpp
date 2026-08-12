#include "log.hpp"

namespace idhan::log
{

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
