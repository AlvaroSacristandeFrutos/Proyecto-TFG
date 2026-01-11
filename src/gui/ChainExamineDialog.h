#ifndef CHAINEXAMINEDIALOG_H
#define CHAINEXAMINEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cstdint>

class ChainExamineDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChainExamineDialog(uint32_t idcode, QWidget* parent = nullptr);
    ~ChainExamineDialog() override = default;

private:
    void setupUI(uint32_t idcode);

    QLabel* m_idcodeLabel;
    QLabel* m_manufacturerLabel;
    QLabel* m_partNumberLabel;
    QLabel* m_versionLabel;
    QPushButton* m_btnOK;

    struct IDCODEInfo {
        uint8_t version;
        uint16_t partNumber;
        uint16_t manufacturer;
    };
    IDCODEInfo decodeIDCODE(uint32_t idcode);
};

#endif // CHAINEXAMINEDIALOG_H
