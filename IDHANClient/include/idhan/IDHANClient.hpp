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
#include "Network.hpp"
#include "logging/logger.hpp"

namespace spdlog
{
class logger;
}

namespace idhan
{
class SHA256;
class TagCache;
class TagTextCache;

//! Server build and API version information, as returned by IDHANClient::queryVersion().
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

//! Registers IDHAN's standard command-line options (server host, port, API key, TLS, ...) on \p parser.
void addIDHANOptions( QCommandLineParser& parser );

//! Callback invoked with the completed network reply on success.
using IDHANResponseHandler = std::function< void( QNetworkReply* ) >;
//! Callback invoked when a request fails, with the reply, the Qt error, and any server message.
using IDHANErrorHandler = std::function< void( QNetworkReply*, QNetworkReply::NetworkError, std::string server_msg ) >;

//! Typed C++/Qt wrapper over the IDHAN REST API. Each method issues one HTTP request and returns a
//! QFuture that resolves to the parsed result (or reports an error). Construct one with the server
//! host/port/key (Qt must be initialised first); a process-wide instance is reachable via instance().
class IDHANClient
{
	std::shared_ptr< spdlog::logger > m_logger { nullptr };
	inline static IDHANClient* m_instance { nullptr };
	QUrl m_url_template {};

	Network network;

	using UrlVariant = std::variant< QString, QUrl >;

	QString m_key {};

	//! Caches resolved (namespace, subtag) -> TagID so repeated tags skip the server round-trip.
	std::unique_ptr< TagCache > m_tag_cache;
	std::unique_ptr< TagTextCache > m_tag_text_cache;

  public:

	[[nodiscard]] std::shared_ptr< spdlog::logger > getLogger() const { return m_logger; }

	void setUrlInfo( QUrl& url );

	static IDHANClient& instance();

	QUrl getBaseUrl() const { return m_url_template; }

	void addKeyHeader( QNetworkRequest& request );

	Q_DISABLE_COPY_MOVE( IDHANClient )

	IDHANClient() = delete;

	/**
	* @brief Queries the target server's version info on construction.
	* @note Qt must be initialised first, via a QGuiApplication or QApplication instance.
	* @param client_name Name this client appears under in the server logs.
	*/
	IDHANClient(
		const QString& client_name,
		const QString& hostname,
		qint16 port,
		const QString& key,
		bool use_tls = false );

	~IDHANClient();

	//! Returns a future that resolves to true if the server responds with valid version info.
	[[nodiscard]] QFuture< bool > validConnection();

	//! Sets the API key sent with subsequent requests.
	void setAPIKey( const QString& key );

	//! Points the client at a (possibly different) server and key, reconnecting.
	void openConnection( const QString& hostname, qint16 port, QString key, bool use_tls = false );

	QFuture< std::vector< RecordID > > createRecords( const std::vector< std::array< std::byte, 32 > >& hashes );

	/**
	 * @param hashes Hex representations of hashes
	 */
	QFuture< std::vector< RecordID > > createRecords( const std::vector< std::string >& hashes );

	//! \return The record ID for the given hex SHA-256, or std::nullopt if no such record exists.
	QFuture< std::optional< RecordID > > getRecordID( const std::string& sha256 );

	//! \return The ID of a random record with active mappings.
	QFuture< RecordID > getRandomActiveRecord();

	//! \return The server's build and API version information.
	QFuture< VersionInfo > queryVersion();

	//! Sets the byte budget of the client-side tag resolution cache, evicting immediately if the
	//! cache is now over the new budget. Defaults to TAG_CACHE_DEFAULT_BUDGET_BYTES (1 GiB).
	void setTagCacheBudget( std::size_t bytes );

	//! Creates the given tags (creating any that don't exist) and returns their IDs, order-preserved.
	QFuture< std::vector< TagID > > createTags( const std::vector< std::string >& tags );
	//! \copydoc createTags(const std::vector<std::string>&)
	QFuture< std::vector< TagID > > createTags( const std::vector< std::pair< std::string, std::string > >& tags );

	//! Creates a single "namespace:subtag" tag and returns its ID.
	QFuture< TagID > createTag( const std::string& namespace_text, const std::string& subtag_text );

	//! \copydoc createTag(const std::string&,const std::string&)
	QFuture< TagID > createTag( const std::string& tag_text );

	//! \return The raw (stored) tag IDs applied to \p record_id in \p tag_domain_id.
	QFuture< std::vector< TagID > > getRecordTags( RecordID record_id, TagDomainID tag_domain_id );
	//! \return The active (alias/sibling/parent-resolved) tag IDs for \p record_id in \p tag_domain_id.
	QFuture< std::vector< TagID > > getActiveRecordTags( RecordID record_id, TagDomainID tag_domain_id );

	//! Resolves tag IDs to their "namespace:subtag" text, order-preserved.
	QFuture< std::vector< std::string > > getTagText( const std::vector< TagID >& tag_ids );

	//! \copydoc getTagText(const std::vector<TagID>&)
	QFuture< std::string > getTagText( TagID tag_id );

	//! \return Tag suggestions (id + text) matching the autocomplete \p text.
	QFuture< std::vector< std::pair< TagID, std::string > > > autocompleteTag( const QString& text );

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

	// File relationships
	QFuture< void > setAlternativeGroups( std::vector< RecordID >& record_ids );

	QFuture< void > setDuplicates( RecordID worse_duplicate, RecordID better_duplicate );

	/**
	 * @param pairs Pairs of ids in a (worse_id, better_id) format
	 */
	QFuture< void > setDuplicates( const std::vector< std::pair< RecordID, RecordID > >& pairs );

	//! Creates a parent/child relationship between two tags
	QFuture< void > createParentRelationship( TagDomainID tag_domian_id, TagID parent_id, TagID child_id );
	QFuture< void > createParentRelationship(
		TagDomainID tag_domian_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	/**
	 * @brief Creates a new alias for a given tag.
	 * @throws AliasLoopException Throws an exception if a loop is detected
	 * @throws InvalidTagID
	 */
	QFuture< void > createAliasRelationship( TagDomainID tag_domain_id, TagID aliased_id, TagID alias_id );

	QFuture< void > createAliasRelationship(
		TagDomainID tag_domain_id,
		const std::vector< std::pair< TagID, TagID > >& pairs );

	//! Creates a new sibling relationship between two tags.
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

	/**
	 * @brief Creates a new tag domain, Throws if the domain exists
	 * @throws DomainExists
	 */
	QFuture< TagDomainID > createTagDomain( const std::string& name );

	/**
	 * @brief Searches for an existing tag domain. Throws if it does not exist
	 * @throws DomainDoesNotExist
	 */
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

		std::size_t item_count { 0 };
	};

	QFuture< TagInfo > getTagInfo( TagID tag_id );

	struct TagDomainInfo
	{
		TagDomainID m_id;
		std::string m_name;
	};

	//! Returns a list of all tag domain ids
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
		// logging::logResponse( reply );

		const std::runtime_error exception { format_ns::format( "{}", server_msg ) };

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
