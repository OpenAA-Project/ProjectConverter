#include "ProjectConverter.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ProjectConverter w;
    w.show();
    return a.exec();
}
