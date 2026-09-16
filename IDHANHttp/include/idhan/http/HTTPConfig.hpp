#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace idhan::http
{
struct HTTPConfig
{
	std::string ipv4_listen { "127.0.0.1" }, ipv6_listen { "::1" };
	std::uint16_t port { 16609 };
	bool tls { true };
	bool self_sign_tls { false };
	std::filesystem::path certificate { "./server.crt" }, private_key { "./server.key" };
	std::filesystem::path temp_path { "/tmp/idhan" };
	std::size_t io_threads { 0 }, application_threads { 0 }, heavy_threads { 0 };
	std::chrono::milliseconds upload_idle_timeout { 60000 }, response_idle_timeout { 60000 },
		keepalive_timeout { 60000 };
	std::uint32_t max_concurrent_streams { 100 };
	std::uint32_t stream_receive_window { 1U << 20 }, connection_receive_window { 16U << 20 };
	int zstd_level { 3 }, gzip_level { 4 };
	[[nodiscard]] static HTTPConfig defaults();
	void validate() const;
};

//! Creates a persistent localhost certificate when explicitly enabled and both credential files are absent.
void ensureSelfSignedCertificate( const HTTPConfig& config );

inline constexpr std::size_t header_limit = 32 * 1024;
inline constexpr std::size_t compression_threshold = 1024;
inline constexpr std::size_t file_chunk_bytes = 256 * 1024;
inline constexpr std::size_t request_memory_limit = 8 * 1024 * 1024;

inline constexpr std::size_t json_body_limit = request_memory_limit;
inline constexpr std::size_t json_chunk_bytes = 256 * 1024;
inline constexpr std::size_t json_queue_chunks = 2;
inline constexpr std::size_t json_initial_capacity = 256 * 1024;
using RequestID = std::uint64_t;
} // namespace idhan::http
