#ifndef PROJECTCONVERTERCMAKE_H
#define PROJECTCONVERTERCMAKE_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class ProjectConverterCMake;
}
QT_END_NAMESPACE

class ProjectConverterCMake : public QMainWindow
{
    Q_OBJECT

public:
    ProjectConverterCMake(QWidget *parent = nullptr);
    ~ProjectConverterCMake();

private:
    Ui::ProjectConverterCMake *ui;
};
#endif // PROJECTCONVERTERCMAKE_H
