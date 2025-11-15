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

ProjectConverter::ProjectConverter(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ProjectConverter)
{
    ui->setupUi(this);
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
    if(FileList.count()!=0){
        ProjectFileName.clear();

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
            }
            else
            if(FInfo.suffix().toUpper()==/**/"VCXPROJ"){
                ProjectFileName.append(FileName);
            }
        }
    }
}


void ProjectConverter::on_pushButtonConvert_clicked()
{
    for(int i=0;i<ProjectFileName.count();i++){
        CreateCMakeFile(ProjectFileName[i]);
    }
}
bool    ProjectConverter::LoadSLN(const QString SLNFileName,QStringList &ProjList)
{
    QFile file(SLNFileName);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    in.setAutoDetectUnicode(true);  // UTF-8 BOM対応など
    // 正規表現でProject行から .vcxproj を抽出
    QRegularExpression re("Project\\(\"[^\"]*\"\\)\\s*=\\s*\"[^\"]*\"\\s*,\\s*\"([^\"]*\\.vcxproj)\"");

    while (!in.atEnd()) {
        QString line = in.readLine();
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            ProjList << match.captured(1);
        }
    }
    ProjList.removeDuplicates();
    ProjList.sort();

    return true;
}

static QString normSlashes(QString p)
{
    return p.replace('\\', QDir::separator());
}

static QStringList splitList(const QString& s)
{
    // ; または , 区切りを許容、空要素や %(...) マクロは除外
    QString t = s;
    t.replace("\r", "").replace("\n", "");
    QStringList raw = t.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
    QStringList out;
    for (QString e : raw) {
        e = e.trimmed();
        if (e.isEmpty()) continue;
        if (e.contains("%(")) continue; // MSBuild の継続マクロは無視
        out << e;
    }
    return out;
}

static QString resolveMSBuildVars(QString path, const QString& projDir, const QString& slnDir)
{
    // 代表的なもののみ解決。未対応はそのまま残す（CMake 側でユーザが調整可能）
    path.replace("$(ProjectDir)", normSlashes(projDir + "/"));
    path.replace("$(SolutionDir)", normSlashes(slnDir + "/"));
    return path;
}

static QString titleCaseModule(const QString& m)
{
    QString s = m.toLower().trimmed();
    if (s.isEmpty()) return s;
    // 一部よくある例外の調整
    if (s == "openglwidgets") return "OpenGLWidgets";
    if (s == "printsupport")  return "PrintSupport";
    // 先頭大文字化
    s[0] = s[0].toUpper();
    return s;
}


class ProjectInfo
{
public:
    QString projectName;
    class ProjectPlatform : public NPList<ProjectPlatform>
    {
    public:
        QString targetType; // Application / StaticLibrary / DynamicLibrary
        QString configuration; // e.g., Debug, Release
        QString platform;      // e.g., x64, Win32
        bool    win32Subsystem = false;

        QStringList includeDirs;
        QStringList libDirs;
        QStringList additionalLibs;

        QStringList qtModules; // Core, Gui, Widgets, ...

        // Settings
        QString     configurationType; // Application, StaticLibrary, DynamicLibrary
        QString     optimization;      // Disabled/MinSpace/MaxSpeed/Full
        QString     runtimeLibrary;    // MultiThreaded, MultiThreadedDebug, MultiThreadedDLL, MultiThreadedDebugDLL
    };
    NPListPack<ProjectPlatform> ProjectContainer;
    QStringList cppFiles;
    QStringList hFiles;
    QStringList uiFiles;
    QStringList qrcFiles;
    QStringList resFiles; // png/jpg/ico/bmp など（None から）


    void    CollectFiles(QDomDocument& doc);
    QString     GenerateCMake(void)    const;
};
static QString detectProjectName(QDomDocument& doc, const QString& vcxprojPath)
{
    // <TargetName> があれば採用。無ければ vcxproj のベース名。
    QDomNodeList t = doc.elementsByTagName("TargetName");
    if (t.size() > 0) {
        QString n = t.item(0).toElement().text().trimmed();
        if (!n.isEmpty()) return n;
    }
    return QFileInfo(vcxprojPath).completeBaseName();
}

void ProjectInfo::CollectFiles(QDomDocument& doc)
{
    auto collectIncludeAttr = [&](const QString& tag, QStringList& out){
        QDomNodeList nodes = doc.elementsByTagName(tag);
        for (int i = 0; i < nodes.size(); ++i) {
            QDomElement e = nodes.item(i).toElement();
            if (!e.isNull()) {
                QString inc = e.attribute("Include").trimmed();
                if (!inc.isEmpty()) out << normSlashes(inc);
            }
        }
    };
    collectIncludeAttr("ClCompile", cppFiles);
    collectIncludeAttr("ClInclude", hFiles);
    collectIncludeAttr("QtMoc",     hFiles);
    collectIncludeAttr("QtUic",     uiFiles);
    collectIncludeAttr("QtRcc",     qrcFiles);

    // None から画像などを拾う（png/jpg/jpeg/ico/bmp/svg等）
    QDomNodeList noneNodes = doc.elementsByTagName("None");
    for (int i = 0; i < noneNodes.size(); ++i) {
        QDomElement e = noneNodes.item(i).toElement();
        if (e.isNull()) continue;
        QString inc = e.attribute("Include").trimmed();
        if (inc.isEmpty()) continue;
        QString p = normSlashes(inc);
        QString ext = QFileInfo(p).suffix().toLower();
        if (QStringList({"png","jpg","jpeg","ico","bmp","svg","gif","tiff","ico"}).contains(ext)) {
            resFiles << p;
        }
    }

    // 重複排除
    auto dedup = [](QStringList& lst){
        QSet<QString> s(lst.begin(), lst.end());
        lst = QStringList(s.begin(), s.end());
        std::sort(lst.begin(), lst.end());
    };
    dedup(cppFiles);
    dedup(hFiles);
    dedup(uiFiles);
    dedup(qrcFiles);
    dedup(resFiles);
}

void resolvePathsInList(QStringList& lst, const QString& projDir, const QString& slnDir)
{
    for (QString& p : lst) {
        p = normSlashes(resolveMSBuildVars(p, projDir, slnDir));
    }
}
static QString cmakeEscape(const QString& s)
{
    QString r = s;
    r.replace("\"", "\\\"");
    return r;
}

