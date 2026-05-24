#include "ProjectConverter.h"
#include "./ui_ProjectConverter.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDomDocument>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSet>
#include <QMap>
#include "NList.h"
#include <QDebug>
#include <QStringList>
#include <QStringConverter>
#include "EditMacroDialog.h"
#include "XGeneralFunc.h"
#include <QInputDialog>

bool    MatchingDim::Save(QIODevice *f)
{
    ::Save(f,MStr);
	::Save(f,RStr);
    ::Save(f,FileName);
    return true;
}
bool    MatchingDim::Load(QIODevice *f)
{
    ::Load(f,MStr);
	::Load(f,RStr);
    ::Load(f,FileName);
    return true;
}

ProjectConverter::ProjectConverter(const QString &SettingFileName ,QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ProjectConverter)
{
    ui->setupUi(this);

    LoadSettings(SettingFileName);
    setWindowTitle(tr("Project Converter - %1").arg(QFileInfo(FileNameToSettings).fileName()));

    ::SetColumnWidthInTable(ui->tableWidgetMacro ,0, 12);
    ::SetColumnWidthInTable(ui->tableWidgetMacro ,1, 20);
    ::SetColumnWidthInTable(ui->tableWidgetMacro ,3, 60);
}

ProjectConverter::~ProjectConverter()
{
    delete ui;
}

void ProjectConverter::on_pushButtonFileName_clicked()
{
    QStringList FileList=QFileDialog::getOpenFileNames(nullptr
                                                    , /**/"VCXProj/SLN files"
                                                    , QString()
                                                    , /**/"SLN(*.sln);;VCXProj(*.vcxproj);;All files(*.*)");
    SLNFiles.clear();
    ProjectFileName.clear();
    if(FileList.count()!=0){
        ui->lineEditFileName->setText(FileList.join(';'));
        for(int i=0;i<FileList.count();i++){
            QString FileName=FileList[i];
            QFileInfo   FInfo(FileName);
            if(FInfo.suffix().toUpper()==/**/"SLN"){
                QStringList  ProjList;
                if(LoadSLN(FileName,ProjList)==true){
                    for(int j=0;j<ProjList.count();j++){
                        QString ProjFileName=ProjList[j];
                        ProjectFileName.append(ProjFileName);
                    }
                }
                SLNFiles.append(FileName);
            }
            else
            if(FInfo.suffix().toUpper()==/**/"VCXPROJ"){
                ProjectFileName.append(FileName);
            }
        }
    }
}
bool    ProjectConverter::MakeCMakeFile(const QString &ProjectSlnFileName)
{
    QFileInfo   FInfo(ProjectSlnFileName);
    if(FInfo.suffix().toUpper()==/**/"SLN"){
        QStringList  ProjList;
        if(LoadSLN(ProjectSlnFileName,ProjList)==true){
            for(int j=0;j<ProjList.count();j++){
                QString ProjFileName=ProjList[j];
                ProjectFileName.append(ProjFileName);
            }
        }
        SLNFiles.append(ProjectSlnFileName);
    }
    else
    if(FInfo.suffix().toUpper()==/**/"VCXPROJ"){
        ProjectFileName.append(ProjectSlnFileName);
    }
    on_pushButtonConvert_clicked();
	return true;
}


