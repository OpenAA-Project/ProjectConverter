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
#include "ProjectConverter.h"

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

    bool hasLinkFree = false;
    for (const VcxprojConfig& config : m_configs.values()) {
        for (const QString& def : config.preprocessorDefinitions) {
            // 大文字小文字を区別せずに "LINKFREE" と完全に一致するか確認
            if (def.trimmed().compare("LINKFREE", Qt::CaseInsensitive) == 0) {
                hasLinkFree = true;
                break;
            }
        }
        if (hasLinkFree) break;
    }

    if (hasLinkFree) {
        // オプションリストから対象の文字列を除外する（ダブルクォーテーション有無の両方に対応）
        m_additionalLinkOptimizations.removeAll("-Wl,--no-undefined");
        m_additionalLinkOptimizations.removeAll("\"-Wl,--no-undefined\"");
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
  
        // Qt 6 で廃止されたモジュールを除外する
        else if (m == "xmlpatterns") {
            continue; // Qt 6には存在しないためスキップ
        }      
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
            for (auto it = m_configs.begin(); it != m_configs.end(); ++it) targetConfigs.append(&it.value());
        } else if (m_configs.contains(condition)) {
            targetConfigs.append(&m_configs[condition]);
        } else {
            continue;
        }

        for (VcxprojConfig* config : targetConfigs) {
            // --- 既存の ClCompile (AdditionalIncludeDirectories) 解析 ---
            QDomElement clCompile = defGroup.firstChildElement("ClCompile");
            if (!clCompile.isNull()) {
                QStringList incDirs = getSemicolonList(clCompile, "AdditionalIncludeDirectories");
                for (const QString& dir : incDirs) {
                    QString d = dir.trimmed();
                    QString d_norm = d;
                    d_norm.replace("\\", "/");
                    if (d_norm.contains("$(QTDIR)/include", Qt::CaseInsensitive)) continue;
                    
                    // configTypeを渡して置換
                    config->includeDirectories.append(applyMacroReplacements(d, config->configType));
                }
                QStringList preDefs = getSemicolonList(clCompile, "PreprocessorDefinitions");
                for (const QString& def : preDefs) {
                    QString d = def.trimmed();
                    if (!d.isEmpty() && d != "%(PreprocessorDefinitions)") {
                        config->preprocessorDefinitions.append(applyMacroReplacements(d, config->configType));
                    }
                }
                config->preprocessorDefinitions.removeDuplicates();
            }
            QDomElement parallelEl = clCompile.firstChildElement("Parallelization");
            if (!parallelEl.isNull() && parallelEl.text().trimmed().toLower() == "true") {
                config->useOpenMP = true;
            }
            QDomElement openmpEl = clCompile.firstChildElement("OpenMP");
            if (!openmpEl.isNull() && openmpEl.text().trimmed() == "GenerateParallelCode") {
                config->useOpenMP = true;
            }
            QDomElement openmpSupportEl = clCompile.firstChildElement("OpenMPSupport");
            if (!openmpSupportEl.isNull() && openmpSupportEl.text().trimmed().toLower() == "true") {
                config->useOpenMP = true;
            }

            // --- Qt User Interface Compiler (UIC) の出力ディレクトリ解析 ---
            QDomElement qtUic = defGroup.firstChildElement("QtUic");
            if (!qtUic.isNull()) {
                QDomElement outputFileEl = qtUic.firstChildElement("OutputFile");
                if (!outputFileEl.isNull()) {
                    QString outPath = outputFileEl.text().trimmed(); // 例: ".\GeneratedFiles\ui_%(Filename).h"
                    
                    // ディレクトリ部分のみを抽出
                    outPath.replace("\\", "/");
                    int lastSlash = outPath.lastIndexOf('/');
                    if (lastSlash != -1) {
                        QString uicDir = outPath.left(lastSlash);
                        // マクロ置換を適用してインクルードパスに追加
                        QString resolvedUicDir = applyMacroReplacements(uicDir, config->configType);
                        if (!resolvedUicDir.isEmpty()) {
                            config->includeDirectories.append(resolvedUicDir);
                        }
                    }
                }
            }
            
            config->includeDirectories.removeDuplicates();

            // --- リンカ設定 (AdditionalLibraryDirectories / Dependencies) ---
            QDomElement link = defGroup.firstChildElement("Link");
            if (link.isNull()) link = defGroup.firstChildElement("Lib");
            if (!link.isNull()) {
                QStringList libDirs = getSemicolonList(link, "AdditionalLibraryDirectories");
                for (const QString& dir : libDirs) {
                    QString d = dir.trimmed();
                    QString d_norm = d;
                    d_norm.replace("\\", "/");
                    if (d_norm.contains("$(QTDIR)/lib", Qt::CaseInsensitive)) continue;
                    config->libraryDirectories.append(applyMacroReplacements(d, config->configType));
                }
                config->libraryDirectories.removeDuplicates();

                QStringList addDeps = getSemicolonList(link, "AdditionalDependencies");
                for (const QString& dep : addDeps) {
                    QString d = dep.trimmed();
                    if (!d.isEmpty() && d != "%(AdditionalDependencies)") {
                        config->additionalDependencies.append(applyMacroReplacements(d, config->configType));
                    }
                }
                config->additionalDependencies.removeDuplicates();

                QDomElement stackReserve = link.firstChildElement("StackReserveSize");
                if (!stackReserve.isNull()) config->stackReserveSize = stackReserve.text().trimmed();

                QDomElement stackCommit = link.firstChildElement("StackCommitSize");
                if (!stackCommit.isNull()) config->stackCommitSize = stackCommit.text().trimmed();

                QDomElement heapReserve = link.firstChildElement("HeapReserveSize");
                if (!heapReserve.isNull()) config->heapReserveSize = heapReserve.text().trimmed();

                QDomElement heapCommit = link.firstChildElement("HeapCommitSize");
                if (!heapCommit.isNull()) config->heapCommitSize = heapCommit.text().trimmed();
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

        // 通常のソースとヘッダー
        extractFiles("ClCompile", m_sources);
        extractFiles("ClInclude", m_headers);

        // QtMoc要素もヘッダーとして抽出する
        extractFiles("QtMoc", m_headers);

        // 古いプロジェクトファイル構造用に CustomBuild の中にあるヘッダーも抽出
        QDomNodeList customBuildItems = itemGroup.elementsByTagName("CustomBuild");
        for (int j = 0; j < customBuildItems.count(); ++j) {
            QDomElement item = customBuildItems.at(j).toElement();
            QString file = item.attribute("Include");
            if (!file.isEmpty()) {
                if (file.endsWith(".h", Qt::CaseInsensitive) || file.endsWith(".hpp", Qt::CaseInsensitive)) {
                    m_headers.append(toCMakePath(file));
                }
                else if (file.endsWith(".qrc", Qt::CaseInsensitive)) {
                    m_qrcFiles.append(toCMakePath(file)); // .qrc ファイルを追加
                }
            }
        }

        // <None> タグとして登録されている .qrc ファイルも抽出
        QDomNodeList noneItems = itemGroup.elementsByTagName("None");
        for (int j = 0; j < noneItems.count(); ++j) {
            QDomElement item = noneItems.at(j).toElement();
            QString file = item.attribute("Include");
            if (!file.isEmpty() && file.endsWith(".qrc", Qt::CaseInsensitive)) {
                m_qrcFiles.append(toCMakePath(file));
            }
        }

        // Qtリソース
        extractFiles("QtUic", m_uiFiles); 
        extractFiles("QtRcc", m_qrcFiles); 
        
        // .ico, .png など (Image タグや ResourceCompile タグなど)
        extractFiles("Image", m_resourceFiles);
        extractFiles("ResourceCompile", m_resourceFiles);
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

// マクロの置換処理
QString VcxprojParser::applyMacroReplacements(const QString& str, const QString& configType) const {
    QString result = str;
    
    // 1: $(ConfigurationName) を空文字に変換
    result.replace("$(ConfigurationName)", "", Qt::CaseInsensitive);

    // 2: $(QtInstDir) の動的置換 (追加)
    if (!configType.isEmpty()) {
        if (configType.compare("Debug", Qt::CaseInsensitive) == 0) {
            result.replace("$(QtInstDir)", "/x64/Debug/qt", Qt::CaseInsensitive);
        } else {
            result.replace("$(QtInstDir)", "/x64/Release/qt", Qt::CaseInsensitive);
        }
    }

    // ユーザー指定のマクロを置換
    for (const MatchingDim* match = m_macroReplacements.GetFirst(); match != NULL; match = match->GetNext()) {
        QString keyword = match->MStr.trimmed();
        if (keyword.isEmpty()) continue;

        if (keyword.startsWith("$(") && keyword.endsWith(")")) {
            result.replace(keyword, match->RStr, Qt::CaseInsensitive);
        } else {
            result.replace("$(" + keyword + ")", match->RStr, Qt::CaseInsensitive);
        }
    }

    // 未解決マクロの抽出ロジック
    QRegularExpression re("\\$\\(([^)]+)\\)");
    QRegularExpressionMatchIterator it = re.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString macroName = match.captured(1); // 括弧の中身 "XXX"

        if (macroName.compare("ProjectDir", Qt::CaseInsensitive) != 0 && 
            macroName.compare("SolutionDir", Qt::CaseInsensitive) != 0 && 
            macroName.compare("Configuration", Qt::CaseInsensitive) != 0 && 
            macroName.compare("Platform", Qt::CaseInsensitive) != 0) {
            m_unresolvedMacros.insert(macroName); 
        }
    }

    return result;
}

void VcxprojParser::setAdditionalIncludeDirs(const QStringList& dirs)
{
    m_additionalIncludeDirs.clear();
    for (const QString& dir : dirs) {
        QString d = dir;
        d.replace("\\", "/");
        if (!d.contains("$(QTDIR)/include", Qt::CaseInsensitive)) {
            m_additionalIncludeDirs.append(d);
        }
    }
}

void VcxprojParser::setAdditionalLibraryDirs(const QStringList& dirs)
{
    m_additionalLibraryDirs.clear();
    for (const QString& dir : dirs) {
        QString d = dir;
        d.replace("\\", "/");
        if (!d.contains("$(QTDIR)/lib", Qt::CaseInsensitive)) {
            m_additionalLibraryDirs.append(d);
        }
    }
}

QString VcxprojParser::toCMakePath(const QString& path, const QString& configType) const {
    // 置換結果に対して .trimmed() を実行し、前後の空白・改行を完全に除去
    QString p = applyMacroReplacements(path, configType).trimmed();
    
    p.replace("\\", "/");
    while (p.contains("//")) {
        p.replace("//", "/");
    }
    
    return p.trimmed();
}

QStringList VcxprojParser::toCMakePaths(const QStringList& paths, const QString& configType) const {
    QStringList result;
    for (const QString& path : paths) {
        // 取得したパスの空白・改行を除去
        QString p = applyMacroReplacements(path, configType).trimmed();
        
        if (p.contains("$(ProjectDir)")) {
            p.replace("$(ProjectDir)", "${CMAKE_CURRENT_SOURCE_DIR}/");
        } else if (p.contains("$(SolutionDir)")) {
            p.replace("$(SolutionDir)", "${CMAKE_SOURCE_DIR}/"); 
        }
        
        // 空文字列になっていなければ追加する
        QString cleanPath = toCMakePath(p, configType);
        if (!cleanPath.isEmpty()) {
            result.append(cleanPath);
        }
    }
    return result;
}

// ----------------------------------------------------------------
// CMakeLists.txt 生成
// ----------------------------------------------------------------
QString VcxprojParser::generateCMakeLists() const
{
    // CMakeLists.txtの生成を開始する前に、リストを一度クリアする
    m_unresolvedMacros.clear();

    QString content;
    QTextStream ss(&content);

    bool hasOpenMP = m_forceOpenMP;
    bool useGL = false;
    bool useGLU = false;
    bool useGlfw = false;
    bool useGlew = false;
    bool usePNG = false;

    for (const QString& lib : m_additionalLibraryFiles) {
        if (lib.compare("PNG::PNG", Qt::CaseInsensitive) == 0) {
            usePNG = true;
            break;
        }
    }

    // プロジェクトの依存ライブラリを走査して OpenGL/GLFW/GLEW の使用を判定
    for (const VcxprojConfig& config : m_configs.values()) {
        if (config.useOpenMP) {
            hasOpenMP = true;
        }
        for (const QString& lib : config.additionalDependencies) {
            QString lowerLib = QFileInfo(lib).completeBaseName().toLower();
            if (lowerLib.startsWith("opengl32")) useGL = true;
            if (lowerLib.startsWith("glu32")) { useGL = true; useGLU = true; }
            if (lowerLib.startsWith("glfw")) useGlfw = true;
            if (lowerLib.startsWith("glew")) useGlew = true;
        }
    }
    
    appendHeader(content);
    appendProject(content);
    
    // UIファイルがなくても、Qtモジュールが指定されていれば Qtセットアップを行う
    bool needsQt = !m_qtModules.isEmpty() || !m_uiFiles.isEmpty() || !m_qrcFiles.isEmpty();
    if (needsQt) {
        appendQtSetup(content);
    }

    // --- 追加: OpenGL / GLFW / GLEW / PNG の検索 ---
    if (useGL || useGlfw || useGlew || usePNG) {
        ss << "# Find OpenGL / GLFW / GLEW\n";
        if (useGL) ss << "find_package(OpenGL REQUIRED)\n";
        if (useGlfw) ss << "find_package(glfw3 REQUIRED)\n";
        if (useGlew) ss << "find_package(GLEW REQUIRED)\n";
        if (usePNG) ss << "find_package(PNG REQUIRED)\n";
        ss << "\n";
    }

    if (hasOpenMP) {
        ss << "# Find OpenMP\n";
        ss << "find_package(OpenMP REQUIRED)\n\n";
    }

    appendFileLists(content);
    
    appendTarget(content);
    
    appendTargetProperties(content);

    // --- 追加のInclude, Lib, 構成オプションを出力 ---
    appendAdditionalSettings(content);

    // --- 追加: OpenGL / GLFW / GLEW / PNG のリンク ---
    if (useGL || useGlfw || useGlew || usePNG) {
        ss << "# Link OpenGL / GLFW / GLEW\n";
        ss << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
        if (useGL) ss << "    OpenGL::GL\n";
        if (useGLU) ss << "    OpenGL::GLU\n";
        if (useGlfw) ss << "    glfw\n";
        if (useGlew) ss << "    GLEW::GLEW\n";
        if (usePNG) ss << "    PNG::PNG\n";
        ss << ")\n\n";
    }
 
    if (hasOpenMP) {
        ss << "# Link OpenMP\n";
        ss << "target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)\n\n";
    }
 
    // --- .desktop.in ファイルの自動検出とインストール設定 ---
    QDir projDir(m_projectDir);
    QStringList desktopInFiles = projDir.entryList(QStringList() << "*.desktop.in", QDir::Files);
    if (!desktopInFiles.isEmpty()) {
        QString desktopInName = desktopInFiles.first(); // 最初に見つかった .desktop.in を使用
        QString desktopOutName = desktopInName;
        desktopOutName.chop(3); // 末尾の ".in" を削除して出力ファイル名を作成

        ss << "# =========================================================================\n";
        ss << "# .desktop file configuration and installation\n";
        ss << "# =========================================================================\n";
        ss << "if(UNIX AND NOT APPLE)\n";
        ss << "    configure_file(\n";
        ss << "        \"${CMAKE_CURRENT_SOURCE_DIR}/" << desktopInName << "\"\n";
        ss << "        \"${CMAKE_CURRENT_BINARY_DIR}/" << desktopOutName << "\"\n";
        ss << "        @ONLY\n";
        ss << "    )\n";
        ss << "    install(\n";
        ss << "        FILES \"${CMAKE_CURRENT_BINARY_DIR}/" << desktopOutName << "\"\n";
        ss << "        DESTINATION \"$ENV{HOME}/.local/share/applications\"\n";
        ss << "    )\n";
        ss << "endif()\n\n";
    }

    // ▼ 未解決マクロがある場合、CMakeLists.txt の末尾に警告コメントとして出力
    QStringList unresolvedList = getUnresolvedMacros();
    if (!unresolvedList.isEmpty()) {
        ss << "\n# =========================================================================\n";
        ss << "# WARNING: The following MSBuild macros were not resolved during conversion:\n";
        for (const QString& macro : unresolvedList) {
            ss << "#   $(" << macro << ")\n";
        }
        ss << "# Please define them in CMake or add them to the macro replacements list.\n";
        ss << "# =========================================================================\n";
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
    ss << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n";
    ss << "set(CMAKE_AUTOMOC ON)\n";
    ss << "set(CMAKE_AUTORCC ON)\n";
    ss << "set(CMAKE_AUTOUIC ON)\n\n";

    ss << "if(WIN32)\n";
    ss << "    add_compile_definitions(WIN32 _WINDOWS)\n";
    ss << "endif()\n\n";

    ss << "if(UNIX)\n";
    ss << "    # Dead code stripping (remove unused functions/data)\n";
    ss << "    add_compile_options(-ffunction-sections -fdata-sections)\n";
    ss << "    add_link_options(\"-Wl,--gc-sections\")\n";
    ss << "    # Allow multiple definitions to mimic MSVC static library linking behavior\n";
    ss << "    add_link_options(\"-Wl,--allow-multiple-definition\")\n\n";
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

    // 実際のファイルシステム上の名称を取得するためのキャッシュ付きラムダ関数
    QMap<QString, QStringList> dirCache;
    auto resolveActualCasing = [&](const QString& relativePath) -> QString {
        QStringList parts = relativePath.split('/');
        QString currentPath = QFileInfo(relativePath).isAbsolute() ? "" : m_projectDir;
        QString resolvedPath;

        for (int i = 0; i < parts.size(); ++i) {
            QString part = parts[i];
            
            // Linuxの絶対パス ("/") の場合
            if (part.isEmpty() && i == 0 && relativePath.startsWith("/")) {
                currentPath = "/";
                resolvedPath = "";
                continue;
            }
            // Windowsのドライブレター
            if (part.endsWith(":") && i == 0) {
                currentPath = part;
                resolvedPath = part;
                continue;
            }

            if (part == "." || part == "..") {
                currentPath += (currentPath.endsWith("/") || currentPath.isEmpty() ? "" : "/") + part;
                resolvedPath += (resolvedPath.isEmpty() ? "" : "/") + part;
                continue;
            }

            QString searchPath = currentPath.isEmpty() ? "/" : currentPath;
            if (!dirCache.contains(searchPath)) {
                QDir dir(searchPath);
                dirCache[searchPath] = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
            }
            
            // キャッシュから大文字小文字を区別せずに一致するものを探す
            for (const QString& entry : dirCache[searchPath]) {
                if (entry.compare(part, Qt::CaseInsensitive) == 0) {
                    part = entry; // 実在する名前に置き換え（例: generatedfiles -> GeneratedFiles）
                    break;
                }
            }

            currentPath += (currentPath.endsWith("/") || currentPath.isEmpty() ? "" : "/") + part;
            resolvedPath += (resolvedPath.isEmpty() ? "" : "/") + part;
        }
        return resolvedPath;
    };

    QStringList commonSources;
    QStringList debugSources;
    QStringList releaseSources;

    // ソースファイルを実際のファイル名に補正しつつ、振り分け
    for (const QString& file : m_sources) {
        QString actualFile = resolveActualCasing(file);
        QString lowerFile = actualFile.toLower(); // 判定用に小文字化

        if (lowerFile.contains("/debug/") || lowerFile.startsWith("debug/")) {
            debugSources.append(actualFile);
        } else if (lowerFile.contains("/release/") || lowerFile.startsWith("release/")) {
            releaseSources.append(actualFile);
        } else {
            commonSources.append(actualFile);
        }
    }

    // 他のリスト全体を実際の名称に補正するためのヘルパー
    auto resolveList = [&](const QStringList& list) -> QStringList {
        QStringList resolved;
        for (const QString& file : list) {
            resolved.append(resolveActualCasing(file));
        }
        return resolved;
    };

    auto appendList = [&](const QString& varName, const QStringList& files) {
        if (files.isEmpty()) return;
        ss << "set(" << varName << "\n";
        for (const QString& file : files) {
            ss << "    \"" << file << "\"\n";
        }
        ss << ")\n\n";
    };

    // 共通ファイルと各種リソースを書き出し (それぞれ実際の名称に補正)
    appendList("PROJECT_SOURCES", commonSources);
    appendList("PROJECT_HEADERS", resolveList(m_headers));
    appendList("PROJECT_FORMS", resolveList(m_uiFiles));
    appendList("PROJECT_RESOURCES", resolveList(m_qrcFiles));
    
    // 構成ごとのファイルを書き出し
    if (!debugSources.isEmpty()) {
        appendList("PROJECT_SOURCES_DEBUG", debugSources);
    }
    if (!releaseSources.isEmpty()) {
        appendList("PROJECT_SOURCES_RELEASE", releaseSources);
    }

    if (!m_resourceFiles.isEmpty()) {
        ss << "# Other resource files (icons, images)\n";
        appendList("PROJECT_OTHER_RESOURCES", resolveList(m_resourceFiles));
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
    // UIファイルとリソースファイルを追加
    if (!m_uiFiles.isEmpty()) {
        ss << "    ${PROJECT_FORMS}\n";
    }
    if (!m_qrcFiles.isEmpty()) {
        ss << "    ${PROJECT_RESOURCES}\n";
    }
    ss << ")\n\n";

    // --- 構成ごとのソースファイルをターゲットに紐づける ---
    ss << "# === Configuration-specific sources ===\n";
    ss << "if(DEFINED PROJECT_SOURCES_DEBUG)\n";
    ss << "    target_sources(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:${PROJECT_SOURCES_DEBUG}>)\n";
    ss << "endif()\n\n";

    ss << "if(DEFINED PROJECT_SOURCES_RELEASE)\n";
    ss << "    target_sources(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:${PROJECT_SOURCES_RELEASE}>)\n";
    ss << "endif()\n\n";
}

void VcxprojParser::appendTargetProperties(QString& content) const {
    QTextStream ss(&content);

    ss << "# === Configuration-specific settings ===\n\n";

    QSet<QString> processedConfigs;
    QSet<QString> outDirsForCheck;

    for (const VcxprojConfig& config : m_configs.values()) {
        QString configType = config.configType; // "Debug" または "Release"
        if (processedConfigs.contains(configType)) {
            continue;
        }
        processedConfigs.insert(configType);

        //対象の構成（Debug等）に x64 プラットフォームが存在するかチェックする
        bool hasX64 = false;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType && cfg.platform.compare("x64", Qt::CaseInsensitive) == 0) {
                hasX64 = true;
                break;
            }
        }

        // --- Output Directory ---
        QString outDir;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            // 優先的に x64 の出力ディレクトリを取得
            if (cfg.configType == configType && cfg.platform.compare("x64", Qt::CaseInsensitive) == 0 && !cfg.outDir.isEmpty()) {
                outDir = cfg.outDir;
                break;
            }
        }
        if (outDir.isEmpty()) {
            for (const VcxprojConfig& cfg : m_configs.values()) {
                if (cfg.configType == configType && !cfg.outDir.isEmpty()) {
                    outDir = cfg.outDir;
                    break;
                }
            }
        }

        if (!outDir.isEmpty()) {
            QString tempOutDir = outDir;
            tempOutDir.replace("$(ConfigurationName)", configType, Qt::CaseInsensitive);

            QString cmakeOutDir = applyMacroReplacements(tempOutDir, configType).replace("\\", "/");
            cmakeOutDir.replace("$(ProjectDir)", "${CMAKE_CURRENT_SOURCE_DIR}/");
            cmakeOutDir.replace("$(SolutionDir)", "${CMAKE_SOURCE_DIR}/");
            cmakeOutDir.replace("$(Configuration)", configType);
            
            if (cmakeOutDir.endsWith("/")) {
                cmakeOutDir.chop(1);
            }

            if (!cmakeOutDir.startsWith("/") && !cmakeOutDir.contains(":") && !cmakeOutDir.startsWith("${")) {
                cmakeOutDir = "${CMAKE_CURRENT_SOURCE_DIR}/" + cmakeOutDir;
            }

            outDirsForCheck.insert(cmakeOutDir);

            QString upperConfig = configType.toUpper();
            ss << "set_target_properties(${PROJECT_NAME} PROPERTIES\n";
            ss << "    ARCHIVE_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << "    LIBRARY_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << "    RUNTIME_OUTPUT_DIRECTORY_" << upperConfig << " \"" << cmakeOutDir << "\"\n";
            ss << ")\n\n";

            ss << "if(CMAKE_BUILD_TYPE STREQUAL \"" << configType << "\" OR NOT CMAKE_BUILD_TYPE)\n";
            ss << "    set_target_properties(${PROJECT_NAME} PROPERTIES\n";
            ss << "        ARCHIVE_OUTPUT_DIRECTORY \"" << cmakeOutDir << "\"\n";
            ss << "        LIBRARY_OUTPUT_DIRECTORY \"" << cmakeOutDir << "\"\n";
            ss << "        RUNTIME_OUTPUT_DIRECTORY \"" << cmakeOutDir << "\"\n";
            ss << "    )\n";
            ss << "endif()\n\n";
        }

        // --- Include Directories ---
        QSet<QString> includeDirs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                // ★ x64 が存在する場合、Win32 の設定は無視する
                if (hasX64 && cfg.platform.compare("x64", Qt::CaseInsensitive) != 0) continue;
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
            
            QStringList cmakePaths = toCMakePaths(includeList, configType);
            for (const QString& path : cmakePaths) {
                ss << "        \"" << path << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Library Directories ---
        QSet<QString> libDirs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                // ★ x64 が存在する場合、Win32 の設定は無視する
                if (hasX64 && cfg.platform.compare("x64", Qt::CaseInsensitive) != 0) continue;
                for (const QString& path : cfg.libraryDirectories) {
                    if (path != "%(AdditionalLibraryDirectories)") {
                        libDirs.insert(path);
                    }
                }
            }
        }
        
        QStringList libDirList;
        for (const QString& path : libDirs) {
            libDirList.append(path);
        }
        // ここで変数 cmakeLibPaths を定義して、後方でも使えるようにしておく
        QStringList cmakeLibPaths = toCMakePaths(libDirList, configType);
        
        if (!cmakeLibPaths.isEmpty()) {
            ss << "target_link_directories(${PROJECT_NAME} PRIVATE\n";
            ss << "    $<$<CONFIG:" << configType << ">:\n";
            for (const QString& path : cmakeLibPaths) {
                ss << "        \"" << path << "\"\n";
            }
            ss << "    >\n)\n\n";
        }

        // --- Preprocessor Definitions ---
        QSet<QString> definitions;
        QSet<QString> win32Definitions;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                // ★ x64 が存在する場合、Win32 の設定は無視する
                if (hasX64 && cfg.platform.compare("x64", Qt::CaseInsensitive) != 0) continue;
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
        
        // --- Linked Libraries ---
        QSet<QString> libs;
        for (const VcxprojConfig& cfg : m_configs.values()) {
            if (cfg.configType == configType) {
                // 該当構成(DebugやRelease)に x64 の設定が存在する場合、Win32 等は完全に無視する
                if (hasX64 && cfg.platform.compare("x64", Qt::CaseInsensitive) != 0) {
                    continue;
                }

                for (const QString& lib : cfg.additionalDependencies) {
                    if (lib != "%(AdditionalDependencies)") {
                        QString libBaseName = QFileInfo(lib).completeBaseName();
                        if (libBaseName.startsWith("Qt", Qt::CaseInsensitive)) continue;

                        if (libBaseName.compare(m_projectName, Qt::CaseInsensitive) == 0 ||
                            libBaseName.compare("lib" + m_projectName, Qt::CaseInsensitive) == 0) {
                            continue;
                        }
                        QString lowerLib = libBaseName.toLower();
                        if (lowerLib.startsWith("opengl32") || lowerLib.startsWith("glu32") ||
                            lowerLib.startsWith("glfw") || lowerLib.startsWith("glew")) {
                            continue;
                        }

                        bool isExcluded = false;
                        QString libFileName = QFileInfo(lib).fileName(); 
                        for (const QString& excl : m_excludedLibraryFiles) {
                            if (lib.compare(excl, Qt::CaseInsensitive) == 0 || 
                                libFileName.compare(excl, Qt::CaseInsensitive) == 0) {
                                isExcluded = true;
                                break;
                            }
                        }

                        // additionalLibraryFiles に登録されているライブラリは元の依存関係から自動除外 ---
                        if (!isExcluded) {
                            QString projLibBase = libBaseName.toLower();
                            if (projLibBase.startsWith("lib")) projLibBase = projLibBase.mid(3);

                            for (const QString& addLib : m_additionalLibraryFiles) {
                                QString addLibBase = QFileInfo(addLib).completeBaseName().toLower();
                                if (addLibBase.startsWith("lib")) addLibBase = addLibBase.mid(3);
                                
                                // "NetworkDrive" と "libNetworkDrive" を同一とみなして除外する
                                if (projLibBase == addLibBase) {
                                    isExcluded = true;
                                    break;
                                }
                            }
                        }
                        if (isExcluded) continue;
                        libs.insert(lib);
                    }
                }
            }
        }

        if (!libs.isEmpty()) {
            ss << "if(WIN32)\n";
            ss << "    target_link_libraries(${PROJECT_NAME} PRIVATE\n";
            for (const QString& lib : libs) {
                ss << "        $<$<CONFIG:" << configType << ">:\"" << toCMakePath(lib, configType) << "\">\n";
            }
            ss << "    )\n";
            
            ss << "elseif(UNIX)\n";
            
            QStringList unixLibVars;

            // ディレクトリ指定がないライブラリ用のフォールバックパス文字列を作成
            QString fallbackPathsArg = "";
            if (!cmakeLibPaths.isEmpty()) {
                fallbackPathsArg = " PATHS";
                for (const QString& p : cmakeLibPaths) {
                    fallbackPathsArg += " \"" + p + "\"";
                }
            }

            for (const QString& lib : libs) {
                QString unixLib = toCMakePath(lib, configType);
                QFileInfo fi(unixLib);
                QString dir = fi.path();
                if (dir == ".") dir = "";
                else if (!dir.startsWith("/") && !dir.contains(":") && !dir.startsWith("${")) {
                    dir = "${CMAKE_CURRENT_SOURCE_DIR}/" + dir;
                }
                
                QString baseName = fi.completeBaseName();
                QString coreName = baseName;
                if (coreName.startsWith("lib", Qt::CaseInsensitive)) {
                    coreName = coreName.mid(3);
                }
                
                int dotIdx = coreName.indexOf('.');
                if (dotIdx != -1) {
                    coreName = coreName.left(dotIdx);
                }
                
                if (coreName.compare("JPeg", Qt::CaseInsensitive) == 0) {
                    coreName = "jpeg";
                }

                QString safeName = coreName.toUpper();
                safeName.replace(QRegularExpression("[^A-Z0-9]"), "_");
                QString safeConfig = configType.toUpper();
                safeConfig.replace(QRegularExpression("[^A-Z0-9]"), "_");

                QString staticVar = safeName + "_STATIC_LIB_" + safeConfig;
                QString sharedVar = safeName + "_SHARED_LIB_" + safeConfig;
                QString finalVar = safeName + "_FINAL_LIB_" + safeConfig;

                if (coreName == "z" || coreName == "pthread" || coreName == "m" || coreName == "dl" || coreName == "rt") {
                    ss << "    # --- System Library Linking for " << coreName << " (" << configType << ") ---\n";
                    ss << "    set(" << finalVar << " " << coreName << ")\n\n";
                    unixLibVars.append("${" + finalVar + "}");
                    continue;
                }
                QString pathsArg = "";
                if (!dir.isEmpty()) {
                    pathsArg = " PATHS \"" + dir + "\" NO_DEFAULT_PATH";
                } else if (!fallbackPathsArg.isEmpty()) {
                    // ディレクトリが指定されていない場合は追加ディレクトリ一覧から検索させる
                    pathsArg = fallbackPathsArg;
                }

                if (coreName.startsWith("tesseract", Qt::CaseInsensitive)) {
                    ss << "    # --- PkgConfig for Tesseract (" << configType << ") ---\n";
                    ss << "    find_package(PkgConfig REQUIRED)\n";
                    ss << "    pkg_check_modules(TESSERACT_" << safeConfig << " REQUIRED tesseract)\n";
                    ss << "    if(TESSERACT_" << safeConfig << "_FOUND)\n";
                    ss << "        target_link_directories(${PROJECT_NAME} PRIVATE ${TESSERACT_" << safeConfig << "_LIBRARY_DIRS})\n";
                    ss << "    else()\n";
                    ss << "        message(WARNING \"Tesseract not found via pkg-config!\")\n";
                    ss << "    endif()\n\n";
                    unixLibVars.append("${TESSERACT_" + safeConfig + "_LIBRARIES}");
                    continue;
                }

                if (m_modeDynamicLink) {
                    ss << "    # --- Dynamic Library Linking for " << coreName << " (" << configType << ") ---\n";
                    ss << "    # 1. First, search for the shared library (.so)\n";
                    ss << "    find_library(" << sharedVar << " NAMES " << coreName << pathsArg << ")\n";
                    ss << "    if(" << sharedVar << ")\n";
                    ss << "        set(" << finalVar << " ${" << sharedVar << "})\n";
                    ss << "    else()\n";
                    ss << "        # 2. If not found, search for the static library (.a)\n";
                    ss << "        find_library(" << staticVar << " NAMES lib" << coreName << ".a" << pathsArg << ")\n";
                    ss << "        if(" << staticVar << ")\n";
                    ss << "            set(" << finalVar << " ${" << staticVar << "})\n";
                    ss << "        else()\n";
                    ss << "            # Fallback if not found (e.g. system library)\n";
                    ss << "            set(" << finalVar << " " << coreName << ")\n";
                    ss << "        endif()\n";
                    ss << "    endif()\n\n";
                } else {
                    QString searchName;
                    // Windows特有の .lib は無視して Linux向けの .a に変換する
                    if (!fi.suffix().isEmpty() && fi.suffix().compare("lib", Qt::CaseInsensitive) != 0) {
                        searchName = fi.fileName(); // 拡張子がある場合は元のファイル名を使用
                    } else {
                        searchName = "lib" + coreName + ".a"; // ない場合は.aを付与
                    }

                    ss << "    # --- Exact/Static Library Linking for " << coreName << " (" << configType << ") ---\n";
                    ss << "    # 1. Search for the exact library or static library\n";
                    ss << "    find_library(" << staticVar << " NAMES \"" << searchName << "\"" << pathsArg << ")\n";
                    ss << "    if(" << staticVar << ")\n";
                    ss << "        set(" << finalVar << " ${" << staticVar << "})\n";
                    ss << "    else()\n";
                    ss << "        # Fallback if not found (e.g. system library)\n";
                    if (!fi.suffix().isEmpty() && fi.suffix().compare("lib", Qt::CaseInsensitive) != 0) {
                        ss << "        set(" << finalVar << " \"" << fi.fileName() << "\")\n";
                    } else {
                        ss << "        set(" << finalVar << " " << coreName << ")\n";
                    }
                    ss << "    endif()\n\n";
                }

                unixLibVars.append("${" + finalVar + "}");
            }
            
            ss << "    target_link_libraries(${PROJECT_NAME} PRIVATE\n";
            ss << "        $<$<CONFIG:" << configType << ">:-Wl,--start-group>\n";
            for (const QString& var : unixLibVars) {
                ss << "        $<$<CONFIG:" << configType << ">:" << var << ">\n";
            }
            ss << "        $<$<CONFIG:" << configType << ">:-Wl,--end-group>\n";
            ss << "    )\n";
            ss << "endif()\n\n";
        }

        // --- MSVC Options ---
        ss << "if(MSVC)\n";
        appendOptimizationFlags(content, config);
        appendRuntimeLibraryFlags(content, config);
        appendLinkStackHeapFlags(content, config);
        ss << "endif()\n\n";

        ss << "# --- End of " << configType << " ---\n\n";
    }

    // 出力フォルダー内に同名のフォルダーがある場合は実行ファイル名に"App"を追加
    QString targetType = "Application";
    if (m_configs.contains("Release|x64")) {
        targetType = m_configs["Release|x64"].configurationType;
    } else if (!m_configs.isEmpty()) {
        targetType = m_configs.first().configurationType;
    }

    if (targetType == "Application" && !outDirsForCheck.isEmpty()) {
        ss << "# Avoid \"Is a directory\" linker error by appending \"App\" to the output name\n";
        ss << "# if a directory with the same name as the target already exists in the output path.\n";
        for (const QString& dir : outDirsForCheck) {
            ss << "if(EXISTS \"" << dir << "/${PROJECT_NAME}\" AND IS_DIRECTORY \"" << dir << "/${PROJECT_NAME}\")\n";
            ss << "    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME \"${PROJECT_NAME}App\")\n";
            ss << "endif()\n";
        }
        ss << "\n";
    }

    // Qt 6 モジュールへのリンク
    ss << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";
    QStringList comps = getQtComponents();
    for (const QString& comp : comps) {
        ss << "    Qt6::" << comp << "\n";
    }
    ss << ")\n\n";
}

// 追加設定情報をファイルに出力
void VcxprojParser::appendAdditionalSettings(QString& content) const {
    if (m_additionalIncludeDirs.isEmpty() && 
        m_additionalLibraryDirs.isEmpty() && 
        m_additionalOptimizationFlags.isEmpty() && 
        m_additionalLibraryFiles.isEmpty() && 
        m_additionalLinkOptimizations.isEmpty()) {
        return; 
    }

    QTextStream ss(&content);
    ss << "# === Additional Custom Settings ===\n\n";

    if (!m_additionalIncludeDirs.isEmpty()) {
        ss << "target_include_directories(${PROJECT_NAME} PRIVATE\n";
        for (const QString& dir : m_additionalIncludeDirs) {
            ss << "    \"" << toCMakePath(dir) << "\"\n";
        }
        ss << ")\n\n";
    }

    if (!m_additionalLibraryDirs.isEmpty()) {
        ss << "target_link_directories(${PROJECT_NAME} PRIVATE\n";
        for (const QString& dir : m_additionalLibraryDirs) {
            ss << "    \"" << toCMakePath(dir) << "\"\n";
        }
        ss << ")\n\n";
    }

    if (!m_additionalOptimizationFlags.isEmpty()) {
        ss << "target_compile_options(${PROJECT_NAME} PRIVATE\n";
        for (const QString& flag : m_additionalOptimizationFlags) {
            ss << "    " << flag << "\n";
        }
        ss << ")\n\n";
    }
    if (!m_additionalLinkOptimizations.isEmpty()) {
        ss << "target_link_options(${PROJECT_NAME} PRIVATE\n";
        for (const QString& opt : m_additionalLinkOptimizations) {
            QString cleanOpt = opt.trimmed();
            // すでにダブルクォーテーションで囲まれていない場合のみ囲んで出力する
            if (!cleanOpt.startsWith("\"")) {
                ss << "    \"" << cleanOpt << "\"\n";
            } else {
                ss << "    " << cleanOpt << "\n";
            }
        }
        ss << ")\n\n";
    }
    if (!m_additionalLibraryFiles.isEmpty()) {
        // ライブラリのコア名ごとにディレクトリパスをグルーピングする
        QMap<QString, QStringList> libDirGroups;
        QMap<QString, QString> libFileNames; // 拡張子判定のために元のファイル名を保持するマップ

        for (const QString& lib : m_additionalLibraryFiles) {
            if (lib.compare("PNG::PNG", Qt::CaseInsensitive) == 0) {
                continue;
            }
            QString cmakePath = toCMakePath(lib);
            QFileInfo fi(cmakePath);
            QString dir = fi.path();
            
            // パスが含まれていない (単なるファイル名) 場合は空として扱う
            if (dir == ".") {
                dir = "";
            } else if (!dir.startsWith("/") && !dir.contains(":") && !dir.startsWith("${")) {
                // 相対パスの場合は CMAKE_CURRENT_SOURCE_DIR を補完
                dir = "${CMAKE_CURRENT_SOURCE_DIR}/" + dir;
            }

            QString baseName = fi.completeBaseName();
            QString coreName = baseName;
            if (coreName.startsWith("lib", Qt::CaseInsensitive)) {
                coreName = coreName.mid(3);
            }
            // 拡張子が重なっている場合（.so.1 など）を考慮してドット以降をさらに除去
            int dotIdx = coreName.indexOf('.');
            if (dotIdx != -1) {
                coreName = coreName.left(dotIdx);
            }
            if (coreName.compare("JPeg", Qt::CaseInsensitive) == 0) {
                coreName = "jpeg";
            }
            if (!dir.isEmpty() && !libDirGroups[coreName].contains(dir)) {
                libDirGroups[coreName].append(dir);
            } else if (dir.isEmpty() && !libDirGroups[coreName].contains("")) {
                // ディレクトリ指定がない場合も一応ダミーで登録（システムパス検索のため）
                libDirGroups[coreName].append("");
            }
            
            // 元のファイル名を記録 (最初に登場したもの)
            if (!libFileNames.contains(coreName)) {
                libFileNames[coreName] = fi.fileName();
            }
        }

        for (auto it = libDirGroups.begin(); it != libDirGroups.end(); ++it) {
            QString coreName = it.key();

            // 自身（プロジェクト名）と同じ名前のライブラリは追加設定からも除外（自己参照の防止）
            if (coreName.compare(m_projectName, Qt::CaseInsensitive) == 0) {
                continue;
            }

            // ▼▼▼ 追加箇所: システムライブラリの場合は探索をスキップして直接リンク ▼▼▼
            // (z の他に、Linuxで頻出する pthread, m(math), dl, rt も念のため登録しておきます)
            if (coreName == "z" || coreName == "pthread" || coreName == "m" || coreName == "dl" || coreName == "rt") {
                ss << "    # --- System Library Linking for " << coreName << " ---\n";
                ss << "    target_link_libraries(${PROJECT_NAME} PRIVATE " << coreName << ")\n\n";
                continue;
            }
            // ▲▲▲ 追加箇所 ここまで ▲▲▲

            QStringList dirs = it.value();

            QString exactFileName = libFileNames[coreName];
            QFileInfo exactFi(exactFileName);

            // 変数名を生成
            QString varName = coreName.toUpper();
            varName.replace(QRegularExpression("[^A-Z0-9]"), "_");
            QString staticVar = varName + "_STATIC_LIB";
            QString sharedVar = varName + "_SHARED_LIB";

            QString pathsArg = "";
            bool hasValidPath = false;
            for (const QString& d : dirs) {
                if (!d.isEmpty()) {
                    pathsArg += " \"" + d + "\"";
                    hasValidPath = true;
                }
            }

            // 指定されたパスがある場合は、そこだけを優先的に探すようにする
            if (hasValidPath) {
                pathsArg = " PATHS" + pathsArg + " NO_DEFAULT_PATH";
            } else if (!m_additionalLibraryDirs.isEmpty()) {
                // パス指定がない場合、Additional Library Directories を検索パスとして構築する ▼▼▼
                pathsArg = " PATHS";
                for (const QString& dir : m_additionalLibraryDirs) {
                    QString cmakeDir = toCMakePath(dir);
                    // 相対パスの場合は ${CMAKE_CURRENT_SOURCE_DIR} を補完して絶対パス化する
                    if (!cmakeDir.startsWith("/") && !cmakeDir.contains(":") && !cmakeDir.startsWith("$")) {
                        cmakeDir = "${CMAKE_CURRENT_SOURCE_DIR}/" + cmakeDir;
                    }
                    pathsArg += " \"" + cmakeDir + "\"";
                }
                // ※システム標準ライブラリ（/usr/lib等）へのフォールバックも残すため、
                // ここでは NO_DEFAULT_PATH をあえて付与しません。
            }

            if (coreName.startsWith("tesseract", Qt::CaseInsensitive)) {
                ss << "    # --- PkgConfig for Tesseract ---\n";
                ss << "    find_package(PkgConfig REQUIRED)\n";
                ss << "    pkg_check_modules(TESSERACT REQUIRED tesseract)\n";
                ss << "    if(TESSERACT_FOUND)\n";
                ss << "        target_link_directories(${PROJECT_NAME} PRIVATE ${TESSERACT_LIBRARY_DIRS})\n";
                ss << "        target_link_libraries(${PROJECT_NAME} PRIVATE ${TESSERACT_LIBRARIES})\n";
                ss << "    else()\n";
                ss << "        message(WARNING \"Tesseract not found via pkg-config!\")\n";
                ss << "    endif()\n\n";
                continue;
            }

            if (m_modeDynamicLink) {
                ss << "    # --- Dynamic Library Linking for " << coreName << " ---\n";
                ss << "    # 1. First, search for the shared library (.so)\n";
                ss << "    find_library(" << sharedVar << " NAMES " << coreName << pathsArg << ")\n\n";
                
                ss << "    if(" << sharedVar << ")\n";
                ss << "        message(STATUS \"Found Shared Library: ${" << sharedVar << "}\")\n";
                ss << "        target_link_libraries(${PROJECT_NAME} PRIVATE ${" << sharedVar << "})\n";
                ss << "    else()\n";
                ss << "        # 2. If not found, search for the static library (.a)\n";
                ss << "        find_library(" << staticVar << " NAMES lib" << coreName << ".a" << pathsArg << ")\n";
                ss << "        if(" << staticVar << ")\n";
                ss << "            message(STATUS \"Found Static Library: ${" << staticVar << "}\")\n";
                ss << "            target_link_libraries(${PROJECT_NAME} PRIVATE ${" << staticVar << "})\n";
                ss << "        else()\n";
                ss << "            message(WARNING \"Library " << coreName << " not found!\")\n";
                ss << "        endif()\n";
                ss << "    endif()\n\n";
            } else {
                // 拡張子が含まれているか判定して検索ファイル名を決定
                QString searchName;
                if (!exactFi.suffix().isEmpty() && exactFi.suffix().compare("lib", Qt::CaseInsensitive) != 0) {
                    searchName = exactFileName;
                } else {
                    searchName = "lib" + coreName + ".a";
                }

                ss << "    # --- Exact/Static Library Linking for " << coreName << " ---\n";
                ss << "    find_library(" << staticVar << " NAMES \"" << searchName << "\"" << pathsArg << ")\n\n";
                
                ss << "    if(" << staticVar << ")\n";
                ss << "        message(STATUS \"Found Exact/Static Library: ${" << staticVar << "}\")\n";
                ss << "        target_link_libraries(${PROJECT_NAME} PRIVATE ${" << staticVar << "})\n";
                ss << "    else()\n";
                ss << "        message(WARNING \"Library " << searchName << " not found! Fallback to default linking.\")\n";
                if (!exactFi.suffix().isEmpty() && exactFi.suffix().compare("lib", Qt::CaseInsensitive) != 0) {
                    ss << "        target_link_libraries(${PROJECT_NAME} PRIVATE \"" << exactFileName << "\")\n";
                } else {
                    ss << "        target_link_libraries(${PROJECT_NAME} PRIVATE " << coreName << ")\n";
                }
                ss << "    endif()\n\n";
            }
        }
    }
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

void VcxprojParser::appendLinkStackHeapFlags(QString& content, const VcxprojConfig& config) const {
    QTextStream ss(&content);
    QStringList linkFlags;

    // スタックサイズ (/STACK:reserve[,commit])
    if (!config.stackReserveSize.isEmpty() || !config.stackCommitSize.isEmpty()) {
        QString flag = "/STACK:";
        // Commitのみ指定されてReserveが空の場合は、エラー回避のため 0 を指定（OSのデフォルトに委ねる）
        flag += config.stackReserveSize.isEmpty() ? "0" : config.stackReserveSize;
        if (!config.stackCommitSize.isEmpty()) {
            flag += "," + config.stackCommitSize;
        }
        linkFlags.append(flag);
    }

    // ヒープサイズ (/HEAP:reserve[,commit])
    if (!config.heapReserveSize.isEmpty() || !config.heapCommitSize.isEmpty()) {
        QString flag = "/HEAP:";
        // Commitのみ指定されてReserveが空の場合は、エラー回避のため 0 を指定
        flag += config.heapReserveSize.isEmpty() ? "0" : config.heapReserveSize;
        if (!config.heapCommitSize.isEmpty()) {
            flag += "," + config.heapCommitSize;
        }
        linkFlags.append(flag);
    }

    if (!linkFlags.isEmpty()) {
        ss << "    target_link_options(${PROJECT_NAME} PRIVATE\n";
        for (const QString& flag : linkFlags) {
            ss << "        $<$<CONFIG:" << config.configType << ">:" << flag << ">\n";
        }
        ss << "    )\n";
    }
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


bool ProjectConverter::CreateCMakeFile(const QString &ProjFileName,QStringList &RetUnresolvedMacros)
{
    QString vcxprojPath = ProjFileName;
    QFileInfo vcxprojInfo(vcxprojPath);
    if (!vcxprojInfo.exists()) {
         return false; 
    }

    QString outputPath = vcxprojInfo.absolutePath() + "/CMakeLists.txt";

    VcxprojParser vcxprojParser(vcxprojPath);
    
    vcxprojParser.setMacroReplacements(this->macroReplacements);
    vcxprojParser.setAdditionalIncludeDirs(this->additionalIncludeDirs);
    vcxprojParser.setAdditionalLibraryDirs(this->additionalLibraryDirs);
    vcxprojParser.setAdditionalOptimizationFlags(this->additionalOptimizationFlags);
    vcxprojParser.setAdditionalLinkOptimizations(this->additionalLinkOptimizations);

    vcxprojParser.setExcludedLibraryFiles(this->excludedLibraryFiles);
    vcxprojParser.setAdditionalLibraryFiles(this->additionalLibraryFiles);
    vcxprojParser.setModeDynamicLink(this->ModeDynamicLink);
    vcxprojParser.setForceOpenMP    (this->ForceOpenMP);

    if (!vcxprojParser.parse()) {
        return false; 
    }

    QString cmakeContent = vcxprojParser.generateCMakeLists();

    QStringList unresolved = vcxprojParser.getUnresolvedMacros();
	RetUnresolvedMacros = unresolved; 

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false; 
    }

    QTextStream outStream(&outFile);
    outStream.setEncoding(QStringConverter::Utf8);
    outStream << cmakeContent;
    outFile.close();

    return true;
}

bool ProjectConverter::CreateRootCMakeFile(const QString &SLNFileName)
{
    QFile file(SLNFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QFileInfo slnInfo(SLNFileName);
    QString slnPath = slnInfo.absolutePath();
    QString slnName = slnInfo.baseName();

    QTextStream in(&file);
    QRegularExpression re("Project\\(\"[^\"]*\"\\)\\s*=\\s*\"[^\"]*\"\\s*,\\s*\"([^\"]*\\.vcxproj)\"");

    QStringList staticProjectDirs;
    QStringList otherProjectDirs;

    // 静的ライブラリ判定用の正規表現
    QRegularExpression reStatic("<ConfigurationType>\\s*StaticLibrary\\s*</ConfigurationType>", QRegularExpression::CaseInsensitiveOption);

    while (!in.atEnd()) {
        QString line = in.readLine();
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            QString vcxprojRelPath = match.captured(1);
            vcxprojRelPath.replace("\\", "/");
            
            QString dirPath = QFileInfo(vcxprojRelPath).path();
            if (dirPath == ".") {
                dirPath = "";
            }

            QString fullVcxprojPath = slnPath + "/" + vcxprojRelPath;
            bool isStatic = false;

            // vcxproj ファイルの中身を確認して静的ライブラリか判定
            QFile vcxprojFile(fullVcxprojPath);
            if (vcxprojFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream vcxprojIn(&vcxprojFile);
                vcxprojIn.setAutoDetectUnicode(true);
                QString content = vcxprojIn.readAll();
                if (content.contains(reStatic)) {
                    isStatic = true;
                }
                vcxprojFile.close();
            }

            // 静的ライブラリとそれ以外でリストを分ける
            if (isStatic) {
                if (!staticProjectDirs.contains(dirPath)) {
                    staticProjectDirs.append(dirPath);
                }
            } else {
                if (!otherProjectDirs.contains(dirPath)) {
                    otherProjectDirs.append(dirPath);
                }
            }
        }
    }
    file.close();

    // 静的ライブラリのディレクトリを先に、その後にそれ以外を結合する
    QStringList projectDirs;
    projectDirs << staticProjectDirs << otherProjectDirs;
    projectDirs.removeDuplicates();

    QString outPath = slnPath + "/CMakeLists.txt";
    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);

    out << "# CMakeLists.txt generated from " << slnInfo.fileName() << "\n";
    out << "cmake_minimum_required(VERSION 3.16)\n\n";
    
    out << "project(" << slnName << " LANGUAGES CXX)\n\n";

    out << "# === Global Build Settings ===\n";
    out << "set(CMAKE_CXX_STANDARD 17)\n";
    out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    out << "set(CMAKE_AUTOMOC ON)\n";
    out << "set(CMAKE_AUTORCC ON)\n";
    out << "set(CMAKE_AUTOUIC ON)\n\n";

    out << "if(WIN32)\n";
    out << "    add_compile_definitions(WIN32 _WINDOWS)\n";
    out << "endif()\n\n";

    out << "if(UNIX)\n";
    out << "    # Dead code stripping (remove unused functions/data)\n";
    out << "    add_compile_options(-ffunction-sections -fdata-sections)\n";
    out << "    add_link_options(\"-Wl,--gc-sections\")\n";
    out << "    # Allow multiple definitions to mimic MSVC static library linking behavior\n";
    out << "    add_link_options(\"-Wl,--allow-multiple-definition\")\n";
    out << "endif()\n\n";

    out << "# === Independent Parallel Builds (Meta-Build) ===\n";
    out << "# デフォルトのビルドタイプを Release に設定 (指定されていない場合)\n";
    out << "if(NOT CMAKE_BUILD_TYPE)\n";
    out << "    set(CMAKE_BUILD_TYPE \"Release\")\n";
    out << "endif()\n\n";

    QString prevTarget; 

    for (const QString& dir : projectDirs) {
        if (!dir.isEmpty()) {
            QString safeTargetName = dir;
            safeTargetName.replace(QRegularExpression("[^a-zA-Z0-9]"), "_");
            
            while(safeTargetName.startsWith("_")) {
                safeTargetName.remove(0, 1);
            }
            if(safeTargetName.isEmpty()) safeTargetName = "Target";

            QString currentTarget = safeTargetName + "_build";

            out << "add_custom_target(" << currentTarget << " ALL\n";
            
            out << "    COMMAND ${CMAKE_COMMAND} -S \"${CMAKE_CURRENT_SOURCE_DIR}/" << dir 
                << "\" -B \"${CMAKE_CURRENT_SOURCE_DIR}/" << dir << "/build\" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n";
            
            QString buildCommand = QString("    COMMAND ${CMAKE_COMMAND} --build \"${CMAKE_CURRENT_SOURCE_DIR}/%1/build\" --config ${CMAKE_BUILD_TYPE}").arg(dir);
            if (m_enableParallelBuild) {
                if (m_maxParallelBuilds > 0) {
                    buildCommand += QString(" --parallel %1").arg(m_maxParallelBuilds);
                } else {
                    buildCommand += " --parallel";
                }
            }
            buildCommand += " -- -k\n";
            out << buildCommand;
            
            out << "    WORKING_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\"\n";
            
            if (m_enableParallelBuild) {
                out << "    COMMENT \"[Meta-Build] Configuring and Building " << dir << " in parallel...\"\n";
            } else {
                out << "    COMMENT \"[Meta-Build] Configuring and Building " << dir << " sequentially...\"\n";
            }
            out << ")\n";

            if (!prevTarget.isEmpty()) {
                if (!m_enableParallelBuild) {
                    out << "add_dependencies(" << currentTarget << " " << prevTarget << ")\n";
                } else {
                    out << "# add_dependencies(" << currentTarget << " " << prevTarget << ") # Uncomment if strict build order is required\n";
                }
            }
            out << "\n";

            prevTarget = currentTarget;

        } else {
            out << "# WARNING: A project is located in the same directory as the SLN file.\n";
            out << "# Independent build folder creation skipped for the root directory.\n";
        }
    }

    outFile.close();
    return true;
}
