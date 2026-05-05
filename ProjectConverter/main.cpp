#include "ProjectConverter.h"
#include <QApplication>
#include "XOpenAA.h"

int main(int argc, char *argv[])
{
    QString	UserPath;
	bool	StopForDebug=false;

	for(int i=1;i<argc;i++){
		if((*argv[i]=='Q' || *argv[i]=='q') && *(argv[i]+1)!=':'){
			char	*fp=argv[i]+1;
			UserPath	=fp;
		}
		else if(strnicmp(argv[i],"StopForDebug",12)==0){
			StopForDebug=true;
		}
	}
    QString	Path=::GetUserPath(UserPath);
	::ForceDirectories(Path);
	QString	SettingFileName = Path+QDir::separator()+QStringLiteral("ProjectConverterSettings.prjc");

    QApplication a(argc, argv);
    ProjectConverter w(SettingFileName);
    w.show();
    return a.exec();
}
