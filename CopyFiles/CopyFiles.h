#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_CopyFiles.h"
#include <QListWidgetItem>
#include <QStringList>

class CopyFiles : public QMainWindow
{
    Q_OBJECT

	QString SourthPath;
    QString DestinationPath;
	QStringList FileNameList;

    QList<QStringList> copiedFilesInfo;
    QString	SettingFileName;

public:
    CopyFiles(QString &SettingFileName ,QWidget *parent = nullptr);
    ~CopyFiles();

	bool    SaveSettings(const QString &SettingFileName) const;
	bool	LoadSettings(const QString &SettingFileName);

private slots:
    void on_pushButtonDestination_clicked();
    void on_pushButtonSource_clicked();
    void on_pushButtonAddFileName_clicked();
    void on_pushButtonSubFileName_clicked();
    void on_listWidgetFileName_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonStartCopy_clicked();
    void on_pushButtonSaveNew_clicked();

    void on_pushButtonOverwrite_clicked();

    void on_pushButtonLoad_clicked();

private:
    Ui::CopyFilesClass ui;

    void    ShowResult();
};