QString     ProjectInfo::GenerateCMake(void)    const
{
    QStringList lines;

    // 1) 基本ヘッダ
    lines << "cmake_minimum_required(VERSION 3.16)";
    lines << QString("project(%1 LANGUAGES CXX)").arg(cmakeEscape(projectName));
    lines << "set(CMAKE_CXX_STANDARD 17)";
    lines << "set(CMAKE_CXX_STANDARD_REQUIRED ON)";
    lines << "";

    // 2) Qt 自動機能
    lines << "set(CMAKE_AUTOMOC ON)";
    lines << "set(CMAKE_AUTOUIC ON)";
    lines << "set(CMAKE_AUTORCC ON)";
    lines << "";

    // 3) find_package(Qt6 ...)
    for(ProjectPlatform *f=ProjectContainer.GetFirst();f!=NULL;f=f->GetNext()){
        QStringList qtMods = QStringList(f->qtModules.begin(), f->qtModules.end());
        std::sort(qtMods.begin(), qtMods.end());
        if (qtMods.isEmpty()) {
            // モジュール未記載でも最低限 Core は想定
            qtMods << "Core";
        }
        lines << QString("find_package(Qt6 REQUIRED COMPONENTS %1)")
                    .arg(qtMods.join(' '));
        lines << "";
    }

    // 4) ファイル群を変数化（見やすさのため）
    auto joinQuoted = [](const QStringList& lst) {
        QStringList out;
        for (const QString& p : lst) out << QString("\"%1\"").arg(cmakeEscape(p));
        return out.join("\n    ");
    };

    if (!cppFiles.isEmpty()) {
        lines << "set(PROJECT_SOURCES";
        lines << "    " + joinQuoted(cppFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "# Note: No .cpp found in the .vcxproj; add sources if needed.";
        lines << "set(PROJECT_SOURCES)";
        lines << "";
    }

    if (!hFiles.isEmpty()) {
        lines << "set(PROJECT_HEADERS";
        lines << "    " + joinQuoted(hFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_HEADERS)";
        lines << "";
    }

    if (!uiFiles.isEmpty()) {
        lines << "set(PROJECT_FORMS";
        lines << "    " + joinQuoted(uiFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_FORMS)";
        lines << "";
    }

    if (!qrcFiles.isEmpty()) {
        lines << "set(PROJECT_RESOURCES";
        lines << "    " + joinQuoted(qrcFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_RESOURCES)";
        lines << "";
    }

    if (!resFiles.isEmpty()) {
        lines << "set(PROJECT_ASSETS";
        lines << "    " + joinQuoted(resFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_ASSETS)";
        lines << "";
    }

    // 5) ターゲット生成
    QString addTarget;
    QString targetName = projectName;
    QString sourcesExpr =
        " ${PROJECT_SOURCES} ${PROJECT_FORMS} ${PROJECT_RESOURCES} ${PROJECT_HEADERS} ${PROJECT_ASSETS}";



    // 6) include & lib ディレクトリ
    for(ProjectPlatform *f=ProjectContainer.GetFirst();f!=NULL;f=f->GetNext()){
        if (f->targetType.compare("Application", Qt::CaseInsensitive) == 0) {
            if (f->win32Subsystem)
                addTarget = QString("add_executable(%1 WIN32%2)").arg(cmakeEscape(targetName), sourcesExpr);
            else
                addTarget = QString("add_executable(%1%2)").arg(cmakeEscape(targetName), sourcesExpr);
        } else if (f->targetType.compare("StaticLibrary", Qt::CaseInsensitive) == 0) {
            addTarget = QString("add_library(%1 STATIC%2)").arg(cmakeEscape(targetName), sourcesExpr);
        } else if (f->targetType.compare("DynamicLibrary", Qt::CaseInsensitive) == 0) {
            addTarget = QString("add_library(%1 SHARED%2)").arg(cmakeEscape(targetName), sourcesExpr);
        } else {
            // 未知の場合は実行ファイルにフォールバック
            addTarget = QString("# Unknown ConfigurationType='%1', fallback to executable")
                            .arg(cmakeEscape(f->targetType));
            lines << addTarget;
            addTarget = QString("add_executable(%1%2)").arg(cmakeEscape(targetName), sourcesExpr);
        }
        lines << addTarget;
        lines << "";
        if (!f->includeDirs.isEmpty()) {
            QStringList incs = QStringList(f->includeDirs.begin(), f->includeDirs.end());
            std::sort(incs.begin(), incs.end());
            lines << QString("target_include_directories(%1 PRIVATE").arg(cmakeEscape(targetName));
            for (const auto& p : incs) {
                lines << QString("    \"%1\"").arg(cmakeEscape(p));
            }
            lines << ")";
            lines << "";
        }

        if (!f->libDirs.isEmpty()) {
            QStringList libs = QStringList(f->libDirs.begin(), f->libDirs.end());
            std::sort(libs.begin(), libs.end());
            lines << QString("target_link_directories(%1 PRIVATE").arg(cmakeEscape(targetName));
            for (const auto& p : libs) {
                lines << QString("    \"%1\"").arg(cmakeEscape(p));
            }
            lines << ")";
            lines << "";
        }

        // 7) リンク（Qt6 モジュール + 追加依存）
        QStringList linkTargets;
        QStringList qtMods = QStringList(f->qtModules.begin(), f->qtModules.end());
        for (const QString& m : qtMods) linkTargets << ("Qt6::" + m);

        if (!f->additionalLibs.isEmpty()) {
            QStringList deps = QStringList(f->additionalLibs.begin(), f->additionalLibs.end());
            std::sort(deps.begin(), deps.end());
            for (const QString& d : deps) linkTargets << d;
        }

        if (!linkTargets.isEmpty()) {
            lines << QString("target_link_libraries(%1 PRIVATE").arg(cmakeEscape(targetName));
            for (const auto& t : linkTargets) {
                lines << QString("    %1").arg(t);
            }
            lines << ")";
            lines << "";
        }
    }

    // 8) 仕上げコメント
    lines << "# Debug/Release などの構成は CMake のマルチコンフィグ・ジェネレータで自動切替されます。";
    lines << "# もし構成ごとに特別な差分設定が必要なら、generator expression($<CONFIG:Debug>)等を追加してください。";
    lines << "";

    return lines.join('\n') + "\n";
}

bool    ProjectConverter::CreateCMakeFile(const QString &ProjFileName)
{
    QDomDocument doc;
    QFile file(ProjFileName);
    if (!file.open(QIODevice::ReadOnly) || !doc.setContent(&file)) {
        return false;
    }
    file.close();

    QFileInfo   FInfo(ProjFileName);
    QString     OutFileName=FInfo.path()
                            +QDir::separator()
                            +QString(/**/"CMakeLists.txt");
    QFile   OutFile(OutFileName);
    if(OutFile.open(QIODevice::WriteOnly)==false)
        return false;
    QTextStream out(&OutFile);

    const QString vcxprojPath = FInfo.absoluteFilePath();
    ProjectInfo pi;
    pi.projectName = detectProjectName(doc, vcxprojPath);


    pi.CollectFiles(doc);
    //collectConfig(doc, pi);
    //collectPropsAndDirs(doc, pi);

    // $(ProjectDir)/$(SolutionDir) 解決
    const QString projDir = QFileInfo(vcxprojPath).absolutePath();
    const QString slnDir  = QFileInfo(projDir).absolutePath(); // 便宜上: proj の親を SolutionDir 相当とみなす
    
    //resolvePathsInSets(pi.includeDirs, projDir, slnDir);
    //resolvePathsInSets(pi.libDirs,     projDir, slnDir);
    resolvePathsInList(pi.cppFiles, projDir, slnDir);
    resolvePathsInList(pi.hFiles,   projDir, slnDir);
    resolvePathsInList(pi.uiFiles,  projDir, slnDir);
    resolvePathsInList(pi.qrcFiles, projDir, slnDir);
    resolvePathsInList(pi.resFiles, projDir, slnDir);

    const QString cmakeText = pi.GenerateCMake();

    out << cmakeText;
    
    return true;
}