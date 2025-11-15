#include "ProjectConverterCMake.h"
#include "./ui_ProjectConverterCMake.h"

ProjectConverterCMake::ProjectConverterCMake(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ProjectConverterCMake)
{
    ui->setupUi(this);
}

ProjectConverterCMake::~ProjectConverterCMake()
{
    delete ui;
}
