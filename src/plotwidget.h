#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QPoint>
#include <QRect>
#include <cstdint>

/**
 * PlotWidget
 * ----------
 * Two stacked plots (time + frequency) with:
 *   - FFT single peak marker
 *   - Rubber-band drag-to-zoom (X and Y)
 *   - Double-click to reset zoom
 *   - Crosshair cursor with readout
 *   - Maximize / restore each sub-plot
 *   - RF metrics panel: SNR, SFDR, Harmonics, ENOB
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
    // ── RF metrics ────────────────────────────────────────────────────────
    struct RfMetrics {
        double snr      = 0;   // dB
        double sfdr     = 0;   // dBc
        double thd      = 0;   // dBc  (total harmonic distortion)
        double enob     = 0;   // bits
        double fundFreq = 0;   // MHz
        double fundPow  = 0;   // dBFS
        QVector<double> harmonicFreqs; // MHz
        QVector<double> harmonicPows;  // dBFS
        bool valid = false;
    };

    // ── plot data ─────────────────────────────────────────────────────────
    struct Peak {
        double  x;
        double  y;
        QString label;
    };

    struct PlotData {
        QVector<double> xs;
        QVector<double> ys;
        double dataXMin = 0, dataXMax = 1;
        double dataYMin = -1, dataYMax = 1;
        double viewXMin = 0, viewXMax = 1;
        double viewYMin = -1, viewYMax = 1;
        QString xLabel, yLabel, title;
        QColor  lineColor;
        QVector<Peak> peaks;
    };

    // ── maximize state ────────────────────────────────────────────────────
    enum ViewState { BothPlots, TimeOnly, FreqOnly };
    ViewState m_viewState = BothPlots;

    // ── builders ──────────────────────────────────────────────────────────
    void buildTimePlot(const QVector<qint16> &s, double rateHz);
    void buildFreqPlot(const QVector<qint16> &s, double rateHz);
    void findPeaks(PlotData &d);
    void computeMetrics(const QVector<double> &db,
                        const QVector<double> &freqsMHz,
                        double sampleRateHz);

    static QVector<double> hammingFFT_dBFS(
        const QVector<qint16> &s, double rateHz,
        QVector<double> &freqsMHz);

    // ── drawing ───────────────────────────────────────────────────────────
    void drawPlot(QPainter &p, const QRect &wr, PlotData &d) const;
    void drawMetricsPanel(QPainter &p, const QRect &wr) const;
    void drawHarmonicMarkers(QPainter &p, const QRect &plot) const;
    void drawPeaks(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawCrosshair(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawRubberBand(QPainter &p, const QRect &plot, const PlotData &d) const;
    void drawMaxButton(QPainter &p, const QRect &wr, bool isMaximized) const;

    // ── layout ────────────────────────────────────────────────────────────
    QRect timeWidgetRect() const;
    QRect freqWidgetRect() const;
    QRect plotRect(const QRect &wr) const;
    QRect maxBtnRect(const QRect &wr) const;
    // metrics panel lives inside the freq widget rect, right side
    QRect metricsPanelRect(const QRect &freqWr) const;

    bool hitTimePlot(const QPoint &pos) const;
    bool hitFreqPlot(const QPoint &pos) const;
    bool hitTimeMaxBtn(const QPoint &pos) const;
    bool hitFreqMaxBtn(const QPoint &pos) const;

    // ── state ─────────────────────────────────────────────────────────────
    QString   m_label;
    PlotData  m_time;
    PlotData  m_freq;
    RfMetrics m_metrics;

    // rubber-band
    bool   m_selecting    = false;
    QPoint m_selStart, m_selEnd;
    bool   m_selectIsTime = false;

    // crosshair
    QPoint m_mousePos;
    bool   m_mouseInWidget = false;

    static constexpr int PAD_L   = 56, PAD_R = 12, PAD_T = 28, PAD_B = 38;
    static constexpr int BTN_SZ  = 20;
    static constexpr int MET_W   = 175;  // metrics panel width (pixels)

    static constexpr QRgb COL_BG      = 0xFF1E1E2E;
    static constexpr QRgb COL_PLOTBG  = 0xFF181825;
    static constexpr QRgb COL_GRID    = 0xFF313244;
    static constexpr QRgb COL_AXIS    = 0xFFCDD6F4;
    static constexpr QRgb COL_TITLE   = 0xFFCBA6F7;
    static constexpr QRgb COL_TIME    = 0xFF89B4FA;
    static constexpr QRgb COL_FREQ    = 0xFFA6E3A1;
    static constexpr QRgb COL_PEAK    = 0xFFFF5555;
    static constexpr QRgb COL_CROSS   = 0x66FFFFFF;
    static constexpr QRgb COL_BTN     = 0xFF45475A;
    static constexpr QRgb COL_BTNHOV  = 0xFF6C7086;
    static constexpr QRgb COL_HARM    = 0xFFFFB86C;  // harmonic marker colour
    static constexpr QRgb COL_METBG   = 0xCC181825;  // metrics panel bg
};
