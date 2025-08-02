//
// Created by kj16609 on 2/20/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::
	createParentRelationship( const TagDomainID tag_domian_id, const TagID parent_id, const TagID child_id )
{
	std::vector< std::pair< TagID, TagID > > pairs { std::make_pair( parent_id, child_id ) };
	return IDHANClient::createParentRelationship( tag_domian_id, std::move( pairs ) );
}

QFuture< void > IDHANClient::
	createParentRelationship( const TagDomainID tag_domian_id, const std::vector< std::pair< TagID, TagID > >& pairs )
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

	for ( const auto& [ parent_id, child_id ] : pairs )
	{
		QJsonObject object {};
		object[ "parent_id" ] = static_cast< qint64 >( parent_id );
		object[ "child_id" ] = static_cast< qint64 >( child_id );
		array.append( std::move( object ) );
	}

	QJsonDocument doc {};
	doc.setArray( std::move( array ) );

	QUrl url {};
	url.setPath( "/tags/parents/create" );
	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domian_id ) );
	url.setQuery( std::move( query ) );

	auto promise { std::make_shared< QPromise< void > >() };

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan
