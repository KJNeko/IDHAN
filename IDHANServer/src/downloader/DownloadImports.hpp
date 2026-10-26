#pragma once

#include <IDHANDownloader/ImportSinkFactory.hpp>

#include <utility>

#include "DownloadBuffer.hpp"
#include "IDHANTypes.hpp"

namespace idhan::downloader
{

class DownloadImports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		DownloadBuffer m_buffer {};
		std::string m_filename {};
		Json::Value m_options {};
		std::string m_url {};
		DownloadSessionID m_session_id {};
		bool m_direct_eligible {};
		bool m_force_download {};

	  public:

		Sink(
			std::string filename,
			Json::Value options,
			std::string url,
			const DownloadSessionID session_id,
			bool direct_eligible,
			bool force_download ) :
		  m_filename( std::move( filename ) ),
		  m_options( std::move( options ) ),
		  m_url( std::move( url ) ),
		  m_session_id( session_id ),
		  m_direct_eligible( direct_eligible ),
		  m_force_download( force_download )
		{}

		Sink( const Sink& ) = delete;
		Sink& operator=( const Sink& ) = delete;

		~Sink() override = default;

		std::expected< void, std::string > write( const std::span< const std::byte > bytes ) override
		{
			return m_buffer.write( bytes );
		}

		std::expected< ImportResult, std::string > finish( const ImportMetadata& metadata ) override;

		void abort() override { m_buffer.discard(); }
	};

  public:

	std::expected< std::optional< ImportResult >, std::string > reuse( const ImportRequest& request ) override;

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		return std::make_unique< Sink >(
			request.options[ "filename" ].isString() ? request.options[ "filename" ].asString() : std::string {},
			request.options,
			request.url,
			static_cast< DownloadSessionID >( request.host_tag ),
			request.direct_eligible,
			request.force_download );
	}
};

} // namespace idhan::downloader
