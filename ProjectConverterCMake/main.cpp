#include "ProjectConverterCMake.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ProjectConverterCMake w;
    w.show();
    return a.exec();
}
