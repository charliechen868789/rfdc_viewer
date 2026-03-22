#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QPoint>
#include <cstdint>

/**
 * PlotWidget
 * ----------
 * Two stacked plots (time + frequency) with:
 *   - FFT peak markers only (top-5, annotated)
 *   - Rubber-band drag-to-zoom (X and Y)
 *   - Double-click to reset zoom
 *   - Crosshair cursor with readout
 */
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(const QString &label, QWidget *parent = nullptr);

    void updateWaveform(const QVector<qint16> &samples, double sampleRateHz);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // ── plot data ─────────────────────────────────────────────────────────
    struct Peak {
        double  x;
        double  y;
        QString label;
    };

    struct PlotData {
        QVector<double> xs;
        QVector<double> ys;
        double dataXMin = 0, dataXMax = 1;   // full data extent
        double dataYMin = -1, dataYMax = 1;
        double viewXMin = 0, viewXMax = 1;   // current zoomed view
        double viewYMin = -1, viewYMax = 1;
        QString xLabel, yLabel, title;
        QColor  lineColor;
        QVector<Peak> peaks;
    };

    // ── builders ──────────────────────────────────────────────────────────
    void buildTimePlot(const QVector<qint16> &s, double rateHz);
    void buildFreqPlot(const QVector<qint16> &s, double rateHz);
    void findPeaks(PlotData &d);

    static QVector<double> hammingFFT_dBFS(
        const QVector<qint16> &s, double rateHz,
        QVector<double> &freqsMHz);

    // ── drawing ───────────────────────────────────────────────────────────
    void drawPlot(QPainter &p, const QRect &widgetRect, PlotData &d) const;
    void drawPeaks(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawCrosshair(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawRubberBand(QPainter &p, const QRect &plot, const PlotData &d) const;

    // ── coordinate helpers ────────────────────────────────────────────────
    QRect plotRect(const QRect &widgetRect) const;
    bool  hitTimePlot(const QPoint &pos) const;
    bool  hitFreqPlot(const QPoint &pos) const;

    // ── state ─────────────────────────────────────────────────────────────
    QString  m_label;
    PlotData m_time;
    PlotData m_freq;

    // rubber-band zoom
    bool   m_selecting    = false;
    QPoint m_selStart;
    QPoint m_selEnd;
    bool   m_selectIsTime = false;

    // crosshair
    QPoint m_mousePos;
    bool   m_mouseInWidget = false;

    // layout
    static constexpr int PAD_L = 56, PAD_R = 12, PAD_T = 28, PAD_B = 38;

    // colours
    static constexpr QRgb COL_BG     = 0xFF1E1E2E;
    static constexpr QRgb COL_PLOTBG = 0xFF181825;
    static constexpr QRgb COL_GRID   = 0xFF313244;
    static constexpr QRgb COL_AXIS   = 0xFFCDD6F4;
    static constexpr QRgb COL_TITLE  = 0xFFCBA6F7;
    static constexpr QRgb COL_TIME   = 0xFF89B4FA;
    static constexpr QRgb COL_FREQ   = 0xFFA6E3A1;
    static constexpr QRgb COL_PEAK   = 0xFFFF5555;
    static constexpr QRgb COL_CROSS  = 0x66FFFFFF;
};
