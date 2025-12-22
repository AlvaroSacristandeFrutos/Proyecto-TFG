#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>

/**
 * @brief Settings dialog for performance configuration
 *
 * Allows user to configure:
 * - Polling interval (refresh rate): 50ms, 100ms, 250ms, 500ms
 * - Sample decimation: capture 1 of every X samples (1-100)
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    // Getters
    int pollingInterval() const;
    int sampleDecimation() const;

    // Setters
    void setPollingInterval(int ms);
    void setSampleDecimation(int decimation);

signals:
    void pollingIntervalChanged(int ms);
    void sampleDecimationChanged(int decimation);

private slots:
    void onPollingIntervalIndexChanged(int index);
    void onSampleDecimationValueChanged(int value);
    void onApplyClicked();
    void onAccepted();

private:
    void setupUI();
    void updateDecimationLabel();

    // UI components
    QComboBox *pollingIntervalCombo;
    QSlider *sampleDecimationSlider;
    QLabel *decimationValueLabel;
    QDialogButtonBox *buttonBox;
    QPushButton *applyButton;

    // Current values
    int m_pollingInterval;
    int m_sampleDecimation;
};

#endif // SETTINGSDIALOG_H
