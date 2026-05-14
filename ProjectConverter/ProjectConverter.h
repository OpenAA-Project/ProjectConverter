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
#include <QListWidgetItem>
#include "NListComp.h"

// 1: マクロ置換用の構造体
class MatchingDim : public NPListSaveLoad<MatchingDim>
{
public:
    QString MStr;
    QString RStr;
    QString FileName;

	MatchingDim(void) {}
    MatchingDim(const MatchingDim &src) {
        MStr = src.MStr;
        RStr = src.RStr;
        FileName=src.FileName;
	}
	virtual bool    Save(QIODevice *f) override;
    virtual bool    Load(QIODevice *f) override;
    virtual MatchingDim &operator=(MatchingDim &src) override {
        MStr = src.MStr;
        RStr = src.RStr;
        FileName=src.FileName;
        return *this;
	}
};
class MatchingDimPack : public NPListPackSaveLoad<MatchingDim>
{
public:
	virtual MatchingDim *Create(void) override { return new MatchingDim(); }

    MatchingDimPack &operator=(const MatchingDimPack &src)
    {
        if (this != &src) {
            this->RemoveAll();
            for (MatchingDim* item=src.GetFirst();item!=NULL;item=item->GetNext()) {
                MatchingDim* newItem = new MatchingDim(*item);
                this->AppendList(newItem);
            }
        }
        return *this;
	}

    void    Merge(MatchingDim *m)
    {
        for (MatchingDim* item=GetFirst();item!=NULL;item=item->GetNext()) {
            if (item->MStr == m->MStr) {
                if(item->RStr.isEmpty()==true){
                    item->RStr = m->RStr; // 置換文字列を更新
                }
                return;
            }
        }
        // 見つからなかった場合は新規追加
        MatchingDim* newItem = new MatchingDim(*m);
        this->AppendList(newItem);
	}
};

QT_BEGIN_NAMESPACE
namespace Ui {
class ProjectConverter;
}
QT_END_NAMESPACE

class ProjectConverter : public QMainWindow
{
    Q_OBJECT

    QStringList ProjectFileName;
    QStringList SLNFiles;
	QString     FileNameToSettings; // 設定の保存/読み込みに使用するファイル名
    mutable QSet<QString> m_unresolvedMacros;

    bool m_enableParallelBuild = true;       // 並列ビルドを有効にするか
    int  m_maxParallelBuilds = 0;            // 最大並列数 (0ならCMakeのデフォルト/無制限)

public:
    ProjectConverter(const QString &SettingFileName ,QWidget *parent = nullptr);
    ~ProjectConverter();

    // 追加された機能の設定変数群
    MatchingDimPack macroReplacements;          // 1: $()の置換リスト
    QStringList additionalIncludeDirs;          // 2: 追加のincludeパス
    QStringList additionalLibraryDirs;          // 3: 追加のlibraryパス
    QStringList additionalOptimizationFlags;    // 4: 追加の最適化フラグ
    
    // 除外するライブラリファイル名のリスト
    QStringList excludedLibraryFiles;
    QStringList additionalLibraryFiles;

private slots:
    void on_pushButtonFileName_clicked();
    void on_pushButtonConvert_clicked();
    void on_tableWidgetMacro_cellDoubleClicked(int row, int column);
    void on_pushButtonAddMacro_clicked();
    void on_pushButtonDelMacro_clicked();
    void on_listWidgetInclude_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonAddInclude_clicked();
    void on_pushButtonDelInclude_clicked();
    void on_listWidgetLibrary_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonAddLibrary_clicked();
    void on_pushButtonDelLibrary_clicked();
    void on_listWidgetOptimaze_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonAddOptimaze_clicked();
    void on_pushButtonDelOptimaze_clicked();
    void on_pushButtonSaveNew_clicked();
    void on_pushButtonOverwrite_clicked();
    void on_pushButtonLoad_clicked();
    void on_listWidgetExcludedLibraryFiles_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonAddExcludedLibraryFile_clicked();
    void on_pushButtonDelExcludedLibraryFile_clicked();
    void on_listWidgetAdditionalLibraryFiles_itemDoubleClicked(QListWidgetItem *item);
    void on_pushButtonAddAdditionalLibraryFile_clicked();
    void on_pushButtonDelAdditionalLibraryFile_clicked();

private:
    Ui::ProjectConverter *ui;

    bool    LoadSLN(const QString &SLNFileName,QStringList &ProjList);
    bool    CreateCMakeFile(const QString &ProjFileName ,QStringList &RetUnresolvedMacros);

    bool    CreateRootCMakeFile(const QString &SLNFileName);

	void	ShowMacro(void);
	void    ShowInclude(void);
	void    ShowLibrary(void);
	void    ShowOptimaze(void);
    void    ShowExcludedLibraryFiles(void);
    void    ShowAdditionalLibraryFiles(void);

public:
	bool    SaveSettings(const QString &FileName);
	bool    LoadSettings(const QString &FileName);

    bool    MakeCMakeFile(const QString &ProjectSlnFileName);
};


// .vcxproj の構成ごとの設定を保持する構造体
struct VcxprojConfig {
    QString configName; // "Debug|x64" など
    QString platform;   // "x64"
    QString configType; // "Debug"

    // ターゲットタイプ
    QString configurationType; // Application, DynamicLibrary, StaticLibrary

    // 出力ディレクトリ
    QString outDir;

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

    bool useOpenMP = false;
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

    // --- 追加機能のセッター ---
    void setMacroReplacements(const MatchingDimPack& macros) { m_macroReplacements = macros; }
    void setAdditionalIncludeDirs(const QStringList& dirs);
    void setAdditionalLibraryDirs(const QStringList& dirs);
    void setAdditionalOptimizationFlags(const QStringList& flags) { m_additionalOptimizationFlags = flags; }

    // 除外ライブラリのセッター
    void setExcludedLibraryFiles(const QStringList& libs) { m_excludedLibraryFiles = libs; }
    void setAdditionalLibraryFiles(const QStringList& libs) { m_additionalLibraryFiles = libs; }

    // 未解決マクロを取得するゲッター
    QStringList getUnresolvedMacros() const { 
        QStringList list = m_unresolvedMacros.values();
        list.sort();
        return list; 
    }
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
    
    // Qt Settings のモジュール保持用
    QSet<QString> m_qtModules;

    // --- 追加設定保持用変数 ---
    MatchingDimPack m_macroReplacements;
    QStringList m_additionalIncludeDirs;
    QStringList m_additionalLibraryDirs;
    QStringList m_additionalOptimizationFlags;
    QStringList m_excludedLibraryFiles;
    QStringList m_additionalLibraryFiles;

    // 置換できなかったマクロを記録するセット (const メソッドから変更できるよう mutable を指定)
    mutable QSet<QString> m_unresolvedMacros;

    // --- 解析ヘルパー ---

    // <ItemGroup> からファイルリストを抽出
    void parseItemGroups(const QDomElement& root);

    // <PropertyGroup Label="Configuration"> から構成を抽出
    void parseProjectConfigurations(const QDomElement& root);

    // <PropertyGroup> (グローバル設定) を解析
    void parseGlobalProperties(const QDomElement& root);

    // <PropertyGroup> (構成ごと) を解析
    void parseConfigProperties(const QDomElement& root);

    // <PropertyGroup Label="QtSettings"> を解析
    void parseQtModules(const QDomElement& root);

    // <ItemDefinitionGroup> (構成ごと) を解析
    void parseItemDefinitions(const QDomElement& root);

    // 複数の <...></...> タグから ; 区切りのリストを取得
    QStringList getSemicolonList(const QDomElement& parent, const QString& tagName);

    // Condition 属性を解析 (例: "'$(Configuration)|$(Platform)'=='Debug|x64'")
    QString extractCondition(const QDomElement& element) const;

    // パスを CMake 形式 (スラッシュ区切り) に変換
    QString toCMakePath(const QString& path, const QString& configType = QString()) const;
    QStringList toCMakePaths(const QStringList& paths, const QString& configType = QString()) const;
    QString applyMacroReplacements(const QString& str, const QString& configType = QString()) const;

    // マクロ置換処理
    QString applyMacroReplacements(const QString& str) const;

    // Qtモジュールの文字列フォーマット処理
    QStringList getQtComponents() const;

    // --- CMake 生成ヘルパー ---
    void appendHeader(QString& content) const;
    void appendProject(QString& content) const;
    void appendQtSetup(QString& content) const;
    void appendFileLists(QString& content) const;
    void appendTarget(QString& content) const;
    void appendTargetProperties(QString& content) const;
    void appendOptimizationFlags(QString& content, const VcxprojConfig& config) const;
    void appendRuntimeLibraryFlags(QString& content, const VcxprojConfig& config) const;
    
    // 追加設定を出力
    void appendAdditionalSettings(QString& content) const;
};




#endif // PROJECTCONVERTER_H
