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
            }
            else
            if(FInfo.suffix().toUpper()==/**/"VCXPROJ"){
                ProjectFileName.append(FileName);
            }
        }
    }
}


bool    ProjectConverter::LoadSLN(const QString &SLNFileName,QStringList &ProjList)
{
    QFile file(SLNFileName);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QFileInfo   SLNInfo(SLNFileName);
	QString     SLNPath = SLNInfo.absolutePath();

    QTextStream in(&file);
    in.setAutoDetectUnicode(true);  // UTF-8 BOM対応など
    // 正規表現でProject行から .vcxproj を抽出
    QRegularExpression re("Project\\(\"[^\"]*\"\\)\\s*=\\s*\"[^\"]*\"\\s*,\\s*\"([^\"]*\\.vcxproj)\"");

    while (!in.atEnd()) {
        QString line = in.readLine();
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            ProjList << SLNPath+QDir::separator()+match.captured(1);
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
        parseQtModules(root);
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

void VcxprojParser::parseQtModules(const QDomElement& root) {
    QDomNodeList propGroups = root.elementsByTagName("PropertyGroup");
    for (int i = 0; i < propGroups.count(); ++i) {
        QDomElement propGroup = propGroups.at(i).toElement();
        if (propGroup.attribute("Label") == "QtSettings") {
            QDomElement qtModulesEl = propGroup.firstChildElement("QtModules");
            if (!qtModulesEl.isNull()) {
                QStringList modules = qtModulesEl.text().split(';', Qt::SkipEmptyParts);
                for (const QString& mod : modules) {
                    m_qtModules.insert(mod);
                }
            }
        }
    }
}


QStringList VcxprojParser::getQtComponents() const {
    QStringList qtComponents;
    for (const QString& mod : m_qtModules) {
        QString m = mod.trimmed().toLower();
        
        // 基本モジュール
        if (m == "core") qtComponents.append("Core");
        else if (m == "gui") qtComponents.append("Gui");
        else if (m == "network") qtComponents.append("Network");
        else if (m == "widgets") qtComponents.append("Widgets");
        else if (m == "sql") qtComponents.append("Sql");
        else if (m == "core5compat") qtComponents.append("Core5Compat");
        else if (m == "xml") qtComponents.append("Xml");
        
        // OpenGL 関連 (大文字小文字の区別が特殊なもの)
        else if (m == "opengl") qtComponents.append("OpenGL");
        else if (m == "openglwidgets") qtComponents.append("OpenGLWidgets");
        
        // その他の一般的なモジュール
        else if (m == "printsupport") qtComponents.append("PrintSupport");
        else if (m == "svg") qtComponents.append("Svg");
        else if (m == "svgwidgets") qtComponents.append("SvgWidgets");
        else if (m == "multimedia") qtComponents.append("Multimedia");
        else if (m == "multimediawidgets") qtComponents.append("MultimediaWidgets");
        else if (m == "qml") qtComponents.append("Qml");
        else if (m == "quick") qtComponents.append("Quick");
        else if (m == "quickwidgets") qtComponents.append("QuickWidgets");
        else if (m == "serialport") qtComponents.append("SerialPort");
        else if (m == "websockets") qtComponents.append("WebSockets");
        else if (m == "bluetooth") qtComponents.append("Bluetooth");
        else if (m == "concurrent") qtComponents.append("Concurrent");
        else if (m == "testlib" || m == "test") qtComponents.append("Test");
        else if (m == "axcontainer") qtComponents.append("AxContainer");
        else if (m == "axserver") qtComponents.append("AxServer");
        else if (m == "dbus") qtComponents.append("DBus");
        else if (m == "uitools") qtComponents.append("UiTools");
        else if (m == "help") qtComponents.append("Help");
        else if (m == "designer") qtComponents.append("Designer");
        
        // リストにない未知のモジュールのフォールバック処理 (先頭だけ大文字にする)
        else {
            if (!m.isEmpty()) {
                m[0] = m[0].toUpper();
                qtComponents.append(m);
            }
        }
    }
    
    // 重複を削除
    qtComponents.removeDuplicates();
    
    // モジュールが1つも設定されていなかった場合のデフォルト
    if (qtComponents.isEmpty()) {
        qtComponents << "Core" << "Gui" << "Widgets";
    }
    
    return qtComponents;
}
// Condition 属性を解析
QString VcxprojParser::extractCondition(const QDomElement& element) const {
    if (!element.hasAttribute("Condition")) {
        return QString();
    }
    QString conditionStr = element.attribute("Condition");
    int index = conditionStr.indexOf("==");
    if (index != -1) {
        QString right = conditionStr.mid(index + 2).trimmed();
        right.remove('\'');
        return right; // e.g., "Debug|x64"
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

        // 出力ディレクトリ (OutDir)
        QDomElement outDirEl = propGroup.firstChildElement("OutDir");
        if (!outDirEl.isNull()) {
            config.outDir = outDirEl.text();
        }
    }
}

// <ItemDefinitionGroup Condition="..."> (構成ごと) を解析
void VcxprojParser::parseItemDefinitions(const QDomElement& root) {
    QDomNodeList defGroups = root.elementsByTagName("ItemDefinitionGroup");
    for (int i = 0; i < defGroups.count(); ++i) {
        QDomElement defGroup = defGroups.at(i).toElement();
        QString condition = extractCondition(defGroup);

        QList<VcxprojConfig*> targetConfigs;
        
        if (condition.isEmpty()) {
            // Conditionが指定されていない場合は、すべての構成に適用する
            for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
                targetConfigs.append(&it.value());
            }
        } else if (m_configs.contains(condition)) {
            // 指定された構成に適用
            targetConfigs.append(&m_configs[condition]);
        } else {
            continue;
        }

        for (VcxprojConfig* config : targetConfigs) {
            // <ClCompile>
            QDomElement clCompile = defGroup.firstChildElement("ClCompile");
            if (!clCompile.isNull()) {
                QStringList incDirs = getSemicolonList(clCompile, "AdditionalIncludeDirectories");
                if (!incDirs.isEmpty()) {
                    config->includeDirectories.append(incDirs);
                    config->includeDirectories.removeDuplicates();
                }

                QStringList prepDefs = getSemicolonList(clCompile, "PreprocessorDefinitions");
                if (!prepDefs.isEmpty()) {
                    config->preprocessorDefinitions.append(prepDefs);
                    config->preprocessorDefinitions.removeDuplicates();
                }

                QDomElement opt = clCompile.firstChildElement("Optimization");
                if (!opt.isNull() && !opt.text().isEmpty()) config->optimization = opt.text();

                QDomElement rtLib = clCompile.firstChildElement("RuntimeLibrary");
                if (!rtLib.isNull() && !rtLib.text().isEmpty()) config->runtimeLibrary = rtLib.text();
            }

            // <Link> または <Lib> (静的ライブラリ用)
            QDomElement link = defGroup.firstChildElement("Link");
            if (link.isNull()) {
                link = defGroup.firstChildElement("Lib");
            }
            
            if (!link.isNull()) {
                QStringList libDirs = getSemicolonList(link, "AdditionalLibraryDirectories");
                if (!libDirs.isEmpty()) {
                    config->libraryDirectories.append(libDirs);
                    config->libraryDirectories.removeDuplicates();
                }

                QStringList addDeps = getSemicolonList(link, "AdditionalDependencies");
                if (!addDeps.isEmpty()) {
                    config->additionalDependencies.append(addDeps);
                    config->additionalDependencies.removeDuplicates();
                }
            }
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

// ----------------------------------------------------------------
// CMakeLists.txt 生成
// ----------------------------------------------------------------

QString VcxprojParser::generateCMakeLists() const {
    QString content;
    QTextStream ss(&content);

    appendHeader(content);
    appendProject(content);
    
    // 【修正】UIファイルがなくても、Qtモジュールが指定されていれば Qtセットアップを行う
    bool needsQt = !m_qtModules.isEmpty() || !m_uiFiles.isEmpty() || !m_qrcFiles.isEmpty();
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

    ss << "if(WIN32)\n";
    ss << "    add_compile_definitions(WIN32 _WINDOWS)\n";
    ss << "endif()\n\n";

    ss << "if(UNIX)\n";
    ss << "    if(CMAKE_SYSTEM_NAME STREQUAL \"Linux\")\n";
    ss << "        # Linux / Raspberry Pi specific settings\n";
    ss << "    endif()\n";
    ss << "endif()\n\n";
}

void VcxprojParser::appendQtSetup(QString& content) const {
    QTextStream ss(&content);
    ss << "# Find Qt6\n";
    ss << "find_package(Qt6 REQUIRED COMPONENTS";
    
    QStringList comps = getQtComponents();
    for (const QString& comp : comps) {
        ss << " " << comp;
    }
    ss << ")\n\n";
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
    
    QString targetType = "Application";
    if (m_configs.contains("Release|x64")) {
        targetType = m_configs["Release|x64"].configurationType;
    } else if (!m_configs.isEmpty()) {
        targetType = m_configs.first().configurationType;
    }

    ss << "# Define the target\n";
    if (targetType == "Application") {
        ss << "add_executable(${PROJECT_NAME}\n";
    } else if (targetType == "StaticLibrary") {
        ss << "add_library(${PROJECT_NAME} STATIC\n";
    } else if (targetType == "DynamicLibrary") {
        ss << "add_library(${PROJECT_NAME} SHARED\n";
    } else {
        ss << "# WARNING: Unknown ConfigurationType: " << targetType << ". Defaulting to Application.\n";
        ss << "add_executable(${PROJECT_NAME}\n";
    }
    
    ss << "    ${PROJECT_SOURCES}\n";
    ss << "    ${PROJECT_HEADERS}\n";
    ss << ")\n\n";
}

void VcxprojParser::appendTargetProperties(QString& content) const {
    QTextStream ss(&content);

    ss << "# === Configuration-specific settings ===\n\n";

    QSet<QString> processedConfigs;

    for (const VcxprojConfig& config : m_configs.values()) {
        QString configType = config.configType;
        if (processedConfigs.contains(configType)) {
            continue;
        }
        processedConfigs.insert(configType);

        // --- Output Directory ---
        QString outDir;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType && !cfg.outDir.isEmpty()) {
                outDir = cfg.outDir;
                break;
            }
        }

        if (!outDir.isEmpty()) {
            // パス区切り文字の変換とマクロの解決
            QString cmakeOutDir = outDir.replace("\\", "/");
            cmakeOutDir.replace("$(ProjectDir)", "${CMAKE_CURRENT_SOURCE_DIR}/");
            cmakeOutDir.replace("$(SolutionDir)", "${CMAKE_SOURCE_DIR}/");
            cmakeOutDir.replace("$(Configuration)", configType);
            
            // 末尾のスラッシュを削除
            if (cmakeOutDir.endsWith("/")) {
                cmakeOutDir.chop(1);
            }

            // 絶対パスやCMake変数が先頭についていない相対パスの場合、現在のディレクトリを付与
            if (!cmakeOutDir.startsWith("/") && !cmakeOutDir.contains(":") && !cmakeOutDir.startsWith("${")) {
                cmakeOutDir = "${CMAKE_CURRENT_SOURCE_DIR}/" + cmakeOutDir;
            }

            QString upperConfig = configType.toUpper();
            ss << "set_target_properties(${PROJECT_NAME} PROPERTIES\n";
            ss << "    ARCHIVE_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << "    LIBRARY_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << "    RUNTIME_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << ")\n\n";
        }

        // --- Include Directories ---
        QSet<QString> includeDirs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                for (const QString& path : cfg.includeDirectories) {
                    includeDirs.insert(path);
                }
            }
        }
        
        if (!includeDirs.isEmpty()) {
            ss << "target_include_directories(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << configType << ">:\n";
            
            QStringList includeList;
            for (const QString& path : includeDirs) {
                includeList.append(path);
            }
            
            QStringList cmakePaths = toCMakePaths(includeList);
            for (const QString& path : cmakePaths) {
                ss << "        \"" << path << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Library Directories ---
        QSet<QString> libDirs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                for (const QString& path : cfg.libraryDirectories) {
                    if (path != "%(AdditionalLibraryDirectories)") {
                        libDirs.insert(path);
                    }
                }
            }
        }
        
        if (!libDirs.isEmpty()) {
            ss << "target_link_directories(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << configType << ">:\n";
            
            QStringList libDirList;
            for (const QString& path : libDirs) {
                libDirList.append(path);
            }
            
            QStringList cmakePaths = toCMakePaths(libDirList);
            for (const QString& path : cmakePaths) {
                ss << "        \"" << path << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Preprocessor Definitions ---
        QSet<QString> definitions;
        QSet<QString> win32Definitions;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                for (const QString& def : cfg.preprocessorDefinitions) {
                    if (def != "%(PreprocessorDefinitions)") {
                        if (def.trimmed().toUpper() == "WIN32" || def.trimmed().toUpper() == "_WINDOWS") {
                            win32Definitions.insert(def);
                        } else {
                            definitions.insert(def);
                        }
                    }
                }
            }
        }

        if (!definitions.isEmpty()) {
            ss << "target_compile_definitions(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << configType << ">:\n";
            for (const QString& def : definitions) {
                QString deft = def;
                ss << "        \"" << deft.replace(QString("\""), QString("\\\"")) << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        if (!win32Definitions.isEmpty()) {
            ss << "if(WIN32)\n";
            ss << "    target_compile_definitions(${PROJECT_NAME} PRIVATE\n";
            ss << "        $<$<CONFIG:" << configType << ">:\n";
            for (const QString& def : win32Definitions) {
                ss << "            \"" << def << "\"\n";
            }
            ss << "        >\n";
            ss << "    )\n";
            ss << "endif()\n\n";
        }
        
        // --- Linked Libraries ---
        QSet<QString> libs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                for (const QString& lib : cfg.additionalDependencies) {
                    if (lib != "%(AdditionalDependencies)") {
                        // Qtのモジュールは末尾で一括リンクするため除外
                        if (lib.contains("QtCore", Qt::CaseInsensitive) ||
                            lib.contains("QtGui", Qt::CaseInsensitive) ||
                            lib.contains("QtSql", Qt::CaseInsensitive) ||
                            lib.contains("QtNetwork", Qt::CaseInsensitive)) {
                            continue;
                        }
                        libs.insert(lib);
                    }
                }
            }
        }

        if (!libs.isEmpty()) {
            // Windows 向けのリンク設定 (そのまま .lib として出力)
            ss << "if(WIN32)\n";
            ss << "    target_link_libraries(${PROJECT_NAME} PRIVATE\n";
            ss << "        $<$<CONFIG:" << configType << ">:\n";
            for (const QString& lib : libs) {
                ss << "            \"" << toCMakePath(lib) << "\"\n";
            }
            ss << "        >\n";
            ss << "    )\n";
            
            // Linux / Raspberry Pi (UNIX) 向けのリンク設定 (lib〜.a に自動変換)
            ss << "elseif(UNIX)\n";
            ss << "    target_link_libraries(${PROJECT_NAME} PRIVATE\n";
            ss << "        $<$<CONFIG:" << configType << ">:\n";
            for (const QString& lib : libs) {
                QString unixLib = toCMakePath(lib); // バックスラッシュをスラッシュに変換
                
                // .lib で終わるファイル名の場合、プレフィックス(lib)と拡張子(.a)を変換
                if (unixLib.endsWith(".lib", Qt::CaseInsensitive)) {
                    int lastSlash = unixLib.lastIndexOf('/');
                    if (lastSlash == -1) {
                        // パスが含まれていない場合 (例: Component.lib -> libComponent.a)
                        unixLib = "lib" + unixLib.left(unixLib.length() - 4) + ".a";
                    } else {
                        // パスが含まれている場合 (例: ../Lib/Release/Component.lib -> ../Lib/Release/libComponent.a)
                        QString path = unixLib.left(lastSlash + 1);
                        QString fileName = unixLib.mid(lastSlash + 1);
                        unixLib = path + "lib" + fileName.left(fileName.length() - 4) + ".a";
                    }
                }
                ss << "            \"" << unixLib << "\"\n";
            }
            ss << "        >\n";
            ss << "    )\n";
            ss << "endif()\n\n";
        }

        // --- Compiler/Optimization Options (MSVC Only) ---
        ss << "if(MSVC)\n";
        appendOptimizationFlags(content, config);
        appendRuntimeLibraryFlags(content, config);
        ss << "endif()\n\n";

        ss << "# --- End of " << configType << " ---\n\n";
    }

    // Qt 6 モジュールへのリンク
    ss << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
    QStringList comps = getQtComponents();
    for (const QString& comp : comps) {
        ss << "    Qt6::" << comp << "\n";
    }
    ss << ")\n";
}

void VcxprojParser::appendOptimizationFlags(QString& content, const VcxprojConfig& config) const {
    if (config.optimization.isEmpty()) return;

    QTextStream ss(&content);
    QString cmakeFlag;

    if (config.optimization == "Disabled") {
        cmakeFlag = "/Od";
    } else if (config.optimization == "MinSpace") {
        cmakeFlag = "/O1";
    } else if (config.optimization == "MaxSpeed") {
        cmakeFlag = "/O2";
    } else if (config.optimization == "Full") {
        cmakeFlag = "/Ox";
    } else {
        ss << "# WARNING: Unknown Optimization flag: " << config.optimization << "\n";
        return;
    }

    ss << "    target_compile_options(${PROJECT_NAME} PRIVATE\n";
    ss << "        $<$<CONFIG:" << config.configType << ">:" << cmakeFlag << ">\n";
    ss << "    )\n";
}

void VcxprojParser::appendRuntimeLibraryFlags(QString& content, const VcxprojConfig& config) const {
    if (config.runtimeLibrary.isEmpty()) return;

    QTextStream ss(&content);
    QString cmakeFlag;

    if (config.runtimeLibrary == "MultiThreadedDebugDLL") cmakeFlag = "/MDd";
    else if (config.runtimeLibrary == "MultiThreadedDLL") cmakeFlag = "/MD";
    else if (config.runtimeLibrary == "MultiThreadedDebug") cmakeFlag = "/MTd";
    else if (config.runtimeLibrary == "MultiThreaded") cmakeFlag = "/MT";
    else {
         ss << "# WARNING: Unknown RuntimeLibrary flag: " << config.runtimeLibrary << "\n";
         return;
    }

    ss << "    target_compile_options(${PROJECT_NAME} PRIVATE\n";
    ss << "        $<$<CONFIG:" << config.configType << ">:" << cmakeFlag << ">\n";
    ss << "    )\n";
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