#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::removeParentRelationship(
	const TagDomainID tag_domain_id,
	const TagID parent_id,
	const TagID child_id )
{
	std::vector< std::pair< TagID, TagID > > pairs { std::make_pair( parent_id, child_id ) };
	return IDHANClient::removeParentRelationship( tag_domain_id, std::move( pairs ) );
}

QFuture< void > IDHANClient::removeParentRelationship(
	const TagDomainID tag_domain_id,
	const std::vector< std::pair< TagID, TagID > >& pairs )
{
	if ( pairs.empty() )
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
		array.append( object );
	}

	QJsonDocument doc {};
	doc.setArray( array );

	QUrl url {};
	url.setPath( "/tags/parents/remove" );
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

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan
