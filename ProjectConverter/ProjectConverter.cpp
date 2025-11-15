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

void ProjectConverter::on_pushButtonConvert_clicked()
{
    for(int i=0;i<ProjectFileName.count();i++){
        CreateCMakeFile(ProjectFileName[i]);
    }
}

VcxprojParser::VcxprojParser(const QString& vcxprojPath)
    : m_vcxprojPath(vcxprojPath) {
    QFileInfo fileInfo(vcxprojPath);
    m_projectDir = fileInfo.absolutePath();
    m_projectName = fileInfo.baseName();
}

bool VcxprojParser::parse() {
    QFile file(m_vcxprojPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errorString = "Failed to open vcxproj file: " + file.errorString();
        return false;
    }

    QString errorMsg;
    int errorLine, errorColumn;
    if (!m_doc.setContent(&file, &errorMsg, &errorLine, &errorColumn)) {
        m_errorString = QString("Failed to parse XML: %1 (Line: %2, Col: %3)")
            .arg(errorMsg).arg(errorLine).arg(errorColumn);
        file.close();
        return false;
    }
    file.close();

    QDomElement root = m_doc.documentElement(); // <Project>
    if (root.tagName() != "Project") {
        m_errorString = "Not a valid vcxproj file (missing <Project> root tag).";
        return false;
    }

    try {
        parseProjectConfigurations(root);
        parseGlobalProperties(root);
        parseConfigProperties(root);
        parseItemDefinitions(root);
        parseItemGroups(root);
    }
    catch (const std::exception& e) {
        m_errorString = QString("Exception during parsing: %1").arg(e.what());
        return false;
    }
    catch (...) {
        m_errorString = "Unknown exception during parsing.";
        return false;
    }

    if (m_configs.isEmpty()) {
        m_errorString = "No valid configurations found.";
        return false;
    }

    return true;
}

// Condition 属性 (例: "'$(Configuration)|$(Platform)'=='Debug|x64'") を解析
QString VcxprojParser::extractCondition(const QDomElement& element) const {
    if (!element.hasAttribute("Condition")) {
        return QString();
    }
    // 正規表現で 'Config|Platform' の形式を抽出
    static const QRegularExpression condRegex(R"('(?:\$\(Configuration\)\|\$\(Platform\))'=='([^']*)')");
    QRegularExpressionMatch match = condRegex.match(element.attribute("Condition"));
    if (match.hasMatch()) {
        return match.captured(1); // "Debug|x64"
    }
    return QString();
}

// <ItemGroup Label="ProjectConfigurations"> を解析
void VcxprojParser::parseProjectConfigurations(const QDomElement& root) {
    QDomNodeList itemGroups = root.elementsByTagName("ItemGroup");
    for (int i = 0; i < itemGroups.count(); ++i) {
        QDomElement itemGroup = itemGroups.at(i).toElement();
        if (itemGroup.attribute("Label") != "ProjectConfigurations") {
            continue;
        }

        QDomNodeList configs = itemGroup.elementsByTagName("ProjectConfiguration");
        for (int j = 0; j < configs.count(); ++j) {
            QDomElement configEl = configs.at(j).toElement();
            QString configName = configEl.attribute("Include"); // "Debug|x64"

            if (configName.isEmpty()) continue;

            VcxprojConfig config;
            config.configName = configName;

            QString configPart = configEl.firstChildElement("Configuration").text(); // "Debug"
            QString platformPart = configEl.firstChildElement("Platform").text();   // "x64"

            config.configType = configPart;
            config.platform = platformPart;
            m_platforms.insert(platformPart);

            m_configs.insert(configName, config);
        }
    }
}

// <PropertyGroup> (グローバル設定) を解析
void VcxprojParser::parseGlobalProperties(const QDomElement& root) {
    QDomNodeList propGroups = root.elementsByTagName("PropertyGroup");
    for (int i = 0; i < propGroups.count(); ++i) {
        QDomElement propGroup = propGroups.at(i).toElement();
        
        // グローバルプロパティ (Condition なし、または Label="Globals")
        if (propGroup.hasAttribute("Condition") && !propGroup.attribute("Condition").isEmpty()) {
             if (propGroup.attribute("Label") != "Globals") {
                continue;
             }
        }
       
        // ルート名前空間やプロジェクト名 (あれば)
        QDomElement projNameEl = propGroup.firstChildElement("ProjectName");
        if (!projNameEl.isNull()) {
            m_projectName = projNameEl.text();
        }
    }
}

// <PropertyGroup Condition="..."> (構成ごと) を解析
void VcxprojParser::parseConfigProperties(const QDomElement& root) {
    QDomNodeList propGroups = root.elementsByTagName("PropertyGroup");
    for (int i = 0; i < propGroups.count(); ++i) {
        QDomElement propGroup = propGroups.at(i).toElement();
        QString condition = extractCondition(propGroup);

        if (condition.isEmpty() || !m_configs.contains(condition)) {
            continue;
        }

        VcxprojConfig& config = m_configs[condition];

        // ターゲットタイプ (Application, StaticLibrary, DynamicLibrary)
        QDomElement configTypeEl = propGroup.firstChildElement("ConfigurationType");
        if (!configTypeEl.isNull()) {
            config.configurationType = configTypeEl.text();
        }
    }
}

// <ItemDefinitionGroup Condition="..."> (構成ごと) を解析
void VcxprojParser::parseItemDefinitions(const QDomElement& root) {
    QDomNodeList defGroups = root.elementsByTagName("ItemDefinitionGroup");
    for (int i = 0; i < defGroups.count(); ++i) {
        QDomElement defGroup = defGroups.at(i).toElement();
        QString condition = extractCondition(defGroup);

        if (condition.isEmpty() || !m_configs.contains(condition)) {
            continue;
        }

        VcxprojConfig& config = m_configs[condition];

        // <ClCompile>
        QDomElement clCompile = defGroup.firstChildElement("ClCompile");
        if (!clCompile.isNull()) {
            config.includeDirectories = getSemicolonList(clCompile, "AdditionalIncludeDirectories");
            config.preprocessorDefinitions = getSemicolonList(clCompile, "PreprocessorDefinitions");

            QDomElement opt = clCompile.firstChildElement("Optimization");
            if (!opt.isNull()) config.optimization = opt.text();

            QDomElement rtLib = clCompile.firstChildElement("RuntimeLibrary");
            if (!rtLib.isNull()) config.runtimeLibrary = rtLib.text();
        }

        // <Link>
        QDomElement link = defGroup.firstChildElement("Link");
        if (!link.isNull()) {
            config.libraryDirectories = getSemicolonList(link, "AdditionalLibraryDirectories");
            config.additionalDependencies = getSemicolonList(link, "AdditionalDependencies");
        }
    }
}

// <ItemGroup> (ファイルリスト) を解析
void VcxprojParser::parseItemGroups(const QDomElement& root) {
    QDomNodeList itemGroups = root.elementsByTagName("ItemGroup");
    for (int i = 0; i < itemGroups.count(); ++i) {
        QDomElement itemGroup = itemGroups.at(i).toElement();
        
        // ラベル付き ItemGroup (ProjectConfigurations や Globals) はスキップ
        if (itemGroup.hasAttribute("Label")) continue;

        auto extractFiles = [&](const QString& tagName, QStringList& targetList) {
            QDomNodeList items = itemGroup.elementsByTagName(tagName);
            for (int j = 0; j < items.count(); ++j) {
                QDomElement item = items.at(j).toElement();
                QString file = item.attribute("Include");
                if (!file.isEmpty()) {
                    targetList.append(toCMakePath(file));
                }
            }
        };

        extractFiles("ClCompile", m_sources);
        extractFiles("ClInclude", m_headers);
        extractFiles("QtUic", m_uiFiles); // Qt VS Tools
        extractFiles("QtRcc", m_qrcFiles); // Qt VS Tools
        
        // .ico, .png など (Image タグや ResourceCompile タグなど)
        extractFiles("Image", m_resourceFiles);
        extractFiles("ResourceCompile", m_resourceFiles);
        // "None" タグに含まれるリソースファイルもあるかもしれない
        // extractFiles("None", m_resourceFiles); // 必要に応じてフィルタリング
    }
    
    m_sources.removeDuplicates();
    m_headers.removeDuplicates();
    m_uiFiles.removeDuplicates();
    m_qrcFiles.removeDuplicates();
    m_resourceFiles.removeDuplicates();
}

// タグから ; 区切りのリストを取得
QStringList VcxprojParser::getSemicolonList(const QDomElement& parent, const QString& tagName) {
    QDomElement el = parent.firstChildElement(tagName);
    if (el.isNull()) {
        return QStringList();
    }
    // %(AdditionalIncludeDirectories) などのマクロを展開する必要があるが、
    // ここでは簡略化のため、マクロは無視し、純粋なテキストのみを扱う
    QString text = el.text().replace("%(AdditionalIncludeDirectories)", "")
                           .replace("%(AdditionalLibraryDirectories)", "")
                           .replace("%(PreprocessorDefinitions)", "");
                           
    return text.split(';', Qt::SkipEmptyParts);
}

QString VcxprojParser::toCMakePath(const QString& path) const {
    // MSBuild のパス (バックスラッシュ) を CMake (スラッシュ) に変換
    // $(ProjectDir) などのマクロも展開する必要があるが、ここでは相対パスと仮定
    return QString(path).replace("\\", "/");
}

QStringList VcxprojParser::toCMakePaths(const QStringList& paths) const {
    QStringList result;
    for (const QString& path : paths) {
        // $(SolutionDir) などの vcxproj マクロを解決する必要がある
        // ここでは単純な $(ProjectDir) のみ CMAKE_CURRENT_SOURCE_DIR に置き換える
        // (非常に不完全なマクロ展開)
        QString p = path;
        if (p.startsWith("$(ProjectDir)")) {
            p.replace("$(ProjectDir)", "${CMAKE_CURRENT_SOURCE_DIR}");
        } else if (p.startsWith("$(SolutionDir)")) {
             p.replace("$(SolutionDir)", "${CMAKE_SOURCE_DIR}"); // 推測
        }
        
        // ..\ などの相対パスを解決
        // QFileInfo(m_projectDir, path).absoluteFilePath() を使うと絶対パスにできる
        
        result.append(toCMakePath(p));
    }
    return result;
}


// ----------------------------------------------------------------
// CMakeLists.txt 生成
// ----------------------------------------------------------------

QString VcxprojParser::generateCMakeLists() const {
    QString content;
    QTextStream ss(&content);

    appendHeader(content);
    appendProject(content);
    
    bool needsQt = !m_uiFiles.isEmpty() || !m_qrcFiles.isEmpty();
    if (needsQt) {
        appendQtSetup(content);
    }

    appendFileLists(content);
    
    if (needsQt) {
        if (!m_uiFiles.isEmpty()) {
            ss << "qt_wrap_ui(PROJECT_SOURCES ${PROJECT_FORMS})\n";
        }
        if (!m_qrcFiles.isEmpty()) {
            ss << "qt_add_resources(PROJECT_SOURCES ${PROJECT_RESOURCES})\n";
        }
        ss << "\n";
    }

    appendTarget(content);
    appendTargetProperties(content);

    if (needsQt) {
         ss << "\n# Qt モジュールへのリンク\n";
         ss << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
         ss << "    Qt6::Core\n";
         ss << "    Qt6::Gui\n";
         ss << "    Qt6::Widgets\n";
         // .vcxproj から Qt モジュール (Network, Xml など) を検出するのは困難
         // ここでは標準的なものを追加
         ss << ")\n";
    }

    return content;
}

void VcxprojParser::appendHeader(QString& content) const {
    QTextStream ss(&content);
    ss << "# CMakeLists.txt generated from " << QFileInfo(m_vcxprojPath).fileName() << "\n";
    ss << "# WARNING: This is an automatically generated file. Manual edits may be overwritten.\n";
    ss << "# VCXPROJ-to-CMAKE limitations:\n";
    ss << "# - MSBuild macros (e.g., $(SolutionDir)) may not be translated correctly.\n";
    ss << "# - Complex conditions (custom platforms, etc.) are likely ignored.\n";
    ss << "# - Linked libraries (e.g., system libs, other projects) may be incomplete.\n";
    ss << "\n";
    ss << "cmake_minimum_required(VERSION 3.16)\n\n";
}

void VcxprojParser::appendProject(QString& content) const {
    QTextStream ss(&content);
    ss << "project(" << m_projectName << " LANGUAGES CXX)\n\n";
    ss << "set(CMAKE_CXX_STANDARD 17)\n";
    ss << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    ss << "set(CMAKE_AUTOMOC ON)\n";
    ss << "set(CMAKE_AUTORCC ON)\n";
    ss << "set(CMAKE_AUTOUIC ON)\n\n";

    // Win32 / x64 のための分岐 (あれば)
    if (m_platforms.contains("Win32")) {
         ss << "if(CMAKE_GENERATOR_PLATFORM STREQUAL \"Win32\")\n";
         ss << "    # Settings for Win32\n";
         ss << "endif()\n";
    }
    if (m_platforms.contains("x64")) {
         ss << "if(CMAKE_GENERATOR_PLATFORM STREQUAL \"x64\")\n";
         ss << "    # Settings for x64\n";
         ss << "endif()\n";
    }
    ss << "\n";
}

