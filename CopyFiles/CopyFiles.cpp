#include "CopyFiles.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>

CopyFiles::CopyFiles(QString &_SettingFileName ,QWidget *parent)
    : QMainWindow(parent), SettingFileName(_SettingFileName)
{
    ui.setupUi(this);

    if(Load(SettingFileName)){
	    setWindowTitle(tr("Copy Files - %1").arg(QFileInfo(SettingFileName).fileName()));
    }
    connect(this,SIGNAL(SignalCopyProgress(const QString &)),this,SLOT(SlotCopyProgress(const QString &)));
}

CopyFiles::~CopyFiles()
{}


void CopyFiles::on_pushButtonDestination_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this
                                                , tr("Select Destination Directory")
                                                , DestinationPath
                                                , QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        DestinationPath = dir;
        ui.lineEditDestination->setText(DestinationPath);
	}
}

void CopyFiles::on_pushButtonSource_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this
                                                , tr("Select Source Directory")
                                                , SourthPath
                                                , QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        SourthPath = dir;
        ui.lineEditSource->setText(SourthPath);
	}
}


void CopyFiles::on_pushButtonAddFileName_clicked()
{
	QString fileName = QInputDialog::getText(this,tr("Add File Name"),tr("File Name:"));
    if(!fileName.isEmpty()) {
        FileNameList.append(fileName);
        ui.listWidgetFileName->addItem(fileName);
	}
}


void CopyFiles::on_pushButtonSubFileName_clicked()
{
    int row = ui.listWidgetFileName->currentRow();
    if (row >= 0 && row < FileNameList.count()) {
        FileNameList.removeAt(row);
        delete ui.listWidgetFileName->takeItem(row);
	}
}


void CopyFiles::on_listWidgetFileName_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui.listWidgetFileName->currentRow();
    QString fileName = FileNameList[row];
    QString newFileName = QInputDialog::getText(this, tr("Edit File Name"), tr("File Name:"), QLineEdit::Normal, fileName);
    if (!newFileName.isEmpty()) {
        FileNameList[row] = newFileName;
        item->setText(newFileName);
	}
}
	
bool    CopyFiles::SaveSettings(const QString &SettingFileName) const
{
    if(Save(SettingFileName)) {
        return true;
    } else {
        QMessageBox::critical(NULL, tr("Error"), tr("Failed to save settings to %1").arg(SettingFileName));
		return false;
    }
}

bool    CopyFiles::Save(const QString &SettingFileName) const
{
	QFile file(SettingFileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "[Source]\n" << SourthPath << "\n";
        out << "[Destination]\n" << DestinationPath << "\n";
        out << "[FileNames]\n";
        for (const QString &fileName : FileNameList) {
            out << fileName << "\n";
        }
        return true;
    } else {
        
        return false;
	}
}

bool	CopyFiles::LoadSettings(const QString &SettingFileName)
{
    if(Load(SettingFileName)) {
        return true;
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to load settings from %1").arg(SettingFileName));
        return false;
    }
}

bool	CopyFiles::Load(const QString &SettingFileName)
{
    QFile file(SettingFileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString line;
        QString currentSection;
        while (in.readLineInto(&line)) {
            if (line.startsWith('[') && line.endsWith(']')) {
                currentSection = line.mid(1, line.length() - 2);
            } else if (!line.isEmpty()) {
                if (currentSection == "Source") {
                    SourthPath = line;
                    ui.lineEditSource->setText(SourthPath);
                } else if (currentSection == "Destination") {
                    DestinationPath = line;
                    ui.lineEditDestination->setText(DestinationPath);
                } else if (currentSection == "FileNames") {
                    FileNameList.append(line);
                    ui.listWidgetFileName->addItem(line);
                }
            }
        }
        return true;
    } else {
        return false;
	}
}


void CopyFiles::on_pushButtonStartCopy_clicked()
{
    DestinationPath =ui.lineEditDestination ->text();
    SourthPath      =ui.lineEditSource      ->text();

    if (SourthPath.isEmpty() || DestinationPath.isEmpty() || FileNameList.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please specify source, destination and file names."));
        return;
    }

    QDir sourceDir(SourthPath);
    QDir destDir(DestinationPath);
    
	copiedFilesInfo.clear();
    
    QDirIterator it(SourthPath, FileNameList, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString srcFilePath = it.next();
        QFileInfo srcFileInfo(srcFilePath);

        QString relativePath = sourceDir.relativeFilePath(srcFileInfo.absolutePath());
        if (relativePath == ".") {
            relativePath = "";
        }

        QString destPath = destDir.absoluteFilePath(relativePath);
        QString destFilePath = QDir(destPath).absoluteFilePath(srcFileInfo.fileName());
        QFileInfo destFileInfo(destFilePath);

        bool shouldCopy = false;

        if (!destFileInfo.exists()) {
            shouldCopy = true;
        } else {
            if (srcFileInfo.lastModified() > destFileInfo.lastModified()) {
                shouldCopy = true;
            }
        }

        if (shouldCopy) {
            QDir targetDir(destPath);
            if (!targetDir.exists()) {
				emit SignalCopyProgress(tr("Creating directory %1...").arg(destPath));
                targetDir.mkpath(".");
            }

            if (destFileInfo.exists()) {
				emit SignalCopyProgress(tr("Overwriting %1...").arg(destFileInfo.fileName()));
                QFile::remove(destFilePath);
            }
            emit SignalCopyProgress(tr("Copying %1...").arg(srcFileInfo.fileName()));
            if (QFile::copy(srcFilePath, destFilePath)) {
                QStringList fileInfo;
                fileInfo << relativePath << relativePath << srcFileInfo.fileName();
                copiedFilesInfo.append(fileInfo);
            } else {
                QMessageBox::critical(this, tr("Error"), tr("Failed to copy %1").arg(srcFileInfo.fileName()));
            }
        }
    }
    ShowResult();
}

void CopyFiles::ShowResult()
{
    ui.tableWidgetResult->setRowCount(0);
    for (int i = 0; i < copiedFilesInfo.size(); ++i) {
        ui.tableWidgetResult->insertRow(i);
        ui.tableWidgetResult->setItem(i, 0, new QTableWidgetItem(copiedFilesInfo[i][0])); // Source path
        ui.tableWidgetResult->setItem(i, 1, new QTableWidgetItem(copiedFilesInfo[i][1])); // Destination path
        ui.tableWidgetResult->setItem(i, 2, new QTableWidgetItem(copiedFilesInfo[i][2])); // File name
    }

    QMessageBox::information(this, tr("Success"), tr("Copy operation completed. %1 file(s) copied.").arg(copiedFilesInfo.size()));

}

void CopyFiles::SlotCopyProgress(const QString &message)
{
    ui.labelProgressiveMessage->setText(message);
}

void CopyFiles::on_pushButtonSaveNew_clicked()
{
	QString fileName = QFileDialog::getSaveFileName(this,tr("Save Settings"),QString(),tr("Settings Files (*.dat);;All Files (*.*)"));
    if (!fileName.isEmpty()) {
        if (SaveSettings(fileName)) {
            SettingFileName = fileName;
            setWindowTitle(tr("Copy Files - %1").arg(QFileInfo(SettingFileName).fileName()));
        }
	}
}


void CopyFiles::on_pushButtonOverwrite_clicked()
{
    if (SettingFileName.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("No settings file to overwrite. Please save as new."));
        return;
    }
    if (SaveSettings(SettingFileName)) {
        QMessageBox::information(this, tr("Success"), tr("Settings saved successfully."));
	}

}


void CopyFiles::on_pushButtonLoad_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,tr("Load Settings"),QString(),tr("Settings Files (*.dat);;All Files (*.*)"));
    if (!fileName.isEmpty()) {
        if (LoadSettings(fileName)) {
            SettingFileName = fileName;
            setWindowTitle(tr("Copy Files - %1").arg(QFileInfo(SettingFileName).fileName()));
        }
	}

}

