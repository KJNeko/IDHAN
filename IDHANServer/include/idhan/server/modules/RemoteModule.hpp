#pragma once

#include <folly/File.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ModuleCallInput.hpp"
#include "ModuleRuntime.hpp"

namespace idhan::server::modules
{

struct RemoteCall
{
	std::shared_ptr< const ModuleCallInput > input;
	std::uint32_t depth { 0 };
	std::optional< ipc::binary::ExtensionDocument > extra;
};

struct RemoteOutput
{
	ipc::binary::Input input;
	folly::File file;
};

struct RemoteThumbnail
{
	ipc::binary::Thumbnail thumbnail;
	folly::File file;
};

class RemoteModule
{
	ModuleRuntime* m_runtime;
	std::size_t m_library_index;
	ipc::binary::ManifestEntry m_descriptor;

	[[nodiscard]] runtime::Task< WorkerResult > dispatch( ipc::binary::Call, std::vector< folly::File > ) const;

  public:

	RemoteModule( ModuleRuntime&, std::size_t library_index, ipc::binary::ManifestEntry );
	~RemoteModule();
	RemoteModule( RemoteModule&& ) noexcept;
	RemoteModule& operator=( RemoteModule&& ) noexcept;
	RemoteModule( const RemoteModule& ) = delete;
	RemoteModule& operator=( const RemoteModule& ) = delete;

	[[nodiscard]] std::string_view name() const noexcept;
	[[nodiscard]] const ipc::binary::ManifestEntry& descriptor() const noexcept;
	[[nodiscard]] bool handles( MimeID ) const noexcept;

	[[nodiscard]] runtime::Task< std::expected< ipc::binary::MetadataInfo, ipc::binary::Error > > metadata( RemoteCall )
		const;
	[[nodiscard]] runtime::Task< std::expected< RemoteThumbnail, ipc::binary::Error > > thumbnail(
		RemoteCall,
		ipc::binary::ThumbnailOptions ) const;
	[[nodiscard]] runtime::Task< std::expected< RemoteOutput, ipc::binary::Error > > generate(
		RemoteCall,
		module::SHA256 desired_hash ) const;
	[[nodiscard]] runtime::Task< std::expected< MimeID, ipc::binary::Error > > parseMime( RemoteCall ) const;
	[[nodiscard]] runtime::Task< std::expected< ipc::binary::Embedding, ipc::binary::Error > > embed( RemoteCall )
		const;
	[[nodiscard]] runtime::Task< std::expected< ipc::binary::Embedding, ipc::binary::Error > > embedText( std::string )
		const;
};

} // namespace idhan::server::modules
