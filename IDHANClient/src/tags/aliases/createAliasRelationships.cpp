//
// Created by kj16609 on 3/11/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::createAliasRelationship( TagDomainID tag_domain_id, TagID aliased_id, TagID alias_id )
{
	std::vector< std::pair< TagID, TagID > > pairs {};
	pairs.push_back( std::make_pair( aliased_id, alias_id ) );
	return IDHANClient::createAliasRelationship( tag_domain_id, std::move( pairs ) );
}

QFuture< void > IDHANClient::
	createAliasRelationship( const TagDomainID tag_domain_id, std::vector< std::pair< TagID, TagID > >&& pairs )
{
	if ( pairs.size() == 0 )
	{
		
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

	QJsonArray array {};

	for ( const auto& [ aliased_id, alias_id ] : pairs )
	{
		QJsonObject object {};
		object[ "aliased_id" ] = static_cast< qint64 >( aliased_id );
		object[ "alias_id" ] = static_cast< qint64 >( alias_id );
		array.append( object );
	}

	QJsonDocument doc {};
	doc.setArray( array );

	QUrl url {};
	url.setPath( "/tags/alias/create" );
	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domain_id ) );
	url.setQuery( query );

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* reply, QNetworkReply::NetworkError error )
	{
		logging::error( reply->errorString().toStdString() );

		const std::runtime_error exception { format_ns::format( "Error: {}", reply->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan