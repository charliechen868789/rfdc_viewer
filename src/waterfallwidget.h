#pragma once
#include <QWidget>
#include <QVector>
#include <QImage>
#include <QString>
#include <QPoint>

/**
 * WaterfallWidget
 * ---------------
 * Scrolling spectrogram: each call to addRow() appends one FFT magnitude
 * spectrum as a new row at the top. Oldest rows scroll off the bottom.
 * Colour map: viridis-style (dark blue → cyan → green → yellow → red).
 *
 * Controls:
 *   - dBFS range sliders (min/max) editable via right-click context menu
 *   - Crosshair readout: frequency + dBFS + time offset
 *   - Maximize button
 *   - clearHistory() resets the buffer
 */
class WaterfallWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaterfallWidget(const QString &label, QWidget *parent = nullptr);

    // Add one FFT row. Call once per acquire/generate.
    void addRow(const QVector<double> &dbfsSpectrum,
                double freqMinMHz, double freqMaxMHz);

    void clearHistory();
    void setDbRange(double minDb, double maxDb);
    void setMaxRows(int rows);   // default 200

    bool    isMaximized() const { return m_maximized; }
    void    setMaximized(bool v){ m_maximized = v; update(); }
signals:
    void maximizeToggled(bool maximized);
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void rebuildImage();
    static QRgb dbToColor(double normalised); // 0..1 → viridis colour

    QString m_label;

    // Each row = one spectrum, newest at index 0
    QVector<QVector<double>> m_rows;
    double  m_freqMin = 0,  m_freqMax = 1000;  // MHz
    double  m_dbMin   = -120, m_dbMax = 0;
    int     m_maxRows = 200;

    // Rendered image (rebuilt when rows change)
    QImage  m_image;
    bool    m_dirty = false;

    // Crosshair
    QPoint  m_mousePos;
    bool    m_mouseIn  = false;
    bool    m_maximized = false;

    static constexpr int PAD_L  = 56, PAD_R = 90, PAD_T = 28, PAD_B = 38;
    static constexpr int BTN_SZ = 20;

    static constexpr QRgb COL_BG    = 0xFF1E1E2E;
    static constexpr QRgb COL_AXIS  = 0xFFCDD6F4;
    static constexpr QRgb COL_TITLE = 0xFFCBA6F7;
    static constexpr QRgb COL_GRID  = 0xFF313244;
    static constexpr QRgb COL_CROSS = 0xAAFFFFFF;
    static constexpr QRgb COL_BTN   = 0xFF45475A;
    static constexpr QRgb COL_BTNHOV= 0xFF6C7086;
};
