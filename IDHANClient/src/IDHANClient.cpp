//
// Created by kj16609 on 7/23/24.
//

#include "idhan/IDHANClient.hpp"

#include <moc_IDHANClient.cpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include <spdlog/spdlog.h>

#include <thread>

#include "logging/qt_formatters/qstring.hpp"

namespace idhan
{

void IDHANClient::attemptQueryVersion()
{
	QNetworkRequest request;

	QUrl url { m_url_template };
	url.setPath( "/version" );

	qDebug() << url;

	request.setUrl( url );

	spdlog::info( "Requesting version info from {}", url.toString() );

	QNetworkReply* reply { m_network.get( request ) };

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[ this, reply ]()
		{
			handleVersionInfo( reply );
			reply->deleteLater();
		} );

	connect(
		reply,
		&QNetworkReply::errorOccurred,
		this,
		[ this, reply ]( QNetworkReply::NetworkError error )
		{
			spdlog::
				error( "Failed to get reply from remote: {}:{}", static_cast< int >( error ), reply->errorString() );

			reply->deleteLater();
		} );
}

void IDHANClient::addKeyHeader( QNetworkRequest& request )
{
	request.setRawHeader( "IDHAN-Api-Key", "lolicon" );
}

IDHANClient& IDHANClient::instance()
{
	return *m_instance;
}

IDHANClient::IDHANClient( const IDHANClientConfig& config ) : QObject( nullptr ), m_config( config )
{
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
	spdlog::info( "Hostname: {}", m_config.hostname );
	spdlog::info( "Port: {}", m_config.port );
	if ( m_config.hostname.empty() ) throw std::runtime_error( "hostname must not be empty" );

	if ( m_instance != nullptr ) throw std::runtime_error( "Only one IDHANClient instance should be created" );

	if ( QCoreApplication::instance() == nullptr )
		throw std::runtime_error(
			"IDHANClient expects a Qt instance. Please use QGuiApplication of QApplication before constructing IDHANClient" );

	m_instance = this;

	m_url_template.setHost( QString::fromStdString( m_config.hostname ) );
	m_url_template.setPort( m_config.port );

	if ( m_config.use_ssl )
	{
		m_url_template.setScheme( "https" );
	}
	else
	{
		m_url_template.setScheme( "http" );
	}

	attemptQueryVersion();
}

QFuture< TagID > IDHANClient::createTag( const std::string& namespace_text, const std::string& subtag_text )
{
	auto promise { std::make_shared< QPromise< TagID > >() };

	QJsonObject object {};
	object[ "namespace" ] = QString::fromStdString( namespace_text );
	object[ "subtag" ] = QString::fromStdString( subtag_text );

	QUrl url { m_url_template };
	url.setPath( "/tag/create" );

	qDebug() << url;

	QNetworkRequest request {};
	addKeyHeader( request );

	QJsonDocument doc { object };

	auto response { m_network.post( request, doc.toJson() ) };

	auto future { promise->future() };

	auto handleResponse = [ promise, response ]()
	{
		// reply will give us a body of json
		const auto data { response->readAll() };

		qDebug() << data;

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise, response ]( QNetworkReply::NetworkError error )
	{
		promise->finish();
		response->deleteLater();
	};

	connect( response, &QNetworkReply::finished, handleResponse );
	connect( response, &QNetworkReply::errorOccurred, handleError );

	return future;
}

QFuture< TagID > IDHANClient::createTag( std::string_view tag_text )
{
	QPromise< TagID > promise {};
}

void IDHANClient::handleVersionInfo( QNetworkReply* reply )
{
	spdlog::info( "Recieved version info" );

	const auto data { reply->readAll() };
	const QJsonDocument json_doc { QJsonDocument::fromJson( data ) };
	const auto json = json_doc.object();

	spdlog::info( "Data recieved: {}", std::string_view( data.data(), data.size() ) );

	const auto version { json[ "idhan_server_version" ].toObject() };
	const auto api_version { json[ "idhan_api_version" ].toObject() };

	spdlog::info(
		"Recieved info from server at {}, IDHAN version: {}, API version: {}",
		reply->url().toString(),
		version[ "string" ].toString(),
		api_version[ "string" ].toString() );
}

} // namespace idhan
