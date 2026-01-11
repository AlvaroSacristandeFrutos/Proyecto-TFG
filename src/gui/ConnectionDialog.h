#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <vector>
// Aseg�rate de que esta ruta sea accesible. Con el CMakeLists que te di, 
// "hal/IJTAGAdapter.h" deber�a funcionar si 'src' est� en include_directories.
#include "../hal/IJTAGAdapter.h" 

class ConnectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionDialog(const std::vector<JTAG::AdapterDescriptor>& adapters,
                              QWidget* parent = nullptr);
    ~ConnectionDialog() override;

    /**
     * @brief Get the selected adapter type
     */
     // CORRECCI�N: A�adido namespace JTAG::
    JTAG::AdapterType getSelectedAdapter() const;
    uint32_t getSelectedClockSpeed() const;

    /**
     * @brief Get the selected adapter descriptor (includes deviceID)
     */
    JTAG::AdapterDescriptor getSelectedDescriptor() const;

private slots:
    void onAdapterChanged(int index);
    void onConnectClicked();
    void onCancelClicked();

private:
    void setupUI(const std::vector<JTAG::AdapterDescriptor>& adapters);
    void updateDescription();

    QComboBox* m_adapterCombo;
    QComboBox* m_clockSpeedCombo;
    QLabel* m_descriptionLabel;
    QLabel* m_clockLabel;
    QPushButton* m_btnConnect;
    QPushButton* m_btnCancel;

    // CORRECCI�N: A�adido namespace JTAG::
    JTAG::AdapterType m_selectedAdapter;
    JTAG::AdapterDescriptor m_selectedDescriptor;
};

#endif // CONNECTIONDIALOG_H