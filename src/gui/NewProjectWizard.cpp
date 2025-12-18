#include "NewProjectWizard.h"
#include <QFormLayout> // Necesario para organizar los inputs
#include <QGroupBox>

// ========== PackageTypePage ==========
PackageTypePage::PackageTypePage(uint32_t idcode, QWidget* parent)
    : QWizardPage(parent), m_idcode(idcode)
{
    setTitle("Chip Configuration");
    setSubTitle("Define package type and approximate dimensions.");

    QVBoxLayout* layout = new QVBoxLayout(this);

    // 0. IDCODE Display Group
    QGroupBox* idcodeGroup = new QGroupBox("Detected Device", this);
    QVBoxLayout* idcodeLayout = new QVBoxLayout(idcodeGroup);

    m_idcodeLabel = new QLabel(this);
    QString idcodeText = QString(
        "<b style='font-size:13pt; color:#2196F3;'>IDCODE: 0x%1</b><br>"
        "<span style='color:gray;'>Manufacturer: 0x%2 | Part: 0x%3 | Version: 0x%4</span>"
    )
    .arg(idcode, 8, 16, QChar('0'))
    .arg((idcode >> 1) & 0x7FF, 3, 16, QChar('0'))
    .arg((idcode >> 12) & 0xFFFF, 4, 16, QChar('0'))
    .arg((idcode >> 28) & 0xF, 1, 16);

    m_idcodeLabel->setText(idcodeText);
    m_idcodeLabel->setTextFormat(Qt::RichText);
    m_idcodeLabel->setAlignment(Qt::AlignCenter);
    m_idcodeLabel->setStyleSheet("padding: 10px; background-color: #f5f5f5; border-radius: 5px;");

    idcodeLayout->addWidget(m_idcodeLabel);
    layout->addWidget(idcodeGroup);
    layout->addSpacing(15);

    // 1. Package Type Group
    QGroupBox* typeGroup = new QGroupBox("Package Type", this);
    QVBoxLayout* typeLayout = new QVBoxLayout(typeGroup);

    m_edgePinsRadio = new QRadioButton("Edge Pins (TQFP, SOIC, QFP)", this);
    QLabel* edgeDesc = new QLabel("  Pins on the perimeter.", this);
    edgeDesc->setStyleSheet("color: gray; font-size: 9pt;");

    m_centerPinsRadio = new QRadioButton("Center Pins (BGA, LGA)", this);
    QLabel* centerDesc = new QLabel("  Pins in a grid array.", this);
    centerDesc->setStyleSheet("color: gray; font-size: 9pt;");

    m_centerPinsRadio->setChecked(true);

    typeLayout->addWidget(m_edgePinsRadio);
    typeLayout->addWidget(edgeDesc);
    typeLayout->addSpacing(5);
    typeLayout->addWidget(m_centerPinsRadio);
    typeLayout->addWidget(centerDesc);

    layout->addWidget(typeGroup);
    layout->addSpacing(15);

    // 2. Dimensions Group
    QGroupBox* dimGroup = new QGroupBox("Approximate Dimensions (Visual)", this);
    QFormLayout* dimLayout = new QFormLayout(dimGroup);

    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(10.0, 1000.0);
    m_widthSpin->setValue(400.0); // Default visual units
    m_widthSpin->setSuffix(" px");

    m_heightSpin = new QDoubleSpinBox(this);
    m_heightSpin->setRange(10.0, 1000.0);
    m_heightSpin->setValue(400.0); // Default visual units
    m_heightSpin->setSuffix(" px");

    dimLayout->addRow("Width:", m_widthSpin);
    dimLayout->addRow("Height:", m_heightSpin);

    layout->addWidget(dimGroup);
    layout->addStretch();
}

PackageTypePage::PackageType PackageTypePage::getSelectedType() const {
    return m_edgePinsRadio->isChecked() ? PackageType::EDGE_PINS : PackageType::CENTER_PINS;
}

double PackageTypePage::getChipWidth() const { return m_widthSpin->value(); }
double PackageTypePage::getChipHeight() const { return m_heightSpin->value(); }

// ========== NewProjectWizard ==========
NewProjectWizard::NewProjectWizard(uint32_t idcode, QWidget* parent)
    : QWizard(parent), m_idcode(idcode)
{
    setWindowTitle("New Project Wizard");
    m_packagePage = new PackageTypePage(idcode, this);
    addPage(m_packagePage);
    setButtonText(QWizard::FinishButton, "Next: Load BSDL");
}

PackageTypePage::PackageType NewProjectWizard::getPackageType() const {
    return m_packagePage->getSelectedType();
}

double NewProjectWizard::getChipWidth() const { return m_packagePage->getChipWidth(); }
double NewProjectWizard::getChipHeight() const { return m_packagePage->getChipHeight(); }