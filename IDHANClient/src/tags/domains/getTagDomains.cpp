//
// Created by kj16609 on 2/20/25.
//
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< std::vector< TagDomainInfo > > IDHANClient::getTagDomains()
{
	auto promise { std::make_shared< QPromise< std::vector< TagDomainInfo > > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const auto wrapped { "{\"domains\":" + data + "}" };
		const QJsonDocument doc { QJsonDocument::fromJson( wrapped ) };
		const auto array = doc[ "domains" ].toArray();

		std::vector< TagDomainInfo > results {};

		for ( const auto& row : array )
		{
			const auto obj = row.toObject();
			results.emplace_back(
				TagDomainInfo { static_cast< TagDomainID >( obj[ "tag_domain_id" ].toInteger() ),
			                    obj[ "domain_name" ].toString().toStdString() } );
		}

		promise->addResult( std::move( results ) );
		promise->finish();
		response->deleteLater();
	};

	sendClientGet( "/tags/domain/list", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan