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
    return p.replace('\\', '/');
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


struct ProjectInfo {
    QString projectName;
    QString targetType; // Application / StaticLibrary / DynamicLibrary
    QString configuration; // e.g., Debug, Release
    QString platform;      // e.g., x64, Win32
    bool    win32Subsystem = false;

    QStringList cppFiles;
    QStringList hFiles;
    QStringList uiFiles;
    QStringList qrcFiles;
    QStringList resFiles; // png/jpg/ico/bmp など（None から）

    QSet<QString> includeDirs;
    QSet<QString> libDirs;
    QSet<QString> additionalLibs;

    QSet<QString> qtModules; // Core, Gui, Widgets, ...

    // Settings
    QString configurationType; // Application, StaticLibrary, DynamicLibrary
    QString optimization;      // Disabled/MinSpace/MaxSpeed/Full
    QString runtimeLibrary;    // MultiThreaded, MultiThreadedDebug, MultiThreadedDLL, MultiThreadedDebugDLL
};

static void collectFiles(QDomDocument& doc, ProjectInfo& pi)
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
    collectIncludeAttr("ClCompile", pi.cppFiles);
    collectIncludeAttr("ClInclude", pi.hFiles);
    collectIncludeAttr("QtUic",     pi.uiFiles);
    collectIncludeAttr("QtRcc",     pi.qrcFiles);

    // None から画像などを拾う（png/jpg/jpeg/ico/bmp/svg等）
    QDomNodeList noneNodes = doc.elementsByTagName("None");
    for (int i = 0; i < noneNodes.size(); ++i) {
        QDomElement e = noneNodes.item(i).toElement();
        if (e.isNull()) continue;
        QString inc = e.attribute("Include").trimmed();
        if (inc.isEmpty()) continue;
        QString p = normSlashes(inc);
        QString ext = QFileInfo(p).suffix().toLower();
        if (QStringList({"png","jpg","jpeg","ico","bmp","svg","gif","tiff"}).contains(ext)) {
            pi.resFiles << p;
        }
    }

    // 重複排除
    auto dedup = [](QStringList& lst){
        QSet<QString> s(lst.begin(), lst.end());
        lst = QStringList(s.begin(), s.end());
        std::sort(lst.begin(), lst.end());
    };
    dedup(pi.cppFiles);
    dedup(pi.hFiles);
    dedup(pi.uiFiles);
    dedup(pi.qrcFiles);
    dedup(pi.resFiles);
}

static void collectConfig(QDomDocument& doc, ProjectInfo& pi)
{
    // ターゲット種別
    QDomNodeList cfg = doc.elementsByTagName("ConfigurationType");
    if (cfg.size() > 0) {
        pi.targetType = cfg.item(0).toElement().text().trimmed(); // 先頭を採用
    } else {
        pi.targetType = "Application"; // 既定
    }

    // SubSystem = Windows なら GUI(EXE) として WIN32 付与
    QDomNodeList subs = doc.elementsByTagName("SubSystem");
    for (int i = 0; i < subs.size(); ++i) {
        QString v = subs.item(i).toElement().text().trimmed();
        if (v.compare("Windows", Qt::CaseInsensitive) == 0) {
            pi.win32Subsystem = true;
            break;
        }
    }

    // 追加ディレクトリ・依存ライブラリ
    auto harvestListText = [&](const QString& tag, QSet<QString>& outSet){
        QDomNodeList nodes = doc.elementsByTagName(tag);
        for (int i = 0; i < nodes.size(); ++i) {
            QString text = nodes.item(i).toElement().text();
            for (const QString& one : splitList(text)) {
                if (!one.trimmed().isEmpty())
                    outSet.insert(normSlashes(one.trimmed()));
            }
        }
    };
    harvestListText("AdditionalIncludeDirectories", pi.includeDirs);
    harvestListText("AdditionalLibraryDirectories", pi.libDirs);

    // 追加依存（; 区切りの .lib 名等）。Qt は find_package に任せるが、サードパーティはここから。
    QDomNodeList depNodes = doc.elementsByTagName("AdditionalDependencies");
    for (int i = 0; i < depNodes.size(); ++i) {
        for (const QString& one : splitList(depNodes.item(i).toElement().text())) {
            QString t = one.trimmed();
            if (t.isEmpty()) continue;
            if (t.contains("%(")) continue;
            // 代表的なWin標準ライブラリは除外したい場合はここでフィルタ（任意）
            pi.additionalLibs.insert(t);
        }
    }

    // QtModules
    QDomNodeList qtMods = doc.elementsByTagName("QtModules");
    for (int i = 0; i < qtMods.size(); ++i) {
        for (const QString& mod : splitList(qtMods.item(i).toElement().text())) {
            QString m = titleCaseModule(mod);
            if (!m.isEmpty())
                pi.qtModules.insert(m);
        }
    }
}

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

