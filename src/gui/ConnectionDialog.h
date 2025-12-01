#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
// Asegúrate de que esta ruta sea accesible. Con el CMakeLists que te di, 
// "hal/IJTAGAdapter.h" debería funcionar si 'src' está en include_directories.
#include "hal/IJTAGAdapter.h" 

class ConnectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget* parent = nullptr);
    ~ConnectionDialog() override;

    /**
     * @brief Get the selected adapter type
     */
     // CORRECCIÓN: Añadido namespace JTAG::
    JTAG::AdapterType getSelectedAdapter() const;

private slots:
    void onAdapterChanged(int index);
    void onConnectClicked();
    void onCancelClicked();

private:
    void setupUI();
    void updateDescription();

    QComboBox* m_adapterCombo;
    QLabel* m_descriptionLabel;
    QPushButton* m_btnConnect;
    QPushButton* m_btnCancel;

    // CORRECCIÓN: Añadido namespace JTAG::
    JTAG::AdapterType m_selectedAdapter;
};

#endif // CONNECTIONDIALOG_H