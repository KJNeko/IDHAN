//
// Created by kj16609 on 6/17/22.
//
#pragma once
#ifndef MAIN_IMPORTVIEWER_HPP
#define MAIN_IMPORTVIEWER_HPP


#include <QFuture>
#include <QWidget>
#include <QTimer>

#include <queue>
#include <filesystem>

#include "../modules/ListViewModule.hpp"
#include "../modules/TagViewModule.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class ImportView;
}
QT_END_NAMESPACE

class ImportView : public QWidget
{
Q_OBJECT

public:
	explicit ImportView( QWidget* parent = nullptr );

	~ImportView() override;

	ImportView( const ImportView& ) = delete;

	ImportView operator=( const ImportView& ) = delete;

	void processFiles();

	void addFiles( const std::vector< std::filesystem::path >& files );

private:
	Ui::ImportView* ui;

	// File import list
	std::vector< std::filesystem::path > files {};
	QFuture< void >* processingThread { nullptr };

	// Record keeping
	uint64_t filesAdded { 0 };
	uint64_t filesProcessed { 0 };
	uint64_t successful { 0 };
	uint64_t failed { 0 };
	uint64_t alreadyinDB { 0 };
	uint64_t deleted { 0 };

	ListViewModule* viewport { nullptr };
	TagViewModule* tagport { nullptr };

signals:

	void updateValues();

private slots:

	void updateValues_slot();
};


#endif // MAIN_IMPORTVIEWER_HPP
