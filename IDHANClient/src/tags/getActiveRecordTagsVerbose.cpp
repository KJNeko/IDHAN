//
// Created by kj16609 on 6/12/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< std::vector< ActiveTagVerboseInfo > > IDHANClient::getActiveRecordTagsVerbose( RecordID record_id )
{
	const auto path { format_ns::format( "/records/{}/tags/active/verbose", record_id ) };

	auto promise { std::make_shared< QPromise< std::vector< ActiveTagVerboseInfo > > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto array = doc.array();

		std::vector< ActiveTagVerboseInfo > results {};
		results.reserve( static_cast< std::size_t >( array.size() ) );

		for ( const auto& value : array )
		{
			const auto obj = value.toObject();

			ActiveTagVerboseInfo info {};
			info.tag_id = obj[ "tag_id" ].toInt();
			info.tag_domain_id = static_cast< TagDomainID >( obj[ "tag_domain_id" ].toInt() );
			info.is_explicit = obj[ "explicit" ].toBool();

			for ( const auto& id : obj[ "aliased_from" ].toArray() ) info.aliased_from.push_back( id.toInt() );

			for ( const auto& id : obj[ "inherited_from" ].toArray() ) info.inherited_from.push_back( id.toInt() );

			results.push_back( std::move( info ) );
		}

		promise->addResult( std::move( results ) );
		promise->finish();

		response->deleteLater();
	};

	auto handleError =
		[ promise ]( QNetworkReply* response, [[maybe_unused]] QNetworkReply::NetworkError error, std::string msg )
	{
		const std::runtime_error exception { format_ns::format( "Error: {}", msg ) };
		promise->setException( std::make_exception_ptr( exception ) );
		promise->finish();

		response->deleteLater();
	};

	QUrl url {};
	url.setPath( QString::fromStdString( path ) );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