bool ProjectConverter::LoadSLN(const QString &SLNFileName, QStringList &ProjList)
{
    QFile file(SLNFileName);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QFileInfo SLNInfo(SLNFileName);
    QString SLNPath = SLNInfo.absolutePath();

    QTextStream in(&file);
    in.setAutoDetectUnicode(true);  // UTF-8 BOM対応など
    
    // 正規表現でProject行から .vcxproj を抽出
    QRegularExpression re("Project\\(\"[^\"]*\"\\)\\s*=\\s*\"[^\"]*\"\\s*,\\s*\"([^\"]*\\.vcxproj)\"");

    QStringList tempProjList;
    while (!in.atEnd()) {
        QString line = in.readLine();
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            QString relPath = match.captured(1);
            relPath.replace("\\", "/"); // スラッシュに統一してパス解決のエラーを防ぐ
            tempProjList << SLNPath + "/" + relPath;
        }
    }
    tempProjList.removeDuplicates();
    tempProjList.sort(); // アルファベット順で一旦ソート

    // 静的ライブラリとそれ以外に分類するためのリスト
    QStringList staticLibs;
    QStringList otherProjs;

    // <ConfigurationType>StaticLibrary</ConfigurationType> を確実に捕捉する正規表現
    // （大文字小文字を区別せず、タグ内の不要な空白も許容する）
    QRegularExpression reStatic("<ConfigurationType>\\s*StaticLibrary\\s*</ConfigurationType>", QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < tempProjList.count(); ++i) {
        QString projFileName = tempProjList[i];
        bool isStatic = false;

        QFile vcxprojFile(projFileName);
        // ファイルをテキストとして開き、正規表現で走査する（XMLのパースエラーや名前空間の影響を排除）
        if (vcxprojFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream vcxprojIn(&vcxprojFile);
            vcxprojIn.setAutoDetectUnicode(true);
            QString content = vcxprojIn.readAll();
            
            if (content.contains(reStatic)) {
                isStatic = true;
            }
            vcxprojFile.close();
        } else {
            qDebug() << "Failed to open project file for static check:" << projFileName;
        }

        // 分類してリストに追加
        if (isStatic) {
            staticLibs << projFileName;
        } else {
            otherProjs << projFileName;
        }
    }

    // 静的ライブラリを先に、その後にそれ以外のプロジェクトを結合する
    ProjList.clear();
    ProjList << staticLibs << otherProjs;

    return true;
}

void ProjectConverter::on_pushButtonConvert_clicked()
{
	m_enableParallelBuild=ui->checkBoxEnableParallelBuild->isChecked();
	m_maxParallelBuilds = ui->spinBoxMaxParallelBuilds->value();
	ModeDynamicLink = ui->checkBoxModeDynamicLink->isChecked();
    ForceOpenMP     = ui->checkBoxModeForceOpenMP->isChecked();

    m_unresolvedMacros.clear();
    for(int i=0;i<ProjectFileName.count();i++){
        QStringList RetUnresolvedMacros;
        CreateCMakeFile(ProjectFileName[i],RetUnresolvedMacros);

        for(const QString &macro : RetUnresolvedMacros) {
			MatchingDim *m = new MatchingDim();
            m->MStr = macro;
			m->RStr = QString(); // 置換後の文字列は空にしておく
            m->FileName =ProjectFileName[i];
			macroReplacements.Merge(m);
		}
    }
    for(int i=0;i<SLNFiles.count();i++){
        CreateRootCMakeFile(SLNFiles[i]);
    }

	ShowMacro();
}

void	ProjectConverter::ShowMacro(void)
{
    ui->tableWidgetMacro->setRowCount(macroReplacements.GetCount());
    for(int i=0;i<macroReplacements.GetCount();i++){
        MatchingDim *m = macroReplacements[i];
        ::SetDataToTable(ui->tableWidgetMacro, 0, i, m->MStr);
        ::SetDataToTable(ui->tableWidgetMacro, 1, i, m->RStr);
        ::SetDataToTable(ui->tableWidgetMacro, 2, i, m->FileName);
    }
}
void    ProjectConverter::ShowInclude(void)
{
    ui->listWidgetInclude->clear();
    for(int i=0;i<additionalIncludeDirs.count();i++){
        ui->listWidgetInclude->addItem(additionalIncludeDirs[i]);
    }
}
void    ProjectConverter::ShowLibrary(void)
{
    ui->listWidgetLibrary->clear();
    for(int i=0;i<additionalLibraryDirs.count();i++){
        ui->listWidgetLibrary->addItem(additionalLibraryDirs[i]);
    }
}
void    ProjectConverter::ShowOptimaze(void)
{
    ui->listWidgetOptimaze->clear();
    for(int i=0;i<additionalOptimizationFlags.count();i++){
        ui->listWidgetOptimaze->addItem(additionalOptimizationFlags[i]);
    }
}


