#pragma once

#include <QFutureWatcher>
#include <QListWidgetItem>
#include <QWidget>

#include <idhan/IDHANClient.hpp>

class TagDomainRelationshipWidget;

namespace Ui
{
class TagManagementWidget;
}

class TagManagementWidget : public QWidget
{
	Q_OBJECT

  public:

	explicit TagManagementWidget( QWidget* parent = nullptr );
	~TagManagementWidget() override;

  private slots:
	void on_tagSearchEdit_textChanged( const QString& text );
	void on_tagSearchEdit_returnPressed();
	void on_autocompleteList_itemClicked( QListWidgetItem* item );
	void on_autocompleteFinished();

  private:

	void loadDomains();

	Ui::TagManagementWidget* ui;
	idhan::TagID m_selectedTagID { 0 };
	std::vector< idhan::TagDomainInfo > m_domains;
	QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >* m_autocompleteWatcher { nullptr };
	std::vector< TagDomainRelationshipWidget* > m_domainWidgets;
};
