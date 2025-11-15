#ifndef PROJECTCONVERTER_H
#define PROJECTCONVERTER_H

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QDomElement>
#include <QVariant>
#include <QDomDocument>

QT_BEGIN_NAMESPACE
namespace Ui {
class ProjectConverter;
}
QT_END_NAMESPACE

class ProjectConverter : public QMainWindow
{
    Q_OBJECT

    QStringList ProjectFileName;
public:
    ProjectConverter(QWidget *parent = nullptr);
    ~ProjectConverter();

private slots:
    void on_pushButtonFileName_clicked();
    void on_pushButtonConvert_clicked();

private:
    Ui::ProjectConverter *ui;

    bool    LoadSLN(const QString SLNFileName,QStringList &ProjList);
    bool    CreateCMakeFile(const QString &ProjFileName);
};


// .vcxproj の構成ごとの設定を保持する構造体
struct VcxprojConfig {
    QString configName; // "Debug|x64" など
    QString platform;   // "x64"
    QString configType; // "Debug"

    // ターゲットタイプ
    QString configurationType; // Application, DynamicLibrary, StaticLibrary

    // ファイルリスト (この構成に固有の場合)
    // Note: 通常ファイルはグローバルだが、条件付きで含まれる場合もある
    // ここでは簡略化のため、ファイルリストはグローバルに持つ

    // コンパイラ設定
    QStringList includeDirectories;
    QString optimization;
    QString runtimeLibrary;
    QStringList preprocessorDefinitions;

    // リンカ設定
    QStringList libraryDirectories;
    QStringList additionalDependencies;
};

class VcxprojParser {
public:
    VcxprojParser(const QString& vcxprojPath);

    // .vcxproj ファイルの解析を実行
    bool parse();

    // 解析結果から CMakeLists.txt を生成
    QString generateCMakeLists() const;

    // パースエラーがある場合にメッセージを返す
    QString errorString() const { return m_errorString; }

private:
    // .vcxproj ファイルのパス
    QString m_vcxprojPath;
    QString m_projectDir; // .vcxproj があるディレクトリ

    // XML DOMドキュメント
    QDomDocument m_doc;
    QString m_errorString;

    // --- 解析データ ---
    QString m_projectName;

    // グローバルなファイルリスト
    QStringList m_sources;
    QStringList m_headers;
    QStringList m_uiFiles;
    QStringList m_qrcFiles;
    QStringList m_resourceFiles; // .ico, .png など

    // 構成マップ (Key: "Debug|x64" など)
    QMap<QString, VcxprojConfig> m_configs;
    // 見つかったプラットフォーム (例: "x64", "Win32")
    QSet<QString> m_platforms;

    // --- 解析ヘルパー ---

    // <ItemGroup> からファイルリストを抽出
    void parseItemGroups(const QDomElement& root);

    // <PropertyGroup Label="Configuration"> から構成を抽出
    void parseProjectConfigurations(const QDomElement& root);

    // <PropertyGroup> (グローバル設定) を解析
    void parseGlobalProperties(const QDomElement& root);

    // <PropertyGroup> (構成ごと) を解析
    void parseConfigProperties(const QDomElement& root);

    // <ItemDefinitionGroup> (構成ごと) を解析
    void parseItemDefinitions(const QDomElement& root);

    // 複数の <...></...> タグから ; 区切りのリストを取得
    QStringList getSemicolonList(const QDomElement& parent, const QString& tagName);

    // Condition 属性を解析 (例: "'$(Configuration)|$(Platform)'=='Debug|x64'")
    QString extractCondition(const QDomElement& element) const;

    // パスを CMake 形式 (スラッシュ区切り) に変換
    QString toCMakePath(const QString& path) const;
    QStringList toCMakePaths(const QStringList& paths) const;

    // --- CMake 生成ヘルパー ---
    void appendHeader(QString& content) const;
    void appendProject(QString& content) const;
    void appendQtSetup(QString& content) const;
    void appendFileLists(QString& content) const;
    void appendTarget(QString& content) const;
    void appendTargetProperties(QString& content) const;
    void appendOptimizationFlags(QString& content, const VcxprojConfig& config) const;
    void appendRuntimeLibraryFlags(QString& content, const VcxprojConfig& config) const;
};

#endif // PROJECTCONVERTER_H
