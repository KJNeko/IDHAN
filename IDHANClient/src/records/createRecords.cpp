#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "fgl/defines.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< std::vector< RecordID > > IDHANClient::createRecords(
	const std::vector< std::array< std::byte, ( 256 / 8 ) > >& hashes )
{
	std::vector< std::string > hex_hashes {};
	hex_hashes.reserve( hashes.size() );

	auto nibbleToHex = []( const uint8_t nibble ) -> char
	{
		switch ( nibble )
		{
			case 0:
				return '0';
			case 1:
				return '1';
			case 2:
				return '2';
			case 3:
				return '3';
			case 4:
				return '4';
			case 5:
				return '5';
			case 6:
				return '6';
			case 7:
				return '7';
			case 8:
				return '8';
			case 9:
				return '9';
			case 10:
				return 'a';
			case 11:
				return 'b';
			case 12:
				return 'c';
			case 13:
				return 'd';
			case 14:
				return 'e';
			case 15:
				return 'f';
			default:
				return '0';
		}
	};

	for ( const auto& hash : hashes )
	{
		std::string hex_string {};
		hex_string.reserve( 64 );
		for ( const auto& byte : hash )
		{
			const auto high { static_cast< uint8_t >( byte ) >> 4 };
			const auto low { static_cast< uint8_t >( byte ) & 0x0F };
			hex_string += nibbleToHex( high );
			hex_string += nibbleToHex( low );
		}
		hex_hashes.emplace_back( std::move( hex_string ) );
	}

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