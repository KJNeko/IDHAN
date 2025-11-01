//
// Created by kj16609 on 3/7/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "fgl/defines.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< std::vector< RecordID > > IDHANClient::createRecords(
	[[maybe_unused]] std::vector< std::array< std::byte, ( 256 / 8 ) > >& hashes )
{
	std::vector< std::string > hex_hashes {};

	FGL_UNIMPLEMENTED();

	return this->createRecords( hex_hashes );
}

QFuture< std::vector< RecordID > > IDHANClient::createRecords( const std::vector< std::string >& hashes )
{
#ifndef NDEBUG
	for ( const auto& hash : hashes )
	{
		if ( hash.size() != 64 ) throw std::runtime_error( "Invalid hash size" );
	}
#endif

	auto promise { std::make_shared< QPromise< std::vector< RecordID > > >() };

	const auto expected_record_count { hashes.size() };
	if ( hashes.empty() )
	{
		logging::warn(
			"IDHANClient::createRecords, No hashes to create. This is likely not intentional. Must have at least 1 hash!" );
		promise->addResult( std::vector< RecordID > {} );
		promise->finish();
		return promise->future();
	}

	QJsonObject object {};
	QJsonArray array {};
	for ( const auto& hash : hashes ) array.append( QString::fromStdString( hash ) );

	object.insert( "sha256", array );

	auto handleResponse = [ promise, expected_record_count ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		std::vector< RecordID > record_ids {};
		record_ids.reserve( doc.array().size() );

		for ( const auto& row : doc.array() )
		{
			const auto record_id { row.toInteger() };
			record_ids.emplace_back( record_id );
		}

		if ( expected_record_count != record_ids.size() )
		{
			const auto log_msg { format_ns::format(
				"Server responded with incorrect number of record results. Expected {} got {}",
				expected_record_count,
				record_ids.size() ) };

			logging::error( log_msg );

			promise->setException( std::make_exception_ptr( log_msg ) );
		}

		promise->addResult( record_ids );
		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc {};
	doc.setObject( std::move( object ) );

	sendClientPost( std::move( doc ), "/records/create", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

QFuture< std::optional< RecordID > > IDHANClient::getRecordID( const std::string& sha256 )
{
	auto promise { std::make_shared< QPromise< std::optional< RecordID > > >() };

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		const auto& object { doc.object() };

		if ( object[ "found" ].toBool() )
		{
			const auto record_id { doc.object()[ "record_id" ].toInteger() };
			promise->addResult( record_id );
			promise->finish();
			response->deleteLater();
		}
		else
		{
			promise->addResult( std::nullopt );
			promise->finish();
			response->deleteLater();
		}
	};

	QUrl url { "/records/search" };
	QUrlQuery query {};
	query.addQueryItem( "sha256", QString::fromStdString( sha256 ) );
	url.setQuery( query );

	sendClientGet( url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan