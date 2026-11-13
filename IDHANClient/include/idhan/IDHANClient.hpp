#pragma once

#include <QCommandLineParser>
#include <QFuture>
#include <QNetworkReply>
#include <QSettings>
#include <QUrlQuery>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <queue>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/IDHANClientExport.hpp"
#include "Network.hpp"
#include "logging/logger.hpp"

namespace spdlog
{
class logger;
}

namespace idhan
{
class SHA256;

struct VersionInfo
{
	struct ServerVersion
	{
		std::size_t major;
		std::size_t minor;
		std::size_t patch;
		std::string str;
	} server;

	struct ApiVersion
	{
		std::size_t major;
		std::size_t minor;
		std::size_t patch;
		std::string str;
	} api;

	QString branch;
	QString build_type;
	QString commit;
};

IDHAN_CLIENT_EXPORT void addIDHANOptions( QCommandLineParser& parser );

using IDHANResponseHandler = std::function< void( QNetworkReply* ) >;
using IDHANErrorHandler = std::function< void( QNetworkReply*, QNetworkReply::NetworkError, std::string server_msg ) >;

class IDHAN_CLIENT_EXPORT IDHANClient
{
	std::shared_ptr< spdlog::logger > m_logger { nullptr };
	static IDHANClient* m_instance;
	QUrl m_url_template {};

	Network network;

	using UrlVariant = std::variant< QString, QUrl >;

	QString m_key {};

  public:

	[[nodiscard]] std::shared_ptr< spdlog::logger > getLogger() const { return m_logger; }

	void setUrlInfo( QUrl& url );

	static IDHANClient& instance();

	QUrl getBaseUrl() const { return m_url_template; }

	void addKeyHeader( QNetworkRequest& request );

	Q_DISABLE_COPY_MOVE( IDHANClient )

	IDHANClient() = delete;

	//! Requires a QCoreApplication instance first; only one client may exist at a time.
	//! client_name names the client's local spdlog logger.
	//! \throws std::runtime_error if a client already exists, no Qt application exists, or hostname is empty.
	IDHANClient(
		const QString& client_name,
		const QString& hostname,
		qint16 port,
		const QString& key,
		bool use_tls = false );

	~IDHANClient();

	//! True if the server responds with valid version info.
	[[nodiscard]] QFuture< bool > validConnection();

	void setAPIKey( const QString& key );

	void openConnection( const QString& hostname, qint16 port, QString key, bool use_tls = false );

	QFuture< std::vector< RecordID > > createRecords( const std::vector< std::array< std::byte, 32 > >& hashes );

	//! Accepts hex-encoded hashes.
	QFuture< std::vector< RecordID > > createRecords( const std::vector< std::string >& hashes );

	//! Accepts a hex SHA-256; returns nullopt if no record matches.
	QFuture< std::optional< RecordID > > getRecordID( const std::string& sha256 );

	//! Selects a random record whose file has a resolved MIME type.
	QFuture< RecordID > getRandomActiveRecord();

	QFuture< VersionInfo > queryVersion();

	//! Creates missing tags and returns IDs in input order.
	QFuture< std::vector< TagID > > createTags( const std::vector< std::string >& tags );
	QFuture< std::vector< TagID > > createTags( const std::vector< std::pair< std::string, std::string > >& tags );

	QFuture< TagID > createTag( const std::string& namespace_text, const std::string& subtag_text );

	QFuture< TagID > createTag( const std::string& tag_text );

	//! Returns stored tag IDs before alias, sibling, and parent resolution.
	QFuture< std::vector< TagID > > getRecordTags( RecordID record_id, TagDomainID tag_domain_id );
	//! Returns tag IDs after alias, sibling, and parent resolution.
	QFuture< std::vector< TagID > > getActiveRecordTags( RecordID record_id, TagDomainID tag_domain_id );

	//! Returns "namespace:subtag" text in input order.
	QFuture< std::vector< std::string > > getTagText( const std::vector< TagID >& tag_ids );

	QFuture< std::string > getTagText( TagID tag_id );

	QFuture< std::vector< std::pair< TagID, std::string > > > autocompleteTag( const QString& text );

	//! Returns at most 8 matches. An empty domain set selects all domains.
	QFuture< std::vector< std::pair< TagID, std::string > > > autocompleteTag(
		const QString& text,
		const std::vector< TagDomainID >& domains,
		const QString& display_type = QStringLiteral( "display" ),
		bool include_unused = true );

	QFuture< void > addTags(
		RecordID record_id,
		TagDomainID tag_domain_id,
		std::vector< std::pair< std::string, std::string > >&& tags );

	QFuture< void > addTags(
		std::vector< RecordID >&& record_ids,
		TagDomainID tag_domain_id,
		std::vector< std::vector< TagID > >&& tag_sets );

	QFuture< void > addTags(
		std::vector< RecordID >&& record_ids,
		TagDomainID tag_domain_id,
		std::vector< std::vector< std::pair< std::string, std::string > > >&& tag_sets );

	QFuture< void > removeTags( RecordID record_id, TagDomainID tag_domain_id, const std::vector< TagID >& tag_ids );

	QFuture< void > removeTags(
		std::vector< RecordID >&& record_ids,
		TagDomainID tag_domain_id,
		std::vector< std::vector< TagID > >&& tag_sets );

	QFuture< void > setAlternatives( std::vector< RecordID >& record_ids );

	QFuture< void > setDuplicates( RecordID worse_duplicate, RecordID better_duplicate );

	//! Each pair is {worse_id, better_id}.
	QFuture< void > setDuplicates( const std::vector< std::pair< RecordID, RecordID > >& pairs );

	QFuture< void > createParentRelationship( TagDomainID tag_domian_id, TagID parent_id, TagID child_id );
	QFuture< void > createParentRelationship(
		TagDomainID tag_domian_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	//! The future fails with std::runtime_error carrying the server's error message.
	QFuture< void > createAliasRelationship( TagDomainID tag_domain_id, TagID aliased_id, TagID alias_id );

	QFuture< void > createAliasRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	QFuture< void > createSiblingRelationship( TagDomainID tag_domain_id, TagID older_id, TagID younger_id );

	QFuture< void > createSiblingRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	QFuture< void > removeParentRelationship( TagDomainID tag_domain_id, TagID parent_id, TagID child_id );
	QFuture< void > removeParentRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	QFuture< void > removeSiblingRelationship( TagDomainID tag_domain_id, TagID older_id, TagID younger_id );
	QFuture< void > removeSiblingRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	QFuture< void > removeAliasRelationship( TagDomainID tag_domain_id, TagID aliased_id, TagID alias_id );
	QFuture< void > removeAliasRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	//! The future fails with std::runtime_error on any server error, including an existing domain.
	QFuture< TagDomainID > createTagDomain( const std::string& name );

	//! Resolves to nullopt if no domain has this name.
	QFuture< std::optional< TagDomainID > > getTagDomain( std::string_view name );

	struct TagRelationshipInfo
	{
		std::vector< TagID > m_aliased, m_aliases, m_parents, m_children, m_older_siblings, m_younger_siblings;
	};

	QFuture< TagRelationshipInfo > getTagRelationships( TagID tag_id, TagDomainID tag_domain_id );

	struct ActiveTagVerboseInfo
	{
		TagID tag_id;
		TagDomainID tag_domain_id;
		bool is_explicit;
		std::vector< TagID > aliased_from;
		std::vector< TagID > inherited_from;
	};

	QFuture< std::vector< ActiveTagVerboseInfo > > getActiveRecordTagsVerbose( RecordID record_id );

	struct TagInfo
	{
		TagID m_id;

		struct NamespaceInfo
		{
			NamespaceID m_id;
			std::string m_text;
		} m_namespace;

		struct SubtagInfo
		{
			std::string m_text;
		} m_subtag;

		std::string toStdString() const
		{
			FGL_ASSERT( m_namespace.m_id != 0, "Namespace ID invalid" );

			if ( m_namespace.m_text.empty() ) return m_subtag.m_text;
			return format_ns::format( "{}:{}", m_namespace.m_text, m_subtag.m_text );
		}

		QString toQString() const { return QString::fromStdString( toStdString() ); }

		struct DomainCounts
		{
			TagDomainID tag_domain_id;
			std::int64_t storage_count;
			std::int64_t display_count;
		};

		std::int64_t storage_count {};
		std::int64_t display_count {};
		std::vector< DomainCounts > domain_counts;
	};

	QFuture< TagInfo > getTagInfo( TagID tag_id, const std::vector< TagDomainID >& domains = {} );

	struct TagDomainInfo
	{
		TagDomainID m_id;
		std::string m_name;
	};

	QFuture< std::vector< TagDomainInfo > > getTagDomains();

	QFuture< void > createFileCluster(
		const std::filesystem::path& server_path,
		const std::string& cluster_name,
		std::size_t byte_limit,
		std::uint16_t ratio,
		bool readonly );

	QFuture< void > addUrls( RecordID record_id, const std::vector< std::string >& urls );

	inline QFuture< void > addUrl( const RecordID record_id, std::string url )
	{
		std::vector< std::string > urls { { url } };
		return addUrls( record_id, urls );
	}

  private:

	void sendClientGet( UrlVariant url, IDHANResponseHandler&& responseHandler, IDHANErrorHandler&& errorHandler );

	void sendClientPost(
		QJsonDocument&& object,
		const UrlVariant& url,
		IDHANResponseHandler&& responseHandler,
		IDHANErrorHandler&& errorHandler );

	void sendClientJson(
		HttpMethod method,
		UrlVariant url,
		IDHANResponseHandler&& responseHandler,
		IDHANErrorHandler&& errorHandler,
		QJsonDocument&& object );
};

template < typename TPromise >
auto defaultErrorHandler( TPromise&& promise )
{
	auto handler =
		[ promise ]( QNetworkReply* reply, [[maybe_unused]] QNetworkReply::NetworkError error, std::string server_msg )
	{
		const std::runtime_error exception { format_ns::format( "Server Message: {}", server_msg ) };

		logging::error( server_msg );

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	return handler;
}

using TagRelationshipInfo = IDHANClient::TagRelationshipInfo;
using TagInfo = IDHANClient::TagInfo;
using TagDomainInfo = IDHANClient::TagDomainInfo;
using ActiveTagVerboseInfo = IDHANClient::ActiveTagVerboseInfo;

} // namespace idhan
