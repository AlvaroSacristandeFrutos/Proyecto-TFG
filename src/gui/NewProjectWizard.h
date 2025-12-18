#ifndef NEWPROJECTWIZARD_H
#define NEWPROJECTWIZARD_H

#include <QWizard>
#include <QWizardPage>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>

class PackageTypePage : public QWizardPage {
    Q_OBJECT

public:
    explicit PackageTypePage(uint32_t idcode, QWidget* parent = nullptr);

    enum class PackageType {
        EDGE_PINS,   // TQFP, SOIC, QFP
        CENTER_PINS  // BGA, LGA
    };

    PackageType getSelectedType() const;
    // --- NUEVOS GETTERS ---
    double getChipWidth() const;
    double getChipHeight() const;

private:
    QRadioButton* m_edgePinsRadio;
    QRadioButton* m_centerPinsRadio;

    // --- NUEVOS CONTROLES ---
    QDoubleSpinBox* m_widthSpin;
    QDoubleSpinBox* m_heightSpin;
    QLabel* m_idcodeLabel;

    uint32_t m_idcode;
};

class NewProjectWizard : public QWizard {
    Q_OBJECT

public:
    explicit NewProjectWizard(uint32_t idcode, QWidget* parent = nullptr);

    PackageTypePage::PackageType getPackageType() const;
    // --- NUEVOS WRAPPERS ---
    double getChipWidth() const;
    double getChipHeight() const;

private:
    PackageTypePage* m_packagePage;
    uint32_t m_idcode;
};

#endif // NEWPROJECTWIZARD_H