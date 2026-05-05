#ifndef EDITMACRODIALOG_H
#define EDITMACRODIALOG_H

#include <QDialog>

namespace Ui {
class EditMacroDialog;
}

class MatchingDim;

class EditMacroDialog : public QDialog
{
    Q_OBJECT

    MatchingDim *M;
public:
    explicit EditMacroDialog(MatchingDim *m ,QWidget *parent = nullptr);
    ~EditMacroDialog();

    QString MStr;
    QString RStr;

private slots:
    void on_pushButtonOK_clicked();
    void on_pushButtonCancel_clicked();

private:
    Ui::EditMacroDialog *ui;
};

#endif // EDITMACRODIALOG_H
