//
// Created by kj16609 on 6/28/25.
//
#pragma once

#include <QWidget>

#include "HydrusImporter.hpp"
#include "HydrusImporterWidget.hpp"

namespace Ui
{
class TagServiceWidget;
}

class TagServiceWidget : public QWidget
{
	std::size_t mappings_processed { 0 };
	std::size_t parents_processed { 0 };
	std::size_t aliases_processed { 0 };
	idhan::hydrus::ServiceInfo m_info;
	QString m_name;
	bool m_preprocessed { false };
	TagServiceWorker* m_worker;

  public:

	explicit TagServiceWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent = nullptr );
	~TagServiceWidget() override;

	void setName( const QString& name );

	bool ready() const { return m_preprocessed; }

	void setInfo( const idhan::hydrus::ServiceInfo& service_info );

  public slots:
	void startImport();
	void startPreImport();

	void processedMappings( std::size_t count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );

	void preprocessingFinished();
	void setMaxMappings( std::size_t count );
	void setMaxParents( std::size_t count );
	void setMaxAliases( std::size_t count );

  private:

	Ui::TagServiceWidget* ui;
};
