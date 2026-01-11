#include "ChainExamineDialog.h"
#include <QGroupBox>

ChainExamineDialog::ChainExamineDialog(uint32_t idcode, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("JTAG Chain Examination Results");
    setModal(true);
    setMinimumWidth(400);
    setupUI(idcode);
}

void ChainExamineDialog::setupUI(uint32_t idcode) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* titleLabel = new QLabel("<b>Device Detected on JTAG Chain</b>", this);
    mainLayout->addWidget(titleLabel);

    m_idcodeLabel = new QLabel(
        QString("IDCODE: <b>0x%1</b>").arg(idcode, 8, 16, QChar('0')), this);
    mainLayout->addWidget(m_idcodeLabel);

    auto info = decodeIDCODE(idcode);

    m_manufacturerLabel = new QLabel(
        QString("Manufacturer ID: 0x%1").arg(info.manufacturer, 3, 16, QChar('0')), this);
    m_partNumberLabel = new QLabel(
        QString("Part Number: 0x%1").arg(info.partNumber, 4, 16, QChar('0')), this);
    m_versionLabel = new QLabel(
        QString("Version: 0x%1").arg(info.version, 1, 16), this);

    mainLayout->addWidget(m_manufacturerLabel);
    mainLayout->addWidget(m_partNumberLabel);
    mainLayout->addWidget(m_versionLabel);
    mainLayout->addSpacing(20);

    QLabel* noteLabel = new QLabel(
        "<i>Please load BSDL file manually from Device menu</i>", this);
    mainLayout->addWidget(noteLabel);

    m_btnOK = new QPushButton("OK", this);
    connect(m_btnOK, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(m_btnOK);
}

ChainExamineDialog::IDCODEInfo ChainExamineDialog::decodeIDCODE(uint32_t idcode) {
    IDCODEInfo info;
    info.version = (idcode >> 28) & 0xF;
    info.partNumber = (idcode >> 12) & 0xFFFF;
    info.manufacturer = (idcode >> 1) & 0x7FF;
    return info;
}
