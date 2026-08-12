#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <unordered_map>

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
			array.append( object );
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

	if ( record_ids.size() != tag_sets.size() )
	{
		logging::warn( "Record vs Tag set mismatch! {} vs {}", record_ids.size(), tag_sets.size() );
		throw std::runtime_error(
			format_ns::format( "Record vs Tag set mismatch! {} vs {}", record_ids.size(), tag_sets.size() ) );
	}

	// Deduplicate tags across all sets into unique_tags, recording each tag's position so the sets
	// can be rebuilt as indices. A hash map keyed on the (namespace, subtag) pair replaces what was
	// an O(n^2) linear scan of unique_tags per tag, which dominated large batches.
	// Combines the two string hashes with boost::hash_combine (as in the server's SHA256.hpp).
	struct TagPairHash
	{
		std::size_t operator()( const std::pair< std::string, std::string >& pair ) const noexcept
		{
			std::size_t seed { 0 };
			seed ^= std::hash< std::string > {}( pair.first ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
			seed ^= std::hash< std::string > {}( pair.second ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
			return seed;
		}
	};

	std::unordered_map< std::pair< std::string, std::string >, std::size_t, TagPairHash > tag_index {};

	for ( const auto& set : tag_sets )
	{
		std::vector< std::size_t > indicies {};
		indicies.reserve( set.size() );
		for ( const auto& tag : set )
		{
			// the mapped value is only consumed when a new entry is inserted, where it equals the
			// index the tag takes once appended to unique_tags just below
			const auto [ itter, inserted ] = tag_index.try_emplace( tag, unique_tags.size() );
			if ( inserted ) unique_tags.emplace_back( tag );
			indicies.emplace_back( itter->second );
		}

		tag_set_indicies.emplace_back( std::move( indicies ) );
	}

	struct State
	{
		std::vector< RecordID > record_ids;
		TagDomainID tag_domain_id;
		std::vector< std::vector< std::size_t > > tag_set_indicies;
		std::size_t expected_tag_count;
	};

	auto state { std::make_shared< State >( State {
		std::move( record_ids ), tag_domain_id, std::move( tag_set_indicies ), unique_tags.size() } ) };

	auto* self { this };

	return this->createTags( unique_tags )
	    .then(
			[ self, state ]( std::vector< TagID > tag_ids ) -> QFuture< void >
			{
				FGL_ASSERT(
					tag_ids.size() == state->expected_tag_count,
					format_ns::format(
						"IDHAN returned not the correct number of tags back, {} != {}",
						tag_ids.size(),
						state->expected_tag_count ) );

				std::vector< std::vector< TagID > > ids {};
				ids.reserve( state->tag_set_indicies.size() );
				for ( const auto& set : state->tag_set_indicies )
				{
					std::vector< TagID > set_ids {};
					set_ids.reserve( set.size() );
					for ( const auto& i : set ) set_ids.emplace_back( tag_ids.at( i ) );
					ids.emplace_back( std::move( set_ids ) );
				}

				return self->addTags( std::move( state->record_ids ), state->tag_domain_id, std::move( ids ) );
			} )
	    .unwrap();
}

} // namespace idhan
