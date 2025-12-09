#include "NewProjectWizard.h"

// ========== PackageTypePage ==========
PackageTypePage::PackageTypePage(QWidget* parent)
    : QWizardPage(parent)
{
    setTitle("Select Package Type");
    setSubTitle("Choose the physical package type of your device.");

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_edgePinsRadio = new QRadioButton(
        "Edge Pins (TQFP, SOIC, QFP, DIP)", this);
    QLabel* edgeDesc = new QLabel(
        "  Pins are located on the edges of the package", this);
    edgeDesc->setStyleSheet("color: gray; font-size: 10pt;");

    m_centerPinsRadio = new QRadioButton(
        "Center Pins (BGA, LGA)", this);
    QLabel* centerDesc = new QLabel(
        "  Pins are located in a grid pattern (ball grid array)", this);
    centerDesc->setStyleSheet("color: gray; font-size: 10pt;");

    m_centerPinsRadio->setChecked(true);  // Default

    layout->addWidget(m_edgePinsRadio);
    layout->addWidget(edgeDesc);
    layout->addSpacing(10);
    layout->addWidget(m_centerPinsRadio);
    layout->addWidget(centerDesc);
    layout->addStretch();
}

PackageTypePage::PackageType PackageTypePage::getSelectedType() const {
    if (m_edgePinsRadio->isChecked()) {
        return PackageType::EDGE_PINS;
    }
    return PackageType::CENTER_PINS;
}

// ========== NewProjectWizard ==========
NewProjectWizard::NewProjectWizard(uint32_t idcode, QWidget* parent)
    : QWizard(parent), m_idcode(idcode)
{
    setWindowTitle("New Project Wizard");
    setWizardStyle(QWizard::ModernStyle);

    m_packagePage = new PackageTypePage(this);
    addPage(m_packagePage);

    setButtonText(QWizard::FinishButton, "Create Project");
}

PackageTypePage::PackageType NewProjectWizard::getPackageType() const {
    return m_packagePage->getSelectedType();
}
