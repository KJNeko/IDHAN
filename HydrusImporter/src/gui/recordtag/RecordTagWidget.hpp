#pragma once

#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QWidget>

#include <idhan/IDHANClient.hpp>

class RecordTagWidget final : public QWidget
{
	Q_OBJECT

  public:

	explicit RecordTagWidget( QWidget* parent = nullptr );
	~RecordTagWidget() override;

  signals:
	void detachRequested();

  private slots:
	void onLoadRecord();
	void onAddTag();
	void onRemoveTag();
	void onDetach();
	void onAddTagTextChanged( const QString& text );
	void onAutocompleteFinished();
	void onAutocompleteItemClicked( QListWidgetItem* item );
	void onAddTagReturnPressed();

  private:

	static std::pair< std::string, std::string > splitTagText( const std::string& tag_text );

	void loadDomains();
	void loadTags();
	void fetchThumbnail();
	void populateLists();

	QLineEdit* m_recordIdInput;
	QPushButton* m_loadButton;
	QComboBox* m_domainCombo;
	QPushButton* m_detachButton;
	QLabel* m_previewLabel;
	QListWidget* m_rawTagsList;
	QListWidget* m_activeTagsList;
	QLineEdit* m_addTagInput;
	QListWidget* m_autocompletePopup;
	QPushButton* m_addTagButton;
	QPushButton* m_removeTagButton;

	idhan::RecordID m_currentRecordId { 0 };
	idhan::TagDomainID m_currentDomainId { 0 };
	std::vector< idhan::TagDomainInfo > m_domains;
	std::vector< idhan::TagID > m_rawTagIds;
	std::vector< idhan::TagID > m_activeTagIds;

	QNetworkAccessManager* m_networkManager;

	QFutureWatcher< std::vector< idhan::TagDomainInfo > >* m_domainWatcher { nullptr };
	QFutureWatcher< std::vector< idhan::TagID > >* m_rawTagsWatcher { nullptr };
	QFutureWatcher< std::vector< idhan::TagID > >* m_activeTagsWatcher { nullptr };
	QFutureWatcher< std::vector< std::string > >* m_tagTextWatcher { nullptr };
	QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >* m_autocompleteWatcher { nullptr };

	idhan::TagID m_selectedAutocompleteTagId { 0 };
	std::vector< std::pair< idhan::TagID, std::string > > m_lastAutocompleteResults;
};
