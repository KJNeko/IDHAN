//
// Created by kj16609 on 6/17/22.
//
#pragma once
#ifndef MAIN_IMPORTVIEWER_HPP
#define MAIN_IMPORTVIEWER_HPP

#include <QFuture>
#include <QWidget>
#include <queue>

#include "database/FileData.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
class ImportViewer;
}
QT_END_NAMESPACE

class ImportViewer : public QWidget
{
	Q_OBJECT

  public:
	explicit ImportViewer( QWidget* parent = nullptr );

	~ImportViewer() override;

	void processFiles();

	void addFiles( const std::vector<std::filesystem::path>& files );

  private:
	Ui::ImportViewer* ui;

	// File import list
	std::vector<std::filesystem::path> files;
	QFuture<void>* processingThread { nullptr };

	// Record keeping
	uint64_t filesAdded { 0 };
	uint64_t filesProcessed { 0 };
	uint64_t successful { 0 };
	uint64_t failed { 0 };
	uint64_t alreadyinDB { 0 };
	uint64_t deleted { 0 };

  private slots:
	void updateValues_slot();
	void addFileToView_slot( uint64_t );

  signals:
	void updateValues();
	void addFileToView( uint64_t );
};


#endif // MAIN_IMPORTVIEWER_HPP
