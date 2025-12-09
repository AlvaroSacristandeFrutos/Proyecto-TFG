#ifndef NEWPROJECTWIZARD_H
#define NEWPROJECTWIZARD_H

#include <QWizard>
#include <QWizardPage>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QLabel>

class PackageTypePage : public QWizardPage {
    Q_OBJECT

public:
    explicit PackageTypePage(QWidget* parent = nullptr);

    enum class PackageType {
        EDGE_PINS,   // TQFP, SOIC, QFP
        CENTER_PINS  // BGA, LGA
    };

    PackageType getSelectedType() const;

private:
    QRadioButton* m_edgePinsRadio;
    QRadioButton* m_centerPinsRadio;
};

class NewProjectWizard : public QWizard {
    Q_OBJECT

public:
    explicit NewProjectWizard(uint32_t idcode, QWidget* parent = nullptr);

    PackageTypePage::PackageType getPackageType() const;

private:
    PackageTypePage* m_packagePage;
    uint32_t m_idcode;
};

#endif // NEWPROJECTWIZARD_H
