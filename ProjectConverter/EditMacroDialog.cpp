#include "EditMacroDialog.h"
#include "ui_EditMacroDialog.h"
#include "ProjectConverter.h"

EditMacroDialog::EditMacroDialog(MatchingDim *m ,QWidget *parent)
    : QDialog(parent)
	,M(m)
    , ui(new Ui::EditMacroDialog)
{
    ui->setupUi(this);

    if(M!=NULL){
        ui->lineEditKeyword->setText(M->MStr);
        ui->lineEditReplace->setText(M->RStr);
	}
}

EditMacroDialog::~EditMacroDialog()
{
    delete ui;
}

void EditMacroDialog::on_pushButtonOK_clicked()
{
    MStr = ui->lineEditKeyword->text();
    RStr = ui->lineEditReplace->text();
    accept();
}


void EditMacroDialog::on_pushButtonCancel_clicked()
{
	reject();
}