static void resolvePathsInSets(QSet<QString>& setRef, const QString& projDir, const QString& slnDir)
{
    QSet<QString> out;
    for (const QString& p : setRef) {
        out.insert(normSlashes(resolveMSBuildVars(p, projDir, slnDir)));
    }
    setRef = out;
}

static void resolvePathsInList(QStringList& lst, const QString& projDir, const QString& slnDir)
{
    for (QString& p : lst) {
        p = normSlashes(resolveMSBuildVars(p, projDir, slnDir));
    }
}
static QString mapOptimizationToFlags(const QString &opt)
{
    // MSVC mappings; use generator expressions so only MSVC gets these
    if (opt.compare("Disabled", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Od>)";
    if (opt.compare("MinSpace", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/O1>)";
    if (opt.compare("MaxSpeed", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/O2>)";
    if (opt.compare("Full", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Ox>)";
    return {};
}

static QString mapRuntimeLibraryToFlags(const QString &rt)
{
    // Translate VS RuntimeLibrary to /MT, /MTd, /MD, /MDd
    if (rt.compare("MultiThreaded", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MT>)";
    if (rt.compare("MultiThreadedDebug", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MTd>)";
    if (rt.compare("MultiThreadedDLL", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MD>)";
    if (rt.compare("MultiThreadedDebugDLL", Qt::CaseInsensitive) == 0)
        return "target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MDd>)";
    return {};
}


static bool condMatches(const QString &cond, const QString &cfg, const QString &plat)
{
    if (cond.isEmpty()) return true; // no condition => applies to all
    // Typical: "'$(Configuration)|$(Platform)'=='Debug|x64'"
    QRegularExpression re("\\'\\$\\(Configuration\\)\\|\\$\\(Platform\\)\\'==\\'([^|]+)\\|([^\\']+)\\'",
                           QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(cond);
    if (!m.hasMatch()) return false;
    QString c = m.captured(1).trimmed();
    QString p = m.captured(2).trimmed();
    return (c.compare(cfg, Qt::CaseInsensitive) == 0) &&
           (p.compare(plat, Qt::CaseInsensitive) == 0);
}

static QString readText(QDomElement parent, const QString &tag)
{
    auto nlist = parent.elementsByTagName(tag);
    if (nlist.isEmpty()) return {};
    return nlist.at(0).toElement().text().trimmed();
}


static QStringList splitMsbuildList(const QString &value)
{
    // MSBuild uses ';' separated lists. Empty segments allowed.
    QString v = value;
    // Replace commas in Visual Studio weirdness? keep simple.
    QStringList parts = v.split(';', Qt::SkipEmptyParts);
    for (QString &p : parts) p = p.trimmed();
    parts.removeAll("");
    // Unique, keep order
    QStringList unique;
    for (const QString &p : parts) if (!unique.contains(p)) unique << p;
    return unique;
}
static void collectPropsAndDirs(const QDomDocument &doc, ProjectInfo &out)
{
    // Project name
    QDomNodeList pn = doc.elementsByTagName("ProjectName");
    if (!pn.isEmpty()) out.projectName = pn.at(0).toElement().text().trimmed();
    if (out.projectName.isEmpty()) {
        // Fallback to <RootNamespace>
        QDomNodeList rn = doc.elementsByTagName("RootNamespace");
        if (!rn.isEmpty()) out.projectName = rn.at(0).toElement().text().trimmed();
    }

    // PropertyGroup Label="Configuration"
    QDomNodeList pgs = doc.elementsByTagName("PropertyGroup");
    for (int i=0;i<pgs.count();++i){
        QDomElement pg = pgs.at(i).toElement();
        QString cond = pg.attribute("Condition");
        if (!condMatches(cond, out.configuration, out.platform)) continue;
        QString confType = readText(pg, "ConfigurationType");
        if (!confType.isEmpty()) out.configurationType = confType;
    }

    // ItemDefinitionGroup -> ClCompile / Link
    QDomNodeList idgs = doc.elementsByTagName("ItemDefinitionGroup");
    for (int i=0;i<idgs.count();++i){
        QDomElement idg = idgs.at(i).toElement();
        QString cond = idg.attribute("Condition");
        if (!condMatches(cond, out.configuration, out.platform)) continue;

        auto clcList = idg.elementsByTagName("ClCompile");
        if (!clcList.isEmpty()) {
            QDomElement clc = clcList.at(0).toElement();
            QString incDirs = readText(clc, "AdditionalIncludeDirectories");
            for (const QString &raw : splitMsbuildList(incDirs)) {
                if (raw.trimmed().isEmpty()) continue;
                out.includeDirs << raw.trimmed();
            }
            QString opt = readText(clc, "Optimization");
            if (!opt.isEmpty()) out.optimization = opt;
            QString rt = readText(clc, "RuntimeLibrary");
            if (!rt.isEmpty()) out.runtimeLibrary = rt;
        }
        auto linkList = idg.elementsByTagName("Link");
        if (!linkList.isEmpty()) {
            QDomElement link = linkList.at(0).toElement();
            QString libDirs = readText(link, "AdditionalLibraryDirectories");
            for (const QString &raw : splitMsbuildList(libDirs)) {
                if (raw.trimmed().isEmpty()) continue;
                out.libDirs << raw.trimmed();
            }
        }
    }

    //// Normalize dirs (expand $(ProjectDir) etc.)
    //QString base = out.projectDir;
    //auto normalizeList = [&](QStringList &lst){
    //    QStringList n;
    //    for (const QString &s : lst) {
    //        QString v = s;
    //        v.replace("$(Configuration)", out.configuration);
    //        v.replace("$(Platform)", out.platform);
    //        v = normPath(base, v);
    //        if (!n.contains(v)) n << v;
    //    }
    //    lst = n;
    //};
    //normalizeList(out.includeDirs);
    //normalizeList(out.libDirs);
}
static QString cmakeEscape(const QString& s)
{
    QString r = s;
    r.replace("\"", "\\\"");
    return r;
}

static QString generateCMake(const ProjectInfo& pi)
{
    QStringList lines;

    // 1) 基本ヘッダ
    lines << "cmake_minimum_required(VERSION 3.16)";
    lines << QString("project(%1 LANGUAGES CXX)").arg(cmakeEscape(pi.projectName));
    lines << "set(CMAKE_CXX_STANDARD 17)";
    lines << "set(CMAKE_CXX_STANDARD_REQUIRED ON)";
    lines << "";

    // 2) Qt 自動機能
    lines << "set(CMAKE_AUTOMOC ON)";
    lines << "set(CMAKE_AUTOUIC ON)";
    lines << "set(CMAKE_AUTORCC ON)";
    lines << "";

    // 3) find_package(Qt6 ...)
    QStringList qtMods = QStringList(pi.qtModules.begin(), pi.qtModules.end());
    std::sort(qtMods.begin(), qtMods.end());
    if (qtMods.isEmpty()) {
        // モジュール未記載でも最低限 Core は想定
        qtMods << "Core";
    }
    lines << QString("find_package(Qt6 REQUIRED COMPONENTS %1)")
                .arg(qtMods.join(' '));
    lines << "";

    // 4) ファイル群を変数化（見やすさのため）
    auto joinQuoted = [](const QStringList& lst) {
        QStringList out;
        for (const QString& p : lst) out << QString("\"%1\"").arg(cmakeEscape(p));
        return out.join("\n    ");
    };

    if (!pi.cppFiles.isEmpty()) {
        lines << "set(PROJECT_SOURCES";
        lines << "    " + joinQuoted(pi.cppFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "# Note: No .cpp found in the .vcxproj; add sources if needed.";
        lines << "set(PROJECT_SOURCES)";
        lines << "";
    }

    if (!pi.hFiles.isEmpty()) {
        lines << "set(PROJECT_HEADERS";
        lines << "    " + joinQuoted(pi.hFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_HEADERS)";
        lines << "";
    }

    if (!pi.uiFiles.isEmpty()) {
        lines << "set(PROJECT_FORMS";
        lines << "    " + joinQuoted(pi.uiFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_FORMS)";
        lines << "";
    }

    if (!pi.qrcFiles.isEmpty()) {
        lines << "set(PROJECT_RESOURCES";
        lines << "    " + joinQuoted(pi.qrcFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_RESOURCES)";
        lines << "";
    }

    if (!pi.resFiles.isEmpty()) {
        lines << "set(PROJECT_ASSETS";
        lines << "    " + joinQuoted(pi.resFiles);
        lines << ")";
        lines << "";
    } else {
        lines << "set(PROJECT_ASSETS)";
        lines << "";
    }

    // 5) ターゲット生成
    QString addTarget;
    QString targetName = pi.projectName;
    QString sourcesExpr =
        " ${PROJECT_SOURCES} ${PROJECT_FORMS} ${PROJECT_RESOURCES} ${PROJECT_HEADERS} ${PROJECT_ASSETS}";

    if (pi.targetType.compare("Application", Qt::CaseInsensitive) == 0) {
        if (pi.win32Subsystem)
            addTarget = QString("add_executable(%1 WIN32%2)").arg(cmakeEscape(targetName), sourcesExpr);
        else
            addTarget = QString("add_executable(%1%2)").arg(cmakeEscape(targetName), sourcesExpr);
    } else if (pi.targetType.compare("StaticLibrary", Qt::CaseInsensitive) == 0) {
        addTarget = QString("add_library(%1 STATIC%2)").arg(cmakeEscape(targetName), sourcesExpr);
    } else if (pi.targetType.compare("DynamicLibrary", Qt::CaseInsensitive) == 0) {
        addTarget = QString("add_library(%1 SHARED%2)").arg(cmakeEscape(targetName), sourcesExpr);
    } else {
        // 未知の場合は実行ファイルにフォールバック
        addTarget = QString("# Unknown ConfigurationType='%1', fallback to executable")
                        .arg(cmakeEscape(pi.targetType));
        lines << addTarget;
        addTarget = QString("add_executable(%1%2)").arg(cmakeEscape(targetName), sourcesExpr);
    }
    lines << addTarget;
    lines << "";

    // 6) include & lib ディレクトリ
    if (!pi.includeDirs.isEmpty()) {
        QStringList incs = QStringList(pi.includeDirs.begin(), pi.includeDirs.end());
        std::sort(incs.begin(), incs.end());
        lines << QString("target_include_directories(%1 PRIVATE").arg(cmakeEscape(targetName));
        for (const auto& p : incs) {
            lines << QString("    \"%1\"").arg(cmakeEscape(p));
        }
        lines << ")";
        lines << "";
    }

    if (!pi.libDirs.isEmpty()) {
        QStringList libs = QStringList(pi.libDirs.begin(), pi.libDirs.end());
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
    for (const QString& m : qtMods) linkTargets << ("Qt6::" + m);

    if (!pi.additionalLibs.isEmpty()) {
        QStringList deps = QStringList(pi.additionalLibs.begin(), pi.additionalLibs.end());
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


    QCommandLineParser parser;
    parser.setApplicationDescription("Convert VS2022 .vcxproj to CMakeLists.txt (Qt6)");
    parser.addPositionalArgument("vcxproj", "Path to .vcxproj file");
    QCommandLineOption cfgOpt({"c", "config"}, "Configuration (Debug/Release)", "name", "Debug");
    QCommandLineOption platOpt({"p", "platform"}, "Platform (x64/Win32)", "name", "x64");
    QCommandLineOption outOpt({"o", "out"}, "Output CMakeLists.txt path", "file", "CMakeLists.txt");
    parser.addOption(cfgOpt);
    parser.addOption(platOpt);
    parser.addOption(outOpt);

    pi.configuration = parser.value(cfgOpt);
    pi.platform = parser.value(platOpt);

    collectFiles(doc, pi);
    collectConfig(doc, pi);
    collectPropsAndDirs(doc, pi);

    // $(ProjectDir)/$(SolutionDir) 解決
    const QString projDir = QFileInfo(vcxprojPath).absolutePath();
    const QString slnDir  = QFileInfo(projDir).absolutePath(); // 便宜上: proj の親を SolutionDir 相当とみなす
    resolvePathsInSets(pi.includeDirs, projDir, slnDir);
    resolvePathsInSets(pi.libDirs,     projDir, slnDir);
    resolvePathsInList(pi.cppFiles, projDir, slnDir);
    resolvePathsInList(pi.hFiles,   projDir, slnDir);
    resolvePathsInList(pi.uiFiles,  projDir, slnDir);
    resolvePathsInList(pi.qrcFiles, projDir, slnDir);
    resolvePathsInList(pi.resFiles, projDir, slnDir);

    const QString cmakeText = generateCMake(pi);

    out << cmakeText;
    
    return true;



























    // <ConfigurationType>要素からターゲット種別を取得
    QDomNodeList configList = doc.elementsByTagName("ConfigurationType");
    QString configType;
    if (configList.count() > 0) {
        configType = configList.at(0).toElement().text().trimmed();
    }
    // CMake の add_executable / add_library を選択
    if (configType == "Application") {
        out << "add_executable(MyTarget ${SOURCES})\n";
    } else if (configType == "StaticLibrary") {
        out << "add_library(MyTarget STATIC ${SOURCES})\n";
    } else if (configType == "DynamicLibrary") {
        out << "add_library(MyTarget SHARED ${SOURCES})\n";
    }

    // PreprocessorDefinitions を取得
    QDomNodeList defList = doc.elementsByTagName("PreprocessorDefinitions");
    if (defList.count() > 0) {
        QStringList defs = defList.at(0).toElement().text().split(';', Qt::SkipEmptyParts);
        // %(PreprocessorDefinitions) を含むマクロは除外
        defs.erase(std::remove_if(defs.begin(), defs.end(),
            [](const QString &s){ return s.startsWith("%("); }), defs.end());
        if (!defs.empty()) {
            out << "target_compile_definitions(MyTarget PRIVATE";
            for (const QString &d : defs)
                out << " " << d;
            out << ")\n";
        }
    }

    // AdditionalDependencies を取得
    QDomNodeList linkList = doc.elementsByTagName("AdditionalDependencies");
    if (linkList.count() > 0) {
        QStringList libs = linkList.at(0).toElement().text().split(';', Qt::SkipEmptyParts);
        libs.erase(std::remove_if(libs.begin(), libs.end(),
            [](const QString &s){ return s.startsWith("%("); }), libs.end());
        if (!libs.empty()) {
            out << "target_link_libraries(MyTarget PRIVATE";
            for (const QString &lib : libs)
                out << " " << lib;
            out << ")\n";
        }
    }

    // AdditionalLibraryDirectories を取得
    QDomNodeList dirList = doc.elementsByTagName("AdditionalLibraryDirectories");
    if (dirList.count() > 0) {
        QStringList dirs = dirList.at(0).toElement().text().split(';', Qt::SkipEmptyParts);
        dirs.erase(std::remove_if(dirs.begin(), dirs.end(),
            [](const QString &s){ return s.startsWith("%("); }), dirs.end());
        if (!dirs.empty()) {
            out << "target_link_directories(MyTarget PRIVATE";
            for (const QString &dir : dirs)
                out << " \"" << dir << "\"";
            out << ")\n";
        }
    }

    // 仮に QtModules 要素があれば取得
    QDomNodeList qtList = doc.elementsByTagName("QtModules");
    if (qtList.count() > 0) {
        QStringList mods = qtList.at(0).toElement().text().split(';', Qt::SkipEmptyParts);
        if (!mods.empty()) {
            out << "find_package(Qt6 REQUIRED COMPONENTS";
            for (const QString &m : mods)
                out << " " << m;
            out << ")\n";
            out << "target_link_libraries(MyTarget PRIVATE";
            for (const QString &m : mods)
                out << " Qt6::" << m;
            out << ")\n";
        }
    }
    return true;
}