void VcxprojParser::appendQtSetup(QString& content) const {
     QTextStream ss(&content);
     ss << "# Find Qt6\n";
     ss << "find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)\n"; 
     ss << "# TODO: Add other required Qt components (e.g., Network, Xml)\n\n";
}

void VcxprojParser::appendFileLists(QString& content) const {
    QTextStream ss(&content);
    
    auto appendList = [&](const QString& varName, const QStringList& files) {
        if (files.isEmpty()) return;
        ss << "set(" << varName << "\n";
        for (const QString& file : files) {
            ss << "    \"" << file << "\"\n";
        }
        ss << ")\n\n";
    };

    appendList("PROJECT_SOURCES", m_sources);
    appendList("PROJECT_HEADERS", m_headers);
    appendList("PROJECT_FORMS", m_uiFiles);
    appendList("PROJECT_RESOURCES", m_qrcFiles);
    
    if (!m_resourceFiles.isEmpty()) {
        ss << "# Other resource files (icons, images)\n";
        appendList("PROJECT_OTHER_RESOURCES", m_resourceFiles);
        // Note: これらのリソースをどう扱うか (qrc に含めるか？) は vcxproj だけでは判断困難
    }
}

void VcxprojParser::appendTarget(QString& content) const {
    QTextStream ss(&content);
    
    // 最初の構成 (または Release|x64) をデフォルトとしてターゲットタイプを決定
    // 本来は CMake 側で if(CONFIG) すべきだが、ターゲットタイプは通常全構成で同じ
    QString targetType = "Application"; // Default
    if (m_configs.contains("Release|x64")) {
        targetType = m_configs["Release|x64"].configurationType;
    } else if (!m_configs.isEmpty()) {
        targetType = m_configs.first().configurationType;
    }

    ss << "# Define the target\n";
    if (targetType == "Application") {
        ss << "add_executable(${PROJECT_NAME} WIN32\n"; // WIN32 (WinMain) を想定
    } else if (targetType == "StaticLibrary") {
        ss << "add_library(${PROJECT_NAME} STATIC\n";
    } else if (targetType == "DynamicLibrary") {
        ss << "add_library(${PROJECT_NAME} SHARED\n";
    } else {
        ss << "# WARNING: Unknown ConfigurationType: " << targetType << ". Defaulting to Application.\n";
        ss << "add_executable(${PROJECT_NAME} WIN32\n";
    }
    
    ss << "    ${PROJECT_SOURCES}\n";
    ss << "    ${PROJECT_HEADERS}\n";
    // .ui, .qrc は qt_wrap_ui/qt_add_resources が SOURCES に追加する
    ss << ")\n\n";
}

void VcxprojParser::appendTargetProperties(QString& content) const {
    QTextStream ss(&content);

    ss << "# === Configuration-specific settings ===\n\n";

    for (const VcxprojConfig& config : m_configs.values()) {
        QString configNameUpper = config.configType.toUpper(); // DEBUG, RELEASE

        // プラットフォーム (x64/Win32) を考慮した条件
        // if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
        // ただし、CMake VS Generator は CMAKE_GENERATOR_PLATFORM を使わない...
        // Generator Expression ( $<CONFIG:Debug> ) を使うのが最善

        // --- Include Directories ---
        if (!config.includeDirectories.isEmpty()) {
            ss << "target_include_directories(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << config.configType << ">:\n";
            QStringList cmakePaths = toCMakePaths(config.includeDirectories);
            for (const QString& path : cmakePaths) {
                // TODO: $(Platform) などのマクロ展開
                QString p = path;
                p.replace("$(Platform)", config.platform); 
                ss << "        \"" << p << "\"\n";
            }
            ss << "    >\n)\n\n";
        }
        
        // --- Library Directories ---
        if (!config.libraryDirectories.isEmpty()) {
            ss << "target_link_directories(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << config.configType << ">:\n";
            QStringList cmakePaths = toCMakePaths(config.libraryDirectories);
            for (const QString& path : cmakePaths) {
                QString p = path;
                p.replace("$(Platform)", config.platform);
                ss << "        \"" << p << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Preprocessor Definitions ---
        if (!config.preprocessorDefinitions.isEmpty()) {
            ss << "target_compile_definitions(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << config.configType << ">:\n";
            for (const QString& def : config.preprocessorDefinitions) {
                if (def == "%(PreprocessorDefinitions)") continue;

                QString deft=def;
                ss << "        \"" << deft.replace(QString("\""), QString("\\\"")) << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Optimization ---
        appendOptimizationFlags(content, config);

        // --- Code Generation (Runtime Library) ---
        appendRuntimeLibraryFlags(content, config);
        
        // --- Linked Libraries ---
        if (!config.additionalDependencies.isEmpty()) {
             ss << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
             ss << "    $<$<CONFIG:" << config.configType << ">:\n";
             QStringList libs = config.additionalDependencies;
             libs.removeAll("%(AdditionalDependencies)");
             for(const QString& lib : libs) {
                // .lib 拡張子がない場合は、そのまま (例: kernel32.lib)
                // ある場合はフルパスかもしれないので toCMakePath
                if (lib.endsWith(".lib", Qt::CaseInsensitive)) {
                     ss << "        \"" << toCMakePath(lib) << "\"\n";
                } else {
                     ss << "        " << lib << "\n";
                }
             }
             ss << "    >\n)\n\n";
        }

        ss << "# --- End of " << config.configName << " ---\n\n";
    }
}


void VcxprojParser::appendOptimizationFlags(QString& content, const VcxprojConfig& config) const {
    if (config.optimization.isEmpty()) return;

    QTextStream ss(&content);
    QString cmakeFlag;

    if (config.optimization == "Disabled") { // /Od
        cmakeFlag = "/Od";
    } else if (config.optimization == "MinSpace") { // /O1
        cmakeFlag = "/O1";
    } else if (config.optimization == "MaxSpeed") { // /O2
        cmakeFlag = "/O2";
    } else if (config.optimization == "Full") { // /Ox
        cmakeFlag = "/Ox";
    } else {
        ss << "# WARNING: Unknown Optimization flag: " << config.optimization << "\n";
        return;
    }

    ss << "target_compile_options(${PROJECT_NAME} PRIVATE\n";
    ss << "    $<$<CONFIG:" << config.configType << ">:" << cmakeFlag << ">\n";
    ss << ")\n";
}

void VcxprojParser::appendRuntimeLibraryFlags(QString& content, const VcxprojConfig& config) const {
    if (config.runtimeLibrary.isEmpty()) return;

    QTextStream ss(&content);
    QString cmakeFlag;

    // MultiThreadedDebugDLL (/MDd)
    // MultiThreadedDLL (/MD)
    // MultiThreadedDebug (/MTd)
    // MultiThreaded (/MT)
    
    if (config.runtimeLibrary == "MultiThreadedDebugDLL") cmakeFlag = "/MDd";
    else if (config.runtimeLibrary == "MultiThreadedDLL") cmakeFlag = "/MD";
    else if (config.runtimeLibrary == "MultiThreadedDebug") cmakeFlag = "/MTd";
    else if (config.runtimeLibrary == "MultiThreaded") cmakeFlag = "/MT";
    else {
         ss << "# WARNING: Unknown RuntimeLibrary flag: " << config.runtimeLibrary << "\n";
         return;
    }

    ss << "target_compile_options(${PROJECT_NAME} PRIVATE\n";
    ss << "    $<$<CONFIG:" << config.configType << ">:" << cmakeFlag << ">\n";
    ss << ")\n";
}



bool    ProjectConverter::CreateCMakeFile(const QString &ProjFileName)
{
    QString vcxprojPath = ProjFileName;
    QFileInfo vcxprojInfo(vcxprojPath);
    if (!vcxprojInfo.exists()) {
         //std::cerr << "Error: File not found: " << qPrintable(vcxprojPath) << std::endl;
         return 1;
    }

    QString outputPath = vcxprojInfo.absolutePath() + "/CMakeLists.txt";

    // --- 処理の実行 ---
    //std::cout << "Parsing " << qPrintable(vcxprojPath) << "..." << std::endl;

    VcxprojParser vcxprojParser(vcxprojPath);
    if (!vcxprojParser.parse()) {
        //std::cerr << "Error during parsing:\n" << qPrintable(vcxprojParser.errorString()) << std::endl;
        return 1;
    }

    //std::cout << "Generating CMakeLists.txt..." << std::endl;
    QString cmakeContent = vcxprojParser.generateCMakeLists();

    // --- ファイル出力 ---
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        //std::cerr << "Error: Could not write to output file: " << qPrintable(outputPath) << std::endl;
        return 1;
    }

    QTextStream outStream(&outFile);
    outStream.setCodec("UTF-8"); // CMakeLists.txt は UTF-8 を推奨
    outStream << cmakeContent;
    outFile.close();

    //std::cout << "Successfully generated " << qPrintable(outputPath) << std::endl;

}