#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan::module
{
using SHA256 = std::array< std::byte, 32 >;
}

namespace idhan::ipc::binary
{
inline constexpr std::uint16_t protocol_major = 1, protocol_minor = 0;
inline constexpr std::size_t header_bytes = 32, envelope_bytes = 24, max_descriptors = 8;
enum class MessageType : std::uint16_t
{
	hello = 1,
	hello_ack = 2,
	manifest = 3,
	call = 4,
	result = 5,
	callback = 6,
	callback_result = 7,
	heartbeat = 8,
	reclaim = 9,
	shutdown = 10
};
enum class CallOp : std::uint32_t
{
	metadata = 1,
	thumb_rgb = 2,
	thumb_file = 3,
	generate = 4,
	embed = 5,
	embed_text = 6,
	mime_parse = 7
};
enum class CallbackKind : std::uint32_t
{
	probe = 1,
	thumbnail = 2,
	generate = 3
};
enum class FieldKind : std::uint8_t
{
	null = 0,
	boolean = 1,
	i32 = 2,
	u32 = 3,
	i64 = 4,
	u64 = 5,
	f32 = 6,
	f64 = 7,
	text = 8,
	bytes = 9,
	record = 10,
	list = 11,
	extension = 12
};

struct ProtocolError
{
	std::size_t offset;
	std::string message;
};

struct Header
{
	std::uint32_t body_bytes, descriptor_count;
	std::array< std::byte, 4 > magic;
	std::uint16_t major, minor;
	MessageType type;
	std::uint16_t flags;
	std::uint64_t call_id;
	std::uint32_t payload_bytes;
};

struct Hello
{
	std::vector< std::uint32_t > supported_capabilities { 1, 2, 3 };
	std::vector< std::uint32_t > required_capabilities { 1, 2, 3 };
	std::uint32_t max_payload { 64U * 1024U * 1024U };
};
} // namespace idhan::ipc::binary
