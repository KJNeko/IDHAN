//
// Created by kj16609 on 5/3/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< IDHANClient::TagRelationshipInfo > IDHANClient::
	getTagRelationships( const TagID tag_id, const TagDomainID domain_id )
{
	const auto promise { std::make_shared< QPromise< TagRelationshipInfo > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );
		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		TagRelationshipInfo results {};

		const auto& object { doc.object() };
		// Process relationship data here according to your TagRelationshipInfo structure

		for ( const auto& aliased_id : object[ "aliased" ].toArray() )
		{
			const auto aliased_id_int = aliased_id.toInt();
			results.m_aliased.emplace_back( aliased_id_int );
		}

		for ( const auto& alias : object[ "aliases" ].toArray() )
		{
			const auto implied_by_id_int = alias.toInt();
			results.m_aliases.emplace_back( implied_by_id_int );
		}

		for ( const auto& parent_id : object[ "parents" ].toArray() )
		{
			const auto parent_id_int = parent_id.toInt();
			results.m_parents.emplace_back( parent_id_int );
		}

		for ( const auto& child_id : object[ "children" ].toArray() )
		{
			const auto child_id_int = child_id.toInt();
			results.m_children.emplace_back( child_id_int );
		}

		for ( const auto& older_id : object[ "older_siblings" ].toArray() )
		{
			const auto older_id_int = older_id.toInt();
			results.m_older_siblings.emplace_back( older_id_int );
		}

		for ( const auto& younger_id : object[ "younger_siblings" ].toArray() )
		{
			const auto younger_id_int = younger_id.toInt();
			results.m_younger_siblings.emplace_back( younger_id_int );
		}

		promise->addResult( std::move( results ) );
		promise->finish();

		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, [[maybe_unused]] QNetworkReply::NetworkError error )
	{
		const std::runtime_error exception { format_ns::format( "Error: {}", response->errorString().toStdString() ) };
		promise->setException( std::make_exception_ptr( exception ) );
		promise->finish();
		response->deleteLater();
	};

	QUrl url {};
	url.setPath( QString( "/tags/%1/%2/relationships" ).arg( domain_id ).arg( tag_id ) );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan