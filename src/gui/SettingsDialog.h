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
 * - Polling interval (refresh rate): 1ms, 5ms, 10ms, 50ms, 100ms, 250ms, 500ms
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
    int samplesPerSecond() const;

    // Setters
    void setPollingInterval(int ms);
    void setSampleDecimation(int decimation);
    void setSamplesPerSecond(int samplesPerSec);

signals:
    void pollingIntervalChanged(int ms);
    void sampleDecimationChanged(int decimation);
    void samplesPerSecondChanged(int samplesPerSec);

private slots:
    void onPollingIntervalIndexChanged(int index);
    void onSampleDecimationValueChanged(int value);
    void onSamplesPerSecondIndexChanged(int index);
    void onApplyClicked();
    void onAccepted();

private:
    void setupUI();
    void updateDecimationLabel();

    // UI components
    QComboBox *pollingIntervalCombo;
    QSlider *sampleDecimationSlider;
    QLabel *decimationValueLabel;
    QComboBox *samplesPerSecondCombo;
    QDialogButtonBox *buttonBox;
    QPushButton *applyButton;

    // Current values
    int m_pollingInterval;
    int m_sampleDecimation;
    int m_samplesPerSecond;
};

#endif // SETTINGSDIALOG_H
