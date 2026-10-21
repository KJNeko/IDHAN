#pragma once
#include <expected>
#include <optional>
#include <variant>

#include "Metadata.hpp"

namespace idhan::ipc::binary
{
struct Input
{
	enum class Source : std::uint32_t
	{
		descriptor = 1,
		parent_input = 2
	};
	Source source;
	std::optional< std::uint32_t > fd_index;
	std::optional< std::uint64_t > parent_call_id;
	std::uint64_t offset, size;
	std::optional< std::string > filename;
};

struct Version
{
	std::uint32_t major, minor, patch;
};

struct ManifestEntry
{
	std::uint32_t index;
	std::string name;
	std::uint32_t type_flags;
	Version version;
	bool thread_safe, single_threaded;
	std::uint32_t residency;
	std::uint64_t rss_ceiling_mb;
	std::vector< MimeID > mimes;
	std::optional< std::string > model_name;
	std::optional< std::uint32_t > dimensions;
	std::optional< bool > supports_text;
};

struct Manifest
{
	std::string library_signature;
	std::vector< ManifestEntry > modules;
};

struct ThumbnailOptions
{
	std::uint32_t width, height;
};

struct GenerateOptions
{
	module::SHA256 desired_hash;
};

struct TextOptions
{
	std::string phrase;
};

using CallOptions = std::variant< std::monostate, ThumbnailOptions, GenerateOptions, TextOptions >;

struct Call
{
	std::uint32_t module_index;
	CallOp operation;
	MimeID mime_id;
	std::uint32_t depth;
	std::optional< Input > input;
	CallOptions options;
	std::optional< ExtensionDocument > extra;
};

struct Thumbnail
{
	enum class Format : std::uint32_t
	{
		rgb,
		animated,
		encoded
	};
	std::uint32_t width, height;
	Format format;
	bool cache;
	Input output;
};

struct Embedding
{
	std::uint32_t dimensions;
	std::vector< float > values;
};

struct Capability
{
	MimeID mime_id;
	bool metadata, thumbnailer, generator;
};

struct Error
{
	enum class Code : std::uint32_t
	{
		module_error = 1,
		invalid_input,
		unsupported,
		worker_died,
		protocol_error
	};
	Code code;
	std::string message;
	bool retryable;
};

using ResultValue = std::variant< std::monostate, MetadataInfo, Thumbnail, Input, Embedding, MimeID, Capability >;
using Result = std::expected< ResultValue, Error >;

struct Callback
{
	std::uint64_t callback_id;
	CallbackKind kind;
	std::uint32_t depth;
	Input input;
	CallOptions options;
	std::optional< ExtensionDocument > extra;
};

struct CallbackResult
{
	std::uint64_t callback_id;
	Result result;
};

struct Heartbeat
{
	std::uint64_t rss_kb;
	std::uint32_t active_calls;
};

struct Reclaim
{};

struct Shutdown
{};

using Message = std::variant< Hello, Manifest, Call, Result, Callback, CallbackResult, Heartbeat, Reclaim, Shutdown >;
} // namespace idhan::ipc::binary