void ProjectConverter::on_tableWidgetMacro_cellDoubleClicked(int, int)
{
	int row = ui->tableWidgetMacro->currentRow();
	MatchingDim *m = macroReplacements[row];
    EditMacroDialog D(m);
	if(D.exec() == QDialog::Accepted) {
        // ダイアログで編集された内容を反映
        m->MStr = D.MStr;
        m->RStr = D.RStr;
        // テーブルの表示も更新
        ::SetDataToTable(ui->tableWidgetMacro, 0, row, m->MStr);
        ::SetDataToTable(ui->tableWidgetMacro, 1, row, m->RStr);
    }
}


void ProjectConverter::on_pushButtonAddMacro_clicked()
{
    EditMacroDialog D(NULL);
	if(D.exec() == QDialog::Accepted) {
		MatchingDim *m = new MatchingDim();
        m->MStr = D.MStr;
        m->RStr = D.RStr;
		int row = macroReplacements.GetCount();
		macroReplacements.AppendList(m);
		ui->tableWidgetMacro->setRowCount(macroReplacements.GetCount());
        ::SetDataToTable(ui->tableWidgetMacro, 0, row, m->MStr);
        ::SetDataToTable(ui->tableWidgetMacro, 1, row, m->RStr);
    }
}


void ProjectConverter::on_pushButtonDelMacro_clicked()
{
    int row = ui->tableWidgetMacro->currentRow();
    MatchingDim *m = macroReplacements[row];
	macroReplacements.RemoveList(m);
	delete m;

    ShowMacro();
}


void ProjectConverter::on_listWidgetInclude_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui->listWidgetInclude->currentRow();
    QString dir = additionalIncludeDirs[row];
    QString newDir = QInputDialog::getText(this, tr("Edit Include Directory"), tr("Include Directory:"), QLineEdit::Normal, dir);
    if (!newDir.isEmpty()) {
        // \ を / に変換してからリストに反映
        additionalIncludeDirs[row] = newDir.replace("\\", "/");
        ShowInclude();
    }
}

void ProjectConverter::on_pushButtonAddInclude_clicked()
{
    QString newDir = QInputDialog::getText(this, tr("Add Include Directory"), tr("Include Directory:"));
    if (!newDir.isEmpty()) {
        // \ を / に変換してからリストに追加
        additionalIncludeDirs.append(newDir.replace("\\", "/"));
        ShowInclude();
    }
}

void ProjectConverter::on_pushButtonDelInclude_clicked()
{
    int row = ui->listWidgetInclude->currentRow();
    if (row >= 0 && row < additionalIncludeDirs.count()) {
        additionalIncludeDirs.removeAt(row);
        ShowInclude();
	}
}


void ProjectConverter::on_listWidgetLibrary_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui->listWidgetLibrary->currentRow();
    QString dir = additionalLibraryDirs[row];
    QString newDir = QInputDialog::getText(this, tr("Edit Library Directory"), tr("Library Directory:"), QLineEdit::Normal, dir);
    if (!newDir.isEmpty()) {
        // \ を / に変換してからリストに反映
        additionalLibraryDirs[row] = newDir.replace("\\", "/");
        ShowLibrary();
    }
}

void ProjectConverter::on_pushButtonAddLibrary_clicked()
{
    QString newDir = QInputDialog::getText(this, tr("Add Library Directory"), tr("Library Directory:"));
    if (!newDir.isEmpty()) {
        // \ を / に変換してからリストに追加
        additionalLibraryDirs.append(newDir.replace("\\", "/"));
        ShowLibrary();
    }
}


void ProjectConverter::on_pushButtonDelLibrary_clicked()
{
    int row = ui->listWidgetLibrary->currentRow();
    if (row >= 0 && row < additionalLibraryDirs.count()) {
        additionalLibraryDirs.removeAt(row);
        ShowLibrary();
	}
}


void ProjectConverter::on_listWidgetOptimaze_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui->listWidgetOptimaze->currentRow();
    QString flag = additionalOptimizationFlags[row];
    QString newFlag = QInputDialog::getText(this, tr("Edit Optimization Flag"), tr("Optimization Flag:"), QLineEdit::Normal, flag);
    if (!newFlag.isEmpty()) {
        additionalOptimizationFlags[row] = newFlag;
        ShowOptimaze();
	}
}


void ProjectConverter::on_pushButtonAddOptimaze_clicked()
{
    QString newFlag = QInputDialog::getText(this, tr("Add Optimization Flag"), tr("Optimization Flag:"));
    if (!newFlag.isEmpty()) {
        additionalOptimizationFlags.append(newFlag);
        ShowOptimaze();
	}
}


void ProjectConverter::on_pushButtonDelOptimaze_clicked()
{
    int row = ui->listWidgetOptimaze->currentRow();
    if (row >= 0 && row < additionalOptimizationFlags.count()) {
        additionalOptimizationFlags.removeAt(row);
        ShowOptimaze();
	}
}


void ProjectConverter::on_pushButtonSaveNew_clicked()
{
	QString FileName = QFileDialog::getSaveFileName(this
                                                    ,tr("Save Project")
                                                    ,QString()
                                                    ,tr("Setting (*.prjc);;All Files (*.*)"));
    SaveSettings(FileName);
}

bool    ProjectConverter::SaveSettings(const QString &FileName)
{
	if(!FileName.isEmpty()) {
	    m_enableParallelBuild=ui->checkBoxEnableParallelBuild->isChecked();
	    m_maxParallelBuilds = ui->spinBoxMaxParallelBuilds->value();
		ModeDynamicLink = ui->checkBoxModeDynamicLink->isChecked();
        ForceOpenMP     = ui->checkBoxModeForceOpenMP->isChecked();

        // プロジェクト設定を保存する処理をここに実装
		// 例: QFileを使ってFileNameに設定内容を書き込む
		QFile file(FileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            // ここでmacroReplacementsやadditionalIncludeDirsなどの内容をoutに書き込む
            // 例:
            out << "[Macros]\n";
            for(int i=0; i<macroReplacements.GetCount(); i++) {
                MatchingDim *m = macroReplacements[i];
                out << m->MStr << "=" << m->RStr << "\n";
            }
            out << "[IncludeDirs]\n";
            for(const QString &dir : additionalIncludeDirs) {
                out << dir << "\n";
            }
            out << "[LibraryDirs]\n";
            for(const QString &dir : additionalLibraryDirs) {
                out << dir << "\n";
            }
            out << "[OptimizationFlags]\n";
            for(const QString &flag : additionalOptimizationFlags) {
                out << flag << "\n";
            }

            // 除外ライブラリ設定の保存
            out << "[ExcludedLibraries]\n";
            for(const QString &lib : excludedLibraryFiles) {
                out << lib << "\n";
            }
            out << "[AdditionalLibraryFiles]\n";
            for(const QString &lib : additionalLibraryFiles) {
                out << lib << "\n";
            }
            out << "[LinkOptions]\n";
            for(const QString &option : additionalLinkOptimizations) {
                out << option << "\n";
            }
            // 並列ビルド設定の保存
            out << "[BuildSettings]\n";
            out << "EnableParallelBuild=" << (m_enableParallelBuild ? "1" : "0") << "\n";
            out << "MaxParallelBuilds=" << m_maxParallelBuilds << "\n";

			out << "ModeDynamicLink=" << (ModeDynamicLink ? "1" : "0") << "\n";
            out << "ForceOpenMP=" << (ForceOpenMP ? "1" : "0") << "\n";

            file.close();
		}
		FileNameToSettings = FileName;
		setWindowTitle(tr("Project Converter - %1").arg(QFileInfo(FileName).fileName()));
        return true;
    }
    return false;
}


void ProjectConverter::on_pushButtonOverwrite_clicked()
{
    if(FileNameToSettings.isEmpty()) {
        on_pushButtonSaveNew_clicked();
        return;
    }
    SaveSettings(FileNameToSettings);
}


void ProjectConverter::on_pushButtonLoad_clicked()
{
    QString FileName = QFileDialog::getOpenFileName(this
                                                    ,tr("Load Project")
                                                    ,QString()
                                                    ,tr("Setting (*.prjc);;All Files (*.*)"));
    LoadSettings(FileName);
}

bool    ProjectConverter::LoadSettings(const QString &FileName)
{
    if(!FileName.isEmpty()) {
        // プロジェクト設定を読み込む処理をここに実装
        QFile file(FileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            // ここでinからmacroReplacementsやadditionalIncludeDirsなどの内容を読み込む
            // 例:
            QString currentSection;
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith('[') && line.endsWith(']')) {
                    currentSection = line.mid(1, line.length() - 2);
                } else if (!line.isEmpty()) {
                    if (currentSection == "Macros") {
                        QStringList parts = line.split('=');
                        if (parts.size() == 2) {
                            MatchingDim *m = new MatchingDim();
                            m->MStr = parts[0].trimmed();
                            m->RStr = parts[1].trimmed();
                            macroReplacements.AppendList(m);
                        }
                    } else if (currentSection == "IncludeDirs") {
                        // \ を / に変換して読み込み
                        additionalIncludeDirs.append(line.replace("\\", "/"));
                    } else if (currentSection == "LibraryDirs") {
                        // \ を / に変換して読み込み
                        additionalLibraryDirs.append(line.replace("\\", "/"));
                    } else if (currentSection == "OptimizationFlags") {
                        additionalOptimizationFlags.append(line);
                    // 除外ライブラリ設定の読み込み
                    } else if (currentSection == "ExcludedLibraries") {
                        excludedLibraryFiles.append(line);
                    // 追加ライブラリファイル設定の読み込み
                    } else if (currentSection == "AdditionalLibraryFiles") {
                        additionalLibraryFiles.append(line);
                    // 並列ビルド設定の読み込み
                    } else if (currentSection == "LinkOptions") {
                        additionalLinkOptimizations.append(line);
					}
                    else if (currentSection == "BuildSettings") {
                        QStringList parts = line.split('=');
                        if (parts.size() == 2) {
                            if (parts[0].trimmed() == "EnableParallelBuild") {
                                m_enableParallelBuild = (parts[1].trimmed() == "1");
                            } else if (parts[0].trimmed() == "MaxParallelBuilds") {
                                m_maxParallelBuilds = parts[1].trimmed().toInt();
                            } else if (parts[0].trimmed() == "ModeDynamicLink") {
								ModeDynamicLink = (parts[1].trimmed() == "1");
                            } else if (parts[0].trimmed() == "ForceOpenMP") {
                                ForceOpenMP = (parts[1].trimmed() == "1");  
                            }
                        }
                    }
                }
            }
            file.close();

            FileNameToSettings = FileName;
            setWindowTitle(tr("Project Converter - %1").arg(QFileInfo(FileNameToSettings).fileName()));
            
            ui->checkBoxEnableParallelBuild->setChecked(m_enableParallelBuild);
    	    ui->spinBoxMaxParallelBuilds->setValue(m_maxParallelBuilds);
			ui->checkBoxModeDynamicLink->setChecked(ModeDynamicLink);
            ui->checkBoxModeForceOpenMP->setChecked(ForceOpenMP);

            ShowMacro();
            ShowInclude();
            ShowLibrary();
            ShowOptimaze();
            ShowLinkOptions();
            ShowExcludedLibraryFiles();
            ShowAdditionalLibraryFiles();
            return true;
        }
	}
    return false;
}


void ProjectConverter::on_listWidgetExcludedLibraryFiles_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui->listWidgetExcludedLibraryFiles->currentRow();
	QString lib = excludedLibraryFiles[row];
    QString newLib = QInputDialog::getText(this, tr("Edit Excluded Library"), tr("Excluded Library File:"), QLineEdit::Normal, lib);
    if (!newLib.isEmpty()) {
        excludedLibraryFiles[row] = newLib;
        ShowExcludedLibraryFiles();
	}
}


