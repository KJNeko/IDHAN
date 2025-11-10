//
// Created by kj16609 on 11/5/25.
//

#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "IDHANTypes.hpp"
#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::setDuplicates( const RecordID worse_duplicate, const RecordID better_duplicate )
{
	const std::vector< std::pair< RecordID, RecordID > > duplicates { { worse_duplicate, better_duplicate } };
	return setDuplicates( duplicates );
}

QFuture< void > IDHANClient::setDuplicates( const std::vector< std::pair< RecordID, RecordID > >& pairs )
{
	QJsonArray json_pairs {};

	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		QJsonObject json_pair {};
		json_pair[ "worse_id" ] = worse_id;
		json_pair[ "better_id" ] = better_id;

		json_pairs.append( json_pair );
	}

	auto promise { std::make_shared< QPromise< void > >() };

	auto handleResponse = [ promise ]( QNetworkReply* response ) -> void
	{
		const auto data = response->readAll();
		if ( !response->isFinished() )
		{
			logging::info( "Failed to read response" );
			throw std::runtime_error( "Failed to read response" );
		}

		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc {};
	doc.setArray( std::move( json_pairs ) );

	sendClientPost( std::move( doc ), "/relationships/duplicates/add", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan