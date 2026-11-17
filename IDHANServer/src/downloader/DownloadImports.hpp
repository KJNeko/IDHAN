#pragma once

#include <IDHANDownloader/imports/ImportSinkFactory.hpp>

#include <utility>

#include "DownloadBuffer.hpp"
#include "IDHANTypes.hpp"
#include "idhan/SHA256Hasher.hpp"

namespace idhan::downloader
{

class DownloadImports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		DownloadBuffer m_buffer {};
		SHA256Hasher m_hasher {};
		std::string m_filename {};
		Json::Value m_options {};
		std::string m_url {};
		DownloadSessionID m_session_id {};
		bool m_force_download {};
		std::optional< RecordID > m_stored_record {};

	  public:

		Sink(
			std::string filename,
			Json::Value options,
			std::string url,
			const DownloadSessionID session_id,
			bool force_download,
			std::optional< RecordID > stored_record ) :
		  m_filename( std::move( filename ) ),
		  m_options( std::move( options ) ),
		  m_url( std::move( url ) ),
		  m_session_id( session_id ),
		  m_force_download( force_download ),
		  m_stored_record( stored_record )
		{}

		Sink( const Sink& ) = delete;
		Sink& operator=( const Sink& ) = delete;

		~Sink() override = default;

		folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
		{
			const auto written_e { co_await m_buffer.write( bytes ) };
			if ( !written_e.has_value() ) co_return std::unexpected( written_e.error() );

			m_hasher.update( bytes );
			co_return std::expected< void, std::string > {};
		}

		folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata ) override;

		void abort() override { m_buffer.scheduleDiscard(); }
	};

  public:

	folly::coro::Task< std::expected< std::optional< RecordID >, std::string > > findStoredRecord(
		ImportRequest request ) override;

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		const auto& posted_at { request.options[ "postedAt" ] };
		if ( !posted_at.isNull() )
		{
			if ( !posted_at.isInt64() )
				return std::unexpected( "postedAt must be an integer Unix timestamp in seconds" );

			if ( posted_at.asInt64() < 0 || posted_at.asInt64() > 253402300799LL )
				return std::unexpected( "postedAt must be between Unix epoch and year 9999" );
		}

		return std::make_unique< Sink >(
			request.options[ "filename" ].isString() ? request.options[ "filename" ].asString() : std::string {},
			request.options,
			request.url,
			request.session_id,
			request.force_download,
			request.stored_record );
	}
};

} // namespace idhan::downloader