void ProjectConverter::on_pushButtonAddExcludedLibraryFile_clicked()
{
    QString newLib = QInputDialog::getText(this, tr("Add Excluded Library"), tr("Excluded Library File:"));
    if (!newLib.isEmpty()) {
        excludedLibraryFiles.append(newLib);
        ShowExcludedLibraryFiles();
	}
}


void ProjectConverter::on_pushButtonDelExcludedLibraryFile_clicked()
{
    int row = ui->listWidgetExcludedLibraryFiles->currentRow();
    if (row >= 0 && row < excludedLibraryFiles.count()) {
        excludedLibraryFiles.removeAt(row);
        ShowExcludedLibraryFiles();
	}
}
void    ProjectConverter::ShowExcludedLibraryFiles(void)
{
    ui->listWidgetExcludedLibraryFiles->clear();
    for(int i=0;i<excludedLibraryFiles.count();i++){
        ui->listWidgetExcludedLibraryFiles->addItem(excludedLibraryFiles[i]);
    }
}

void ProjectConverter::on_listWidgetAdditionalLibraryFiles_itemDoubleClicked(QListWidgetItem *item)
{
    int row = ui->listWidgetAdditionalLibraryFiles->currentRow();
    QString lib = additionalLibraryFiles[row];
    QString newLib = QInputDialog::getText(this, tr("Edit Additional Library File"), tr("Additional Library File:"), QLineEdit::Normal, lib);
    if (!newLib.isEmpty()) {
        additionalLibraryFiles[row] = newLib;
        ShowAdditionalLibraryFiles();
    }
}

void ProjectConverter::on_pushButtonAddAdditionalLibraryFile_clicked()
{
    QString newLib = QInputDialog::getText(this, tr("Add Additional Library File"), tr("Additional Library File:"));
    if (!newLib.isEmpty()) {
        additionalLibraryFiles.append(newLib);
        ShowAdditionalLibraryFiles();
    }
}

void ProjectConverter::on_pushButtonDelAdditionalLibraryFile_clicked()
{
    int row = ui->listWidgetAdditionalLibraryFiles->currentRow();
    if (row >= 0 && row < additionalLibraryFiles.count()) {
        additionalLibraryFiles.removeAt(row);
        ShowAdditionalLibraryFiles();
    }
}

void ProjectConverter::ShowAdditionalLibraryFiles(void)
{
    ui->listWidgetAdditionalLibraryFiles->clear();
    for(int i=0;i<additionalLibraryFiles.count();i++){
        ui->listWidgetAdditionalLibraryFiles->addItem(additionalLibraryFiles[i]);
    }
}
void    ProjectConverter::ShowLinkOptions (void)
{
    ui->listWidgetLinkOptions->clear();
    for(int i=0;i<additionalLinkOptimizations.count();i++){
        ui->listWidgetLinkOptions->addItem(additionalLinkOptimizations[i]);
    }
}

void ProjectConverter::on_listWidgetLinkOptions_itemDoubleClicked(QListWidgetItem *item)
{
	int row = ui->listWidgetLinkOptions->currentRow();
    QString option = additionalLinkOptimizations[row];
    QString newOption = QInputDialog::getText(this, tr("Edit Link Option"), tr("Link Option:"), QLineEdit::Normal, option);
    if (!newOption.isEmpty()) {
        additionalLinkOptimizations[row] = newOption;
        ShowLinkOptions ();
    }
}


void ProjectConverter::on_pushButtonAddLinkOptions_clicked()
{
    QString newOption = QInputDialog::getText(this, tr("Add Link Option"), tr("Link Option:"));
    if (!newOption.isEmpty()) {
        additionalLinkOptimizations.append(newOption);
        ShowLinkOptions ();
	}
}


void ProjectConverter::on_pushButtonDelLinkOptions_clicked()
{
    int row = ui->listWidgetLinkOptions->currentRow();
    if (row >= 0 && row < additionalLinkOptimizations.count()) {
        additionalLinkOptimizations.removeAt(row);
        ShowLinkOptions ();
	}
}

