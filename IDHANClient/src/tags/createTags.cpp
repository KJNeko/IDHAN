#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "idhan/IDHANClient.hpp"
#include "logging/logger.hpp"
#include "splitTag.hpp"

namespace idhan
{

QFuture< std::vector< TagID > > IDHANClient::createTags( const std::vector< std::string >& tags )
{
	std::vector< std::pair< std::string, std::string > > pairs {};
	pairs.reserve( tags.size() );

	for ( const auto& tag : tags )
	{
		const auto& [ ntag, stag ] = splitTag( tag );
		pairs.emplace_back( ntag, stag );
	}

	return createTags( pairs );
}

QFuture< std::vector< TagID > > IDHANClient::createTags(
	const std::vector< std::pair< std::string, std::string > >& tags )
{
	if ( tags.empty() ) throw std::runtime_error( "IDHANClient::createTags Needs more then 1 tag to make" );

	auto promise { std::make_shared< QPromise< std::vector< TagID > > >() };
	promise->start();

	QJsonArray array {};
	for ( const auto& [ namespace_text, subtag_text ] : tags )
	{
		QJsonObject obj {};
		obj[ "namespace" ] = QString::fromStdString( namespace_text );
		obj[ "subtag" ] = QString::fromStdString( subtag_text );
		array.append( std::move( obj ) );
	}

	const auto expected_count { tags.size() };

	auto handleResponse = [ promise, expected_count ]( auto* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument document { QJsonDocument::fromJson( data ) };

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( expected_count );

		for ( const auto& obj : document.array() )
		{
			const auto& tag_obj = obj.toObject();
			const auto tag_id = tag_obj[ "tag_id" ].toInteger();
			FGL_ASSERT(
				tag_id > 0,
				format_ns::format(
					"Tag ID was invalid being returned from IDHAN Got {} from {}",
					tag_id,
					document.toJson().toStdString() ) );
			tag_ids.emplace_back( static_cast< TagID >( tag_id ) );
		}

		if ( tag_ids.size() != expected_count )
			throw std::runtime_error(
				format_ns::format(
					"IDHAN did not return the correct number of tags back. Expected {} got {}",
					expected_count,
					tag_ids.size() ) );

		promise->addResult( std::move( tag_ids ) );
		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc { array };

	sendClientPost( std::move( doc ), "/tags/create", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan
