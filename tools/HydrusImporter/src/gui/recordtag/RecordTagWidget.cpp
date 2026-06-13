#include "RecordTagWidget.hpp"
#include "RecordTagColors.hpp"

#include <QHBoxLayout>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>


void ActiveTagDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	// Draw base item first (background, selection highlight, text)
	QStyledItemDelegate::paint( painter, option, index );

	const auto type = static_cast< TagRelationshipType >( index.data( RelationshipTypeRole ).toInt() );

	if ( type != TagRelationshipType::None )
	{
		painter->save();
		painter->setOpacity( 0.35 );

		if ( type == TagRelationshipType::Both )
		{
			const auto mid = option.rect.top() + option.rect.height() / 2;
			const QRect top_half( option.rect.left(), option.rect.top(), option.rect.width(), mid - option.rect.top() );
			const QRect bottom_half(
				option.rect.left(), mid, option.rect.width(), option.rect.bottom() - mid );

			painter->fillRect( top_half, idhan::tag_colors::ALIASED );
			painter->fillRect( bottom_half, idhan::tag_colors::INHERITED );
		}
		else if ( type == TagRelationshipType::Aliased )
		{
			painter->fillRect( option.rect, idhan::tag_colors::ALIASED );
		}
		else if ( type == TagRelationshipType::Inherited )
		{
			painter->fillRect( option.rect, idhan::tag_colors::INHERITED );
		}

		painter->restore();
	}
}

RecordTagWidget::RecordTagWidget( QWidget* parent ) :
  QWidget( parent ),
  m_networkManager( new QNetworkAccessManager( this ) )
{
	auto* main_layout = new QVBoxLayout( this );

	// Top row: Record ID input, Load button, Domain selector, Detach
	auto* top_layout = new QHBoxLayout();
	auto* record_id_label = new QLabel( "Record ID:", this );
	m_recordIdInput = new QLineEdit( this );
	m_recordIdInput->setPlaceholderText( "Enter record ID" );
	m_loadButton = new QPushButton( "Load", this );
	m_randomButton = new QPushButton( "Random", this );
	m_domainCombo = new QComboBox( this );
	m_detachButton = new QPushButton( "Detach", this );

	top_layout->addWidget( record_id_label );
	top_layout->addWidget( m_recordIdInput );
	top_layout->addWidget( m_loadButton );
	top_layout->addWidget( m_randomButton );
	top_layout->addWidget( m_domainCombo );
	top_layout->addWidget( m_detachButton );
	main_layout->addLayout( top_layout );

	// Middle: Preview + Raw Tags + Active Tags
	auto* middle_layout = new QHBoxLayout();

	m_previewLabel = new QLabel( this );
	m_previewLabel->setFixedSize( 256, 256 );
	m_previewLabel->setAlignment( Qt::AlignCenter );
	m_previewLabel->setStyleSheet( "border: 1px solid gray;" );
	m_previewLabel->setText( "No preview" );
	middle_layout->addWidget( m_previewLabel );

	auto* raw_group = new QWidget( this );
	auto* raw_layout = new QVBoxLayout( raw_group );
	auto* raw_label = new QLabel( "Raw Tags (tag_mappings)", raw_group );
	m_rawTagsList = new QListWidget( raw_group );
	raw_layout->addWidget( raw_label );
	raw_layout->addWidget( m_rawTagsList );
	middle_layout->addWidget( raw_group );

	auto* active_group = new QWidget( this );
	auto* active_layout = new QVBoxLayout( active_group );
	auto* active_label = new QLabel( "Active Tags (active_tag_mappings_final)", active_group );
	m_activeTagsList = new QListWidget( active_group );
	m_activeTagDelegate = new ActiveTagDelegate( this );
	m_activeTagsList->setItemDelegate( m_activeTagDelegate );
	active_layout->addWidget( active_label );
	active_layout->addWidget( m_activeTagsList );
	middle_layout->addWidget( active_group );

	main_layout->addLayout( middle_layout );

	// Bottom: Add/Remove controls
	auto* bottom_layout = new QHBoxLayout();
	m_addTagInput = new QLineEdit( this );
	m_addTagInput->setPlaceholderText( "Type to search tags..." );
	m_addTagButton = new QPushButton( "Add Tag", this );
	m_removeTagButton = new QPushButton( "Remove Selected", this );

	bottom_layout->addWidget( m_addTagInput );
	bottom_layout->addWidget( m_addTagButton );
	bottom_layout->addWidget( m_removeTagButton );
	main_layout->addLayout( bottom_layout );

	// Autocomplete popup
	m_autocompletePopup = new QListWidget( this );
	m_autocompletePopup->setWindowFlags( Qt::Popup | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus );
	m_autocompletePopup->setAttribute( Qt::WA_ShowWithoutActivating );
	m_autocompletePopup->setFocusPolicy( Qt::NoFocus );
	m_autocompletePopup->setMouseTracking( true );
	m_autocompletePopup->hide();

	// Connections
	connect( m_loadButton, &QPushButton::clicked, this, &RecordTagWidget::onLoadRecord );
	connect( m_randomButton, &QPushButton::clicked, this, &RecordTagWidget::onRandomRecord );
	connect( m_recordIdInput, &QLineEdit::returnPressed, this, &RecordTagWidget::onLoadRecord );
	connect( m_detachButton, &QPushButton::clicked, this, &RecordTagWidget::onDetach );
	connect( m_addTagButton, &QPushButton::clicked, this, &RecordTagWidget::onAddTag );
	connect( m_addTagInput, &QLineEdit::returnPressed, this, &RecordTagWidget::onAddTagReturnPressed );
	connect( m_removeTagButton, &QPushButton::clicked, this, &RecordTagWidget::onRemoveTag );
	connect( m_addTagInput, &QLineEdit::textChanged, this, &RecordTagWidget::onAddTagTextChanged );
	connect( m_autocompletePopup, &QListWidget::itemClicked, this, &RecordTagWidget::onAutocompleteItemClicked );

	connect(
		m_domainCombo,
		&QComboBox::currentIndexChanged,
		this,
		[ this ]()
		{
			if ( m_currentRecordId != 0 ) loadTags();
		} );

	loadDomains();
}

RecordTagWidget::~RecordTagWidget() = default;

void RecordTagWidget::loadDomains()
{
	auto& client { idhan::IDHANClient::instance() };
	auto future { client.getTagDomains() };

	m_domainWatcher = new QFutureWatcher< std::vector< idhan::TagDomainInfo > >( this );
	m_domainWatcher->setFuture( future );

	connect(
		m_domainWatcher,
		&QFutureWatcherBase::finished,
		this,
		[ this ]()
		{
			try
			{
				m_domains = m_domainWatcher->result();
				m_domainCombo->clear();
				for ( const auto& domain : m_domains )
				{
					m_domainCombo->addItem(
						QString::fromStdString( format_ns::format( "{} ({})", domain.m_name, domain.m_id ) ),
						static_cast< qlonglong >( domain.m_id ) );
				}
			}
			catch ( ... )
			{}
			m_domainWatcher->deleteLater();
			m_domainWatcher = nullptr;
		} );
}

void RecordTagWidget::onLoadRecord()
{
	if ( m_recordIdInput->text().trimmed().isEmpty() ) return;

	bool ok { false };
	const auto record_id = static_cast< idhan::RecordID >( m_recordIdInput->text().trimmed().toULongLong( &ok ) );
	if ( !ok || record_id == 0 ) return;

	m_currentRecordId = record_id;
	m_previewLabel->setText( "Loading..." );
	m_rawTagsList->clear();
	m_activeTagsList->clear();

	loadTags();
	fetchThumbnail();
}

void RecordTagWidget::onRandomRecord()
{
	if ( m_randomWatcher != nullptr ) return;

	m_previewLabel->setText( "Loading random..." );
	m_rawTagsList->clear();
	m_activeTagsList->clear();

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.getRandomActiveRecord() };

	m_randomWatcher = new QFutureWatcher< idhan::RecordID >( this );
	m_randomWatcher->setFuture( future );

	connect(
		m_randomWatcher,
		&QFutureWatcherBase::finished,
		this,
		[ this ]()
		{
			try
			{
				const auto record_id = m_randomWatcher->result();
				m_recordIdInput->setText( QString::number( record_id ) );
				onLoadRecord();
			}
			catch ( ... )
			{
				m_previewLabel->setText( "No active records" );
			}
			m_randomWatcher->deleteLater();
			m_randomWatcher = nullptr;
		} );
}

void RecordTagWidget::loadTags()
{
	const auto domain_id = static_cast< idhan::TagDomainID >( m_domainCombo->currentData().toULongLong() );
	if ( domain_id == 0 ) return;
	m_currentDomainId = domain_id;

	m_verboseMap.clear();

	auto& client { idhan::IDHANClient::instance() };

	{
		auto future { client.getRecordTags( m_currentRecordId, domain_id ) };
		m_rawTagsWatcher = new QFutureWatcher< std::vector< idhan::TagID > >( this );
		m_rawTagsWatcher->setFuture( future );

		connect(
			m_rawTagsWatcher,
			&QFutureWatcherBase::finished,
			this,
			[ this ]()
			{
				try
				{
					m_rawTagIds = m_rawTagsWatcher->result();
				}
				catch ( ... )
				{
					m_rawTagIds.clear();
				}
				m_rawTagsWatcher->deleteLater();
				m_rawTagsWatcher = nullptr;
				populateLists();
			} );
	}

	{
		auto future { client.getActiveRecordTags( m_currentRecordId, domain_id ) };
		m_activeTagsWatcher = new QFutureWatcher< std::vector< idhan::TagID > >( this );
		m_activeTagsWatcher->setFuture( future );

		connect(
			m_activeTagsWatcher,
			&QFutureWatcherBase::finished,
			this,
			[ this ]()
			{
				try
				{
					m_activeTagIds = m_activeTagsWatcher->result();
				}
				catch ( ... )
				{
					m_activeTagIds.clear();
				}
				m_activeTagsWatcher->deleteLater();
				m_activeTagsWatcher = nullptr;
				populateLists();
			} );
	}

	{
		auto future { client.getActiveRecordTagsVerbose( m_currentRecordId ) };
		m_verboseWatcher = new QFutureWatcher< std::vector< idhan::ActiveTagVerboseInfo > >( this );
		m_verboseWatcher->setFuture( future );

		connect(
			m_verboseWatcher,
			&QFutureWatcherBase::finished,
			this,
			[ this ]()
			{
				try
				{
					for ( const auto& info : m_verboseWatcher->result() )
					{
						if ( info.tag_domain_id != m_currentDomainId ) continue;
						m_verboseMap[ info.tag_id ] = info;
					}
				}
				catch ( ... )
				{
					m_verboseMap.clear();
				}
				m_verboseWatcher->deleteLater();
				m_verboseWatcher = nullptr;
				populateLists();
			} );
	}
}

void RecordTagWidget::populateLists()
{
	if ( m_rawTagsWatcher != nullptr || m_activeTagsWatcher != nullptr || m_verboseWatcher != nullptr ) return;

	std::vector< idhan::TagID > all_ids;
	all_ids.reserve( m_rawTagIds.size() + m_activeTagIds.size() );
	all_ids.insert( all_ids.end(), m_rawTagIds.begin(), m_rawTagIds.end() );
	all_ids.insert( all_ids.end(), m_activeTagIds.begin(), m_activeTagIds.end() );

	// Collect extra IDs from verbose info for tag text resolution
	for ( const auto& [ tag_id, info ] : m_verboseMap )
	{
		for ( const auto id : info.aliased_from ) all_ids.push_back( id );
		for ( const auto id : info.inherited_from ) all_ids.push_back( id );
	}

	if ( all_ids.empty() )
	{
		m_rawTagsList->clear();
		m_activeTagsList->clear();
		return;
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.getTagText( all_ids ) };

	m_tagTextWatcher = new QFutureWatcher< std::vector< std::string > >( this );
	m_tagTextWatcher->setFuture( future );

	connect(
		m_tagTextWatcher,
		&QFutureWatcherBase::finished,
		this,
		[ this, all_ids = std::move( all_ids ) ]()
		{
			try
			{
				const auto tag_texts { m_tagTextWatcher->result() };
				std::unordered_map< idhan::TagID, std::string > id_to_text;
				for ( std::size_t i = 0; i < all_ids.size() && i < tag_texts.size(); ++i )
				{
					id_to_text[ all_ids[ i ] ] = tag_texts[ i ];
				}

				m_rawTagsList->clear();
				for ( const auto tag_id : m_rawTagIds )
				{
					const auto it = id_to_text.find( tag_id );
					const auto text =
						( it != id_to_text.end() ) ? it->second : format_ns::format( "Unknown ({})", tag_id );
					auto* item = new QListWidgetItem( QString::fromStdString( text ), m_rawTagsList );
					item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );
				}

				m_activeTagsList->clear();
				for ( const auto tag_id : m_activeTagIds )
				{
					const auto it = id_to_text.find( tag_id );
					const auto text =
						( it != id_to_text.end() ) ? it->second : format_ns::format( "Unknown ({})", tag_id );
					auto* item = new QListWidgetItem( QString::fromStdString( text ), m_activeTagsList );
					item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );

					// Build tooltip and relationship type from verbose info
					QStringList tooltip_lines;
					auto rel_type = ActiveTagDelegate::TagRelationshipType::None;

					if ( const auto vit = m_verboseMap.find( tag_id ); vit != m_verboseMap.end() )
					{
						const auto& info = vit->second;

						if ( !info.aliased_from.empty() )
						{
							rel_type = ActiveTagDelegate::TagRelationshipType::Aliased;
							for ( const auto orig_id : info.aliased_from )
							{
								const auto ot = id_to_text.find( orig_id );
								const auto orig_text = ( ot != id_to_text.end() ) ?
							                               ot->second :
							                               format_ns::format( "Unknown ({})", orig_id );
								tooltip_lines.push_back(
									QString::fromStdString( format_ns::format( "Aliased from: {}", orig_text ) ) );
							}
						}

						if ( !info.inherited_from.empty() )
						{
							rel_type = rel_type == ActiveTagDelegate::TagRelationshipType::Aliased ?
						                   ActiveTagDelegate::TagRelationshipType::Both :
						                   ActiveTagDelegate::TagRelationshipType::Inherited;
							for ( const auto origin_id : info.inherited_from )
							{
								const auto ot = id_to_text.find( origin_id );
								const auto origin_text = ( ot != id_to_text.end() ) ?
							                                 ot->second :
							                                 format_ns::format( "Unknown ({})", origin_id );
								tooltip_lines.push_back(
									QString::fromStdString( format_ns::format( "Inherited from: {}", origin_text ) ) );
							}
						}
					}

					if ( tooltip_lines.isEmpty() ) tooltip_lines.push_back( "No Relationships" );

					item->setToolTip( tooltip_lines.join( '\n' ) );
					item->setData( ActiveTagDelegate::RelationshipTypeRole, static_cast< int >( rel_type ) );
				}
			}
			catch ( ... )
			{
				m_rawTagsList->clear();
				m_activeTagsList->clear();
			}
			m_tagTextWatcher->deleteLater();
			m_tagTextWatcher = nullptr;
		} );
}

void RecordTagWidget::fetchThumbnail()
{
	if ( m_currentRecordId == 0 ) return;

	auto& client { idhan::IDHANClient::instance() };
	auto base_url { client.getBaseUrl() };

	QUrl url { base_url };
	url.setPath( QString::fromStdString( format_ns::format( "/records/{}/thumbnail", m_currentRecordId ) ) );

	QNetworkRequest request { url };
	client.addKeyHeader( request );

	auto* reply { m_networkManager->get( request ) };

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[ this, reply ]()
		{
			if ( reply->error() == QNetworkReply::NoError )
			{
				QPixmap pixmap;
				if ( pixmap.loadFromData( reply->readAll() ) )
				{
					m_previewLabel->setPixmap(
						pixmap.scaled( 256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
				}
				else
				{
					m_previewLabel->setText( "Invalid image" );
				}
			}
			else
			{
				m_previewLabel->setText( "Failed to load" );
			}
			reply->deleteLater();
		} );
}

std::pair< std::string, std::string > RecordTagWidget::splitTagText( const std::string& tag_text )
{
	const auto colon_pos = tag_text.find( ':' );
	if ( colon_pos == std::string::npos || colon_pos == 0 )
	{
		return { "", tag_text };
	}
	return { tag_text.substr( 0, colon_pos ), tag_text.substr( colon_pos + 1 ) };
}

void RecordTagWidget::onAddTagTextChanged( const QString& text )
{
	if ( text.trimmed().isEmpty() )
	{
		m_autocompletePopup->hide();
		m_lastAutocompleteResults.clear();
		return;
	}

	if ( m_autocompleteWatcher != nullptr )
	{
		m_autocompleteWatcher->disconnect();
		m_autocompleteWatcher->cancel();
		m_autocompleteWatcher->deleteLater();
		m_autocompleteWatcher = nullptr;
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.autocompleteTag( text.trimmed() ) };

	m_autocompleteWatcher = new QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >( this );
	m_autocompleteWatcher->setFuture( future );

	connect( m_autocompleteWatcher, &QFutureWatcherBase::finished, this, &RecordTagWidget::onAutocompleteFinished );
}

void RecordTagWidget::onAutocompleteFinished()
{
	try
	{
		m_lastAutocompleteResults = m_autocompleteWatcher->result();
		m_autocompletePopup->clear();

		if ( m_lastAutocompleteResults.empty() )
		{
			m_autocompletePopup->hide();
			return;
		}

		for ( const auto& [ tag_id, tag_text ] : m_lastAutocompleteResults )
		{
			auto* item = new QListWidgetItem( QString::fromStdString( tag_text ), m_autocompletePopup );
			item->setData( Qt::UserRole, static_cast< qlonglong >( tag_id ) );
		}

		auto pos = m_addTagInput->mapToGlobal( QPoint( 0, m_addTagInput->height() ) );
		m_autocompletePopup->setMinimumWidth( m_addTagInput->width() );
		m_autocompletePopup->move( pos );
		m_autocompletePopup->show();
		m_autocompletePopup->setCurrentRow( 0 );
		m_addTagInput->setFocus();
	}
	catch ( ... )
	{
		m_autocompletePopup->hide();
		m_lastAutocompleteResults.clear();
	}

	m_autocompleteWatcher->deleteLater();
	m_autocompleteWatcher = nullptr;
}

void RecordTagWidget::onAutocompleteItemClicked( QListWidgetItem* item )
{
	m_selectedAutocompleteTagId = static_cast< idhan::TagID >( item->data( Qt::UserRole ).toULongLong() );
	m_addTagInput->setText( item->text() );
	m_autocompletePopup->hide();
	onAddTag();
	m_addTagInput->setFocus();
}

void RecordTagWidget::onAddTagReturnPressed()
{
	if ( m_autocompletePopup->isVisible() && m_autocompletePopup->currentItem() )
	{
		onAutocompleteItemClicked( m_autocompletePopup->currentItem() );
		return;
	}
	onAddTag();
}

void RecordTagWidget::onAddTag()
{
	if ( m_currentRecordId == 0 ) return;

	std::string tag_text;

	if ( m_selectedAutocompleteTagId != 0 )
	{
		tag_text = m_addTagInput->text().toStdString();
		m_selectedAutocompleteTagId = 0;
	}
	else if ( !m_lastAutocompleteResults.empty() )
	{
		tag_text = m_lastAutocompleteResults[ 0 ].second;
	}
	else
	{
		tag_text = m_addTagInput->text().trimmed().toStdString();
		if ( tag_text.empty() ) return;
	}

	m_autocompletePopup->hide();
	m_addTagInput->clear();
	m_addTagInput->setFocus();

	const auto domain_id = m_currentDomainId;
	if ( domain_id == 0 ) return;

	auto pair = splitTagText( tag_text );

	auto& client { idhan::IDHANClient::instance() };

	std::vector< std::pair< std::string, std::string > > tags;
	tags.emplace_back( std::move( pair ) );

	auto future { client.addTags( m_currentRecordId, domain_id, std::move( tags ) ) };

	auto* watcher { new QFutureWatcher< void >( this ) };
	watcher->setFuture( future );

	connect(
		watcher,
		&QFutureWatcher< void >::finished,
		this,
		[ this, watcher ]()
		{
			loadTags();
			watcher->deleteLater();
		} );
}

void RecordTagWidget::onRemoveTag()
{
	if ( m_currentRecordId == 0 ) return;

	auto selected_items = m_rawTagsList->selectedItems();
	if ( selected_items.isEmpty() ) return;

	const auto domain_id = m_currentDomainId;
	if ( domain_id == 0 ) return;

	std::vector< idhan::TagID > tag_ids_to_remove;
	tag_ids_to_remove.reserve( selected_items.size() );

	for ( const auto* item : selected_items )
	{
		const auto tag_id = static_cast< idhan::TagID >( item->data( Qt::UserRole ).toULongLong() );
		tag_ids_to_remove.push_back( tag_id );
	}

	auto& client { idhan::IDHANClient::instance() };
	auto future { client.removeTags( m_currentRecordId, domain_id, tag_ids_to_remove ) };

	auto* watcher { new QFutureWatcher< void >( this ) };
	watcher->setFuture( future );

	connect(
		watcher,
		&QFutureWatcher< void >::finished,
		this,
		[ this, watcher ]()
		{
			loadTags();
			watcher->deleteLater();
		} );
}

void RecordTagWidget::onDetach()
{
	emit detachRequested();
}
