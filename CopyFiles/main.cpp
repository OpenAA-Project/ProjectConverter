#include "CopyFiles.h"
#include <QtWidgets/QApplication>
#include "XOpenAA.h"

QString ForceDirectories( const QString &path )
{
	const	char delim = '/';
	QStringList dirs = (path).split(QRegularExpression("[/\\\\]"), Qt::SkipEmptyParts);
	if(dirs.count()==0)
		return path;
	if(path.left(2)=="//" || path.left(2)=="\\\\"){
		QString	sDir=QString("//")+dirs[0];
		for(int i=1; i<dirs.count(); i++){
			QDir dir(sDir);
			sDir+=delim;
			sDir+=dirs[i];
			dir.mkdir(dirs[i]);
		}
		return sDir;
	}
	else{
		//QDir dir(dirs[0] + delim);
		QDir dir;
		for(int i=0; i<dirs.count(); i++){
			if(!dir.exists(dirs[i])){
				if(!dir.mkdir(dirs[i])){
					return dir.path();
				}
			}
			if(!dir.cd(dirs[i])){
				return dir.path();
			}
		}
		return dir.path();
	}
}

int main(int argc, char *argv[])
{
    QString	UserPath;
	bool	StopForDebug=false;

	for(int i=1;i<argc;i++){
		if((*argv[i]=='Q' || *argv[i]=='q') && *(argv[i]+1)!=':'){
			char	*fp=argv[i]+1;
			UserPath	=fp;
		}
		else if(QString(argv[i]).startsWith("StopForDebug", Qt::CaseInsensitive)){
			StopForDebug=true;
		}
	}
    QString	Path=::GetUserPath(UserPath);
	::ForceDirectories(Path);
	QString	SettingFileName = Path+QDir::separator()+QStringLiteral("CopyFile.dat");

    QApplication app(argc, argv);
    CopyFiles window(SettingFileName);
    window.show();
    return app.exec();
}
