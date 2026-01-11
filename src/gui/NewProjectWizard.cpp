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

    // 1. Device Name Group
    QGroupBox* nameGroup = new QGroupBox("Device Name", this);
    QVBoxLayout* nameLayout = new QVBoxLayout(nameGroup);

    QLabel* nameLabel = new QLabel("Enter a name for this device:", this);
    nameLabel->setStyleSheet("color: gray; font-size: 9pt;");

    m_deviceNameEdit = new QLineEdit(this);
    m_deviceNameEdit->setPlaceholderText("e.g., STM32F407, FPGA_Board, MyDevice");
    m_deviceNameEdit->setText("Unknown Device");

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_deviceNameEdit);

    layout->addWidget(nameGroup);
    layout->addSpacing(15);

    // 2. Package Type Group
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

    // 3. Expected Pins Group (solo para EDGE_PINS)
    m_dimGroup = new QGroupBox("Expected Pin Distribution", this);
    QFormLayout* dimLayout = new QFormLayout(m_dimGroup);

    QLabel* infoLabel = new QLabel(
        "Specify approximate number of pins expected on each side.\n"
        "This helps visualize the chip's aspect ratio.", this);
    infoLabel->setStyleSheet("color: gray; font-size: 9pt;");
    infoLabel->setWordWrap(true);
    dimLayout->addRow(infoLabel);

    m_horizontalPinsSpin = new QSpinBox(this);
    m_horizontalPinsSpin->setRange(1, 200);
    m_horizontalPinsSpin->setValue(0);  // 0 = auto (cuadrado)
    m_horizontalPinsSpin->setSpecialValueText("Auto (square)");
    m_horizontalPinsSpin->setToolTip("Expected pins on top/bottom sides (0 = auto-calculate)");

    m_verticalPinsSpin = new QSpinBox(this);
    m_verticalPinsSpin->setRange(1, 200);
    m_verticalPinsSpin->setValue(0);  // 0 = auto (cuadrado)
    m_verticalPinsSpin->setSpecialValueText("Auto (square)");
    m_verticalPinsSpin->setToolTip("Expected pins on left/right sides (0 = auto-calculate)");

    dimLayout->addRow("Horizontal (top/bottom):", m_horizontalPinsSpin);
    dimLayout->addRow("Vertical (left/right):", m_verticalPinsSpin);

    layout->addWidget(m_dimGroup);
    layout->addStretch();

    // Conectar señales para ocultar/mostrar dimGroup
    connect(m_edgePinsRadio, &QRadioButton::toggled, this, &PackageTypePage::onPackageTypeChanged);
    connect(m_centerPinsRadio, &QRadioButton::toggled, this, &PackageTypePage::onPackageTypeChanged);

    // Configurar visibilidad inicial
    onPackageTypeChanged();
}

void PackageTypePage::onPackageTypeChanged() {
    bool isEdgePins = m_edgePinsRadio->isChecked();
    m_dimGroup->setVisible(isEdgePins);
}

PackageTypePage::PackageType PackageTypePage::getSelectedType() const {
    return m_edgePinsRadio->isChecked() ? PackageType::EDGE_PINS : PackageType::CENTER_PINS;
}

int PackageTypePage::getHorizontalPins() const {
    return m_horizontalPinsSpin->value();
}

int PackageTypePage::getVerticalPins() const {
    return m_verticalPinsSpin->value();
}

QString PackageTypePage::getDeviceName() const {
    return m_deviceNameEdit->text();
}

// ========== NewProjectWizard ==========
NewProjectWizard::NewProjectWizard(uint32_t idcode, QWidget* parent)
    : QWizard(parent), m_idcode(idcode)
{
    setWindowTitle("New Project Wizard");
    m_packagePage = new PackageTypePage(idcode, this);
    addPage(m_packagePage);
    setButtonText(QWizard::FinishButton, "Next: Load BSDL");
    setMinimumSize(700, 700);
    resize(750, 750);
}

PackageTypePage::PackageType NewProjectWizard::getPackageType() const {
    return m_packagePage->getSelectedType();
}

int NewProjectWizard::getHorizontalPins() const {
    return m_packagePage->getHorizontalPins();
}

int NewProjectWizard::getVerticalPins() const {
    return m_packagePage->getVerticalPins();
}

QString NewProjectWizard::getDeviceName() const {
    return m_packagePage->getDeviceName();
}