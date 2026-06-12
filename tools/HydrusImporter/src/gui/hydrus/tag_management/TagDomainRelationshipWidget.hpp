#pragma once

#include <QFutureWatcher>
#include <QListWidgetItem>
#include <QWidget>

#include <idhan/IDHANClient.hpp>

namespace Ui
{
class TagDomainRelationshipWidget;
}

class TagDomainRelationshipWidget : public QWidget
{
	Q_OBJECT

  public:

	explicit TagDomainRelationshipWidget( idhan::TagDomainID domainID, QWidget* parent = nullptr );
	~TagDomainRelationshipWidget() override;

	void setTag( idhan::TagID tagID );

  private slots:
	void on_addParentButton_clicked();
	void on_removeParentButton_clicked();
	void on_addChildButton_clicked();
	void on_removeChildButton_clicked();
	void on_addOlderSiblingButton_clicked();
	void on_removeOlderSiblingButton_clicked();
	void on_addYoungerSiblingButton_clicked();
	void on_removeYoungerSiblingButton_clicked();
	void on_addAliasedByButton_clicked();
	void on_removeAliasedByButton_clicked();
	void on_setAliasTargetButton_clicked();
	void on_aliasTargetEdit_textChanged( const QString& text );
	void on_aliasTargetResults_itemClicked( QListWidgetItem* item );

  private:

	void updateRelationships();
	void updateAliasTargetDisplay();
	void clearLists();
	void enterAliasSearchMode();
	void exitAliasSearchMode();

	Ui::TagDomainRelationshipWidget* ui;
	idhan::TagID m_tagID { 0 };
	idhan::TagDomainID m_domainID;
	bool m_aliasSearchMode { false };
	QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > >* m_aliasTargetWatcher { nullptr };
};
