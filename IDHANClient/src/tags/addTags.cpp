//
// Created by kj16609 on 3/11/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <ranges>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< void > IDHANClient::addTags(
	const RecordID record_id, const TagDomainID domain_id, std::vector< std::pair< std::string, std::string > >&& tags )
{
	if ( tags.empty() )
	{
		return QtFuture::makeReadyVoidFuture();
	}

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	constexpr auto NAMESPACE_KEY { "namespace" };
	constexpr auto SUBTAG_KEY { "subtag" };

	auto convertTagsToArray = []( const auto& tags ) -> QJsonArray
	{
		QJsonArray array {};
		for ( const auto& [ namespace_t, subtag_t ] : tags )
		{
			QJsonObject object {};
			object[ NAMESPACE_KEY ] = QString::fromStdString( namespace_t );
			object[ SUBTAG_KEY ] = QString::fromStdString( subtag_t );
		}
		return array;
	};

	QJsonDocument doc {};
	doc.setArray( convertTagsToArray( tags ) );

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* reply, QNetworkReply::NetworkError error )
	{
		logging::error( reply->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", reply->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	const QString path { QString::fromStdString( std::format( "/records/{}/tags/add", record_id ) ) };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	return promise->future();
}

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID domain_id,
	const std::vector< std::pair< std::string, std::string > >& tags )
{
	if ( record_ids.empty() ) return QtFuture::makeReadyVoidFuture();

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	QJsonDocument doc {};

	QJsonObject object {};
	QJsonArray tag_array {};

	for ( const auto& [ namespace_t, subtag_t ] : tags )
	{
		QJsonObject tag_object {};
		tag_object[ "namespace" ] = QString::fromStdString( namespace_t );
		tag_object[ "subtag" ] = QString::fromStdString( subtag_t );
		tag_array.append( tag_object );
	}

	QJsonArray id_array {};
	for ( const auto& record_id : record_ids ) id_array.append( static_cast< qint64 >( record_id ) );

	object[ "tags" ] = std::move( tag_array );
	object[ "records" ] = std::move( id_array );

	doc.setObject( object );

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* reply, QNetworkReply::NetworkError error )
	{
		logging::error( reply->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", reply->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	const QString path { "/records/tags/add" };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	std::string tag_str {};

	for ( std::size_t i = 0; i < tags.size(); ++i )
	{
		const auto& [ namespace_t, subtag_t ] = tags[ i ];
		std::string str {};
		if ( namespace_t == "" )
			str = subtag_t;
		else
			str = std::format( "{}:{}", namespace_t, subtag_t );

		tag_str += str;
		if ( i + 1 != tags.size() ) tag_str += ", ";
	}

	logging::debug( "Submitted {} to be added to {} records", tag_str, record_ids.size() );

	return promise->future();
}

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids, const TagDomainID domain_id, std::vector< std::vector< TagID > >&& tag_sets )
{
	if ( record_ids.empty() ) return QtFuture::makeReadyVoidFuture();

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	QJsonDocument doc {};

	QJsonObject object {};
	QJsonArray tag_sets_array {};

	FGL_ASSERT( tag_sets.size() > 0, "Must have at least 1 set" );

	for ( const auto& set : tag_sets )
	{
		QJsonArray tag_array {};

		for ( const auto& tag_id : set ) tag_array.append( static_cast< qint64 >( tag_id ) );

		tag_sets_array.append( std::move( tag_array ) );
	}

	QJsonArray id_array {};
	for ( const auto& record_id : record_ids ) id_array.append( static_cast< qint64 >( record_id ) );

	FGL_ASSERT( tag_sets_array.size() == id_array.size(), "Tag sets array must match number of records. Mismatched" );

	object[ "sets" ] = std::move( tag_sets_array );
	object[ "records" ] = std::move( id_array );

	doc.setObject( object );

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* reply, QNetworkReply::NetworkError error )
	{
		logging::error( reply->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", reply->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	const QString path { "/records/tags/add" };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	return promise->future();
}

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID domain_id,
	std::vector< std::vector< std::pair< std::string, std::string > > >&& tag_sets )
{
	std::vector< std::pair< std::string, std::string > > unique_tags {};
	std::vector< std::vector< std::size_t > > tag_set_indicies {};

	std::size_t counter { 0 };

	for ( const auto& set : tag_sets )
	{
		std::vector< std::size_t > indicies {};
		for ( const auto& tag : set )
		{
			if ( const auto itter = std::ranges::find_if(
					 unique_tags,
					 [ &tag ]( const std::pair< std::string, std::string >& pair ) -> bool
					 { return pair.first == tag.first && pair.second == tag.second; } );
			     itter != unique_tags.end() )
			{
				indicies.emplace_back( std::distance( unique_tags.begin(), itter ) );
				continue;
			}
			// not found. insert
			unique_tags.emplace_back( tag );
			indicies.emplace_back( unique_tags.size() - 1 );
		}

		tag_set_indicies.emplace_back( indicies );

		counter += set.size();
	}

	auto tags_future { this->createTags( unique_tags ) };

	std::vector< std::vector< TagID > > ids {};

	tags_future.waitForFinished();

	const auto tag_ids { tags_future.result() };

	FGL_ASSERT( tag_ids.size() == unique_tags.size(), "IDHAN returned not the correct number of tags back" );

	for ( const auto& set : tag_set_indicies )
	{
		std::vector< TagID > set_ids {};
		for ( const auto& i : set )
		{
			set_ids.emplace_back( tag_ids.at( i ) );
		}

		ids.emplace_back( std::move( set_ids ) );
	}

	return addTags( std::move( record_ids ), domain_id, std::move( ids ) );
}

} // namespace idhan
