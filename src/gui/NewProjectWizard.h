#ifndef NEWPROJECTWIZARD_H
#define NEWPROJECTWIZARD_H

#include <QWizard>
#include <QWizardPage>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QGroupBox>

class PackageTypePage : public QWizardPage {
    Q_OBJECT

public:
    explicit PackageTypePage(uint32_t idcode, QWidget* parent = nullptr);

    enum class PackageType {
        EDGE_PINS,   // TQFP, SOIC, QFP
        CENTER_PINS  // BGA, LGA
    };

    PackageType getSelectedType() const;
    int getHorizontalPins() const;
    int getVerticalPins() const;
    QString getDeviceName() const;

private slots:
    void onPackageTypeChanged();

private:
    QRadioButton* m_edgePinsRadio;
    QRadioButton* m_centerPinsRadio;

    QLineEdit* m_deviceNameEdit;
    QSpinBox* m_horizontalPinsSpin;
    QSpinBox* m_verticalPinsSpin;
    QGroupBox* m_dimGroup;
    QLabel* m_idcodeLabel;

    uint32_t m_idcode;
};

class NewProjectWizard : public QWizard {
    Q_OBJECT

public:
    explicit NewProjectWizard(uint32_t idcode, QWidget* parent = nullptr);

    PackageTypePage::PackageType getPackageType() const;
    int getHorizontalPins() const;
    int getVerticalPins() const;
    QString getDeviceName() const;

private:
    PackageTypePage* m_packagePage;
    uint32_t m_idcode;
};

#endif // NEWPROJECTWIZARD_H