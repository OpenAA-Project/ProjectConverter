/********************************************************************************
** Form generated from reading UI file 'ProjectConverterCMake.ui'
**
** Created by: Qt User Interface Compiler version 6.10.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROJECTCONVERTERCMAKE_H
#define UI_PROJECTCONVERTERCMAKE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ProjectConverterCMake
{
public:
    QWidget *centralwidget;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *ProjectConverterCMake)
    {
        if (ProjectConverterCMake->objectName().isEmpty())
            ProjectConverterCMake->setObjectName("ProjectConverterCMake");
        ProjectConverterCMake->resize(800, 600);
        centralwidget = new QWidget(ProjectConverterCMake);
        centralwidget->setObjectName("centralwidget");
        ProjectConverterCMake->setCentralWidget(centralwidget);
        menubar = new QMenuBar(ProjectConverterCMake);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 800, 18));
        ProjectConverterCMake->setMenuBar(menubar);
        statusbar = new QStatusBar(ProjectConverterCMake);
        statusbar->setObjectName("statusbar");
        ProjectConverterCMake->setStatusBar(statusbar);

        retranslateUi(ProjectConverterCMake);

        QMetaObject::connectSlotsByName(ProjectConverterCMake);
    } // setupUi

    void retranslateUi(QMainWindow *ProjectConverterCMake)
    {
        ProjectConverterCMake->setWindowTitle(QCoreApplication::translate("ProjectConverterCMake", "ProjectConverterCMake", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ProjectConverterCMake: public Ui_ProjectConverterCMake {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROJECTCONVERTERCMAKE_H
