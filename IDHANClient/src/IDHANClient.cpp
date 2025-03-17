//
// Created by kj16609 on 7/23/24.
//

#include "idhan/IDHANClient.hpp"

#include <QCoreApplication>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include <thread>

#include "logging/logger.hpp"
#include "logging/qt_formatters/qstring.hpp"
#include "spdlog/sinks/stdout_color_sinks-inl.h"

namespace idhan
{

VersionInfo handleVersionInfo( QNetworkReply* reply )
{
	logging::info( "Recieved version info" );

	const auto data { reply->readAll() };
	const QJsonDocument json_doc { QJsonDocument::fromJson( data ) };
	const auto json = json_doc.object();

	// logging::info( "Data recieved: {}", std::string_view( data.data(), data.size() ) );

	const auto version { json[ "idhan_server_version" ].toObject() };
	const auto api_version { json[ "idhan_api_version" ].toObject() };

	logging::info(
		"Recieved info from server at {}, IDHAN version: {}, API version: {}",
		reply->url().toString(),
		version[ "string" ].toString(),
		api_version[ "string" ].toString() );

	VersionInfo info {};
	info.api.str = version[ "string" ].toString().toStdString();
	info.server.str = version[ "string" ].toString().toStdString();

	return info;
}

std::uint8_t attempts { 0 };

QFuture< VersionInfo > IDHANClient::queryVersion()
{
	QNetworkRequest request;

	QUrl url { m_url_template };
	url.setPath( "/version" );

	request.setUrl( url );

	logging::info( "Requesting version info from {}", url.toString() );

	const QByteArray empty_body {};

	QNetworkReply* reply { network.send( GET, request, empty_body ) };

	auto promise { std::make_shared< QPromise< VersionInfo > >() };

	QObject::connect(
		reply,
		&QNetworkReply::finished,
		[ this, reply, promise ]()
		{
			logging::info( "Handling version network response" );
			promise->addResult( handleVersionInfo( reply ) );
			promise->finish();

			reply->deleteLater();
		} );

	QObject::connect(
		reply,
		&QNetworkReply::errorOccurred,
		[ this, reply, promise ]( QNetworkReply::NetworkError error )
		{
			logging::
				error( "Failed to get reply from remote: {}:{}", static_cast< int >( error ), reply->errorString() );

			const auto e { std::runtime_error( "Failed to get reply from remote" ) };

			if ( attempts < 3 )
			{
				std::this_thread::yield();
				std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
				auto future { queryVersion() };

				while ( future.isRunning() && QThread::isMainThread() )
				{
					QCoreApplication::processEvents();
				}

				future.waitForFinished();

				reply->deleteLater();

				if ( future.isFinished() && future.resultCount() > 0 )
				{
					promise->addResult( future.result() );
					return;
				}
			}

			promise->setException( std::make_exception_ptr( e ) );
			promise->finish();

			reply->deleteLater();
		} );

	return promise->future();
}

void IDHANClient::addKeyHeader( QNetworkRequest& request )
{
	request.setRawHeader( "IDHAN-Api-Key", "lolicon" );
}

void IDHANClient::setUrlInfo( QUrl& url )
{
	url.setHost( m_url_template.host() );
	url.setPort( m_url_template.port() );
	url.setScheme( m_url_template.scheme() );
}

IDHANClient& IDHANClient::instance()
{
	return *m_instance;
}

IDHANClient::IDHANClient( const IDHANClientConfig& config ) : m_config( config ), network( nullptr )
{
	logger = spdlog::stdout_color_mt( "client" );
	logger->set_level( spdlog::level::debug );

	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
	logging::info( "Hostname: {}", m_config.hostname );
	logging::info( "Port: {}", m_config.port );
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

	// Get version info from server.
	QFuture< VersionInfo > future { queryVersion() };

	while ( !future.isFinished() && !future.isCanceled() && !future.isSuspended() )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		QCoreApplication::processEvents();
	}

	if ( future.resultCount() < 0 ) throw std::runtime_error( "Failed to get version info" );
	future.waitForFinished();

	const VersionInfo data { future.result() };

	logging::info( "Got version info from server: Server: {}, Api: {}", data.server.str, data.api.str );
}

void IDHANClient::sendClientGet(
	UrlVariant url,
	std::function< void( QNetworkReply* reply ) >&& responseHandler,
	std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler )
{
	QJsonDocument doc {};

	return sendClientJson(
		GET,
		url,
		std::forward< decltype( responseHandler ) >( responseHandler ),
		std::forward< decltype( errorHandler ) >( errorHandler ),
		std::move( doc ) );
}

void handleResponse( QNetworkReply* response, std::function< void( constQJsonDocument& ) > callback )
{
	const auto data { reply->readAll() };
	if ( !reply->isFInished() ) throw std::runtime_error( "Failed to read response" );

	reply->deleteLater();

	callback( QJsonDocument::fromJson( data ) );
}

void IDHANClient::sendClientPost(
	QJsonDocument&& object,
	UrlVariant url,
	std::function< void( QNetworkReply* reply ) >&& responseHandler,
	std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler )
{
	sendClientJson(
		POST,
		url,
		std::forward< std::function< void( QNetworkReply * reply ) > >( responseHandler ),
		std::forward<
			std::function< void( QNetworkReply * reply, QNetworkReply::NetworkError error ) > >( errorHandler ),
		std::forward< QJsonDocument >( object ) );
}

void IDHANClient::sendClientJson(
	const HttpMethod method,
	UrlVariant url_v,
	std::function< void( QNetworkReply* reply ) >&& responseHandler,
	std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler,
	QJsonDocument&& object )
{
	const auto json_str_thing { "application/json" };

	if ( std::holds_alternative< QString >( url_v ) )
	{
		QString path { std::get< QString >( url_v ) };
		QUrl url { m_url_template };
		url.setPath( path );
		url_v = url;
	}

	QUrl url { std::get< QUrl >( url_v ) };

	setUrlInfo( url );

	QNetworkRequest request { url };
	// request.setTransferTimeout( std::chrono::milliseconds( 8000 ) );
	request.setHeader( QNetworkRequest::ContentTypeHeader, json_str_thing );
	request.setRawHeader( "accept", json_str_thing );
	addKeyHeader( request );

	const QJsonDocument doc { std::move( object ) };

	const QByteArray body { doc.toJson() };

	QNetworkReply* response { network.send( method, request, body ) };

	QObject::
		connect( response, &QNetworkReply::finished, [ responseHandler, response ]() { responseHandler( response ); } );

	QObject::connect(
		response,
		&QNetworkReply::errorOccurred,
		[ errorHandler, response, url ]( const QNetworkReply::NetworkError error )
		{
			if ( error == QNetworkReply::NetworkError::OperationCanceledError )
			{
				logging::critical(
					"Operation timed out with request to {}: {}",
					url.toString(),
					response->errorString().toStdString() );
				std::abort();
			}

			logging::warn(
				"This maybe can be ignored: Error occurred with request to {}: {}",
				url.toString(),
				response->errorString().toStdString() );

			// check if this is a special error or not.
			// It should have json if so
			auto header = response->header( QNetworkRequest::ContentTypeHeader );
			if ( header.isValid() && header.toString().contains( "application/json" ) )
			{
				const auto body { response->readAll() };
				QJsonDocument doc { QJsonDocument::fromJson( body ) };
				if ( doc.isObject() )
				{
					QJsonObject object { doc.object() };
					if ( object.contains( "error" ) )
					{
						logging::error( object[ "error" ].toString().toStdString() );
					}
				}
			}

			errorHandler( response, error );
		} );
}

QFuture< void > IDHANClient::createFileCluster(
	const std::filesystem::path& server_path,
	const std::string& cluster_name,
	const std::size_t byte_limit,
	const std::uint16_t ratio,
	const bool readonly )
{
	QJsonObject object {};
	object[ "readonly" ] = readonly;
	object[ "name" ] = QString::fromStdString( cluster_name );
	QJsonObject size {};
	size[ "limit" ] = static_cast< qint64 >( byte_limit );
	object[ "size" ] = size;
	object[ "ratio" ] = ratio;
	object[ "path" ] = QString::fromStdString( server_path.string() );

	auto promise { std::make_shared< QPromise< void > >() };

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, QNetworkReply::NetworkError error )
	{
		logging::logResponse( response );

		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc {};
	doc.setObject( std::move( object ) );

	sendClientPost( std::move( doc ), "/clusters/add", handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
