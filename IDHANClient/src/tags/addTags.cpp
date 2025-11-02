//
// Created by kj16609 on 3/11/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< void > IDHANClient::addTags(
	const RecordID record_id,
	const TagDomainID tag_domain_id,
	std::vector< std::pair< std::string, std::string > >&& tags )
{
	if ( tags.empty() )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	auto convertTagsToArray = []( const auto& tag_list ) -> QJsonArray
	{
		QJsonArray array {};
		for ( const auto& [ namespace_t, subtag_t ] : tag_list )
		{
			QJsonObject object {};
			object[ "namespace" ] = QString::fromStdString( namespace_t );
			object[ "subtag" ] = QString::fromStdString( subtag_t );
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

	const QString path { QString::fromStdString( format_ns::format( "/records/{}/tags/add", record_id ) ) };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID tag_domain_id,
	std::vector< std::vector< TagID > >&& tag_sets )
{
	if ( record_ids.empty() )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

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

	const QString path { "/records/tags/add" };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID tag_domain_id,
	std::vector< std::vector< std::pair< std::string, std::string > > >&& tag_sets )
{
	std::vector< std::pair< std::string, std::string > > unique_tags {};
	std::vector< std::vector< std::size_t > > tag_set_indicies {};

	std::size_t counter { 0 };

	if ( record_ids.size() != tag_sets.size() )
	{
		logging::warn( "Record vs Tag set mismatch! {} vs {}", record_ids.size(), tag_sets.size() );
		throw std::runtime_error(
			format_ns::format( "Record vs Tag set mismatch! {} vs {}", record_ids.size(), tag_sets.size() ) );
	}

	for ( const auto& set : tag_sets )
	{
		std::vector< std::size_t > indicies {};
		for ( const auto& tag : set )
		{
			if ( const auto itter = std::ranges::find_if(
					 unique_tags,
					 [ &tag ]( const std::pair< std::string, std::string >& pair ) noexcept -> bool
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

	FGL_ASSERT(
		tag_ids.size() == unique_tags.size(),
		format_ns::format(
			"IDHAN returned not the correct number of tags back, {} != {}", tag_ids.size(), unique_tags.size() ) );

	for ( const auto& set : tag_set_indicies )
	{
		std::vector< TagID > set_ids {};
		for ( const auto& i : set )
		{
			set_ids.emplace_back( tag_ids.at( i ) );
		}

		ids.emplace_back( std::move( set_ids ) );
	}

	return addTags( std::move( record_ids ), tag_domain_id, std::move( ids ) );
}

} // namespace idhan
