#include "plotwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>

// ── Radix-2 DIT FFT ──────────────────────────────────────────────────────────
static std::vector<std::complex<double>> doFFT(std::vector<std::complex<double>> x)
{
    int N = (int)x.size();
    if (N <= 1) return x;
    int n2 = 1;
    while (n2 < N) n2 <<= 1;
    x.resize(n2, {0, 0}); N = n2;
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1, 0);
            for (int j = 0; j < len / 2; ++j) {
                auto u = x[i + j], v = x[i + j + len / 2] * w;
                x[i + j] = u + v; x[i + j + len / 2] = u - v; w *= wlen;
            }
        }
    }
    return x;
}

// ── Constructor ───────────────────────────────────────────────────────────────
PlotWidget::PlotWidget(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label)
{
    setMinimumSize(400, 420);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    m_time.title     = label + "  \u2013  Time Domain";
    m_time.xLabel    = "Time (\u00b5s)";
    m_time.yLabel    = "Amplitude";
    m_time.lineColor = QColor(COL_TIME);
    m_time.dataXMin = m_time.viewXMin = 0;
    m_time.dataXMax = m_time.viewXMax = 1;
    m_time.dataYMin = m_time.viewYMin = -32768;
    m_time.dataYMax = m_time.viewYMax =  32767;

    m_freq.title     = label + "  \u2013  Frequency Domain";
    m_freq.xLabel    = "Freq (MHz)";
    m_freq.yLabel    = "dBFS";
    m_freq.lineColor = QColor(COL_FREQ);
    m_freq.dataXMin = m_freq.viewXMin = 0;
    m_freq.dataXMax = m_freq.viewXMax = 1000;
    m_freq.dataYMin = m_freq.viewYMin = -120;
    m_freq.dataYMax = m_freq.viewYMax = 0;
}

// ── Public API ────────────────────────────────────────────────────────────────
void PlotWidget::updateWaveform(const QVector<qint16> &samples, double sampleRateHz)
{
    if (samples.isEmpty()) return;
    buildTimePlot(samples, sampleRateHz);
    buildFreqPlot(samples, sampleRateHz);
    update();
}

// ── Layout helpers ────────────────────────────────────────────────────────────
QRect PlotWidget::timeWidgetRect() const
{
    switch (m_viewState) {
    case TimeOnly: return rect();
    case FreqOnly: return QRect();
    default:       return QRect(0, 0, width(), height() / 2);
    }
}

QRect PlotWidget::freqWidgetRect() const
{
    switch (m_viewState) {
    case FreqOnly: return rect();
    case TimeOnly: return QRect();
    default:       return QRect(0, height() / 2, width(), height() - height() / 2);
    }
}

QRect PlotWidget::plotRect(const QRect &wr) const
{
    if (wr.isEmpty()) return QRect();
    return QRect(wr.left()  + PAD_L,
                 wr.top()   + PAD_T,
                 wr.width() - PAD_L - PAD_R,
                 wr.height()- PAD_T - PAD_B);
}

QRect PlotWidget::maxBtnRect(const QRect &wr) const
{
    // Top-right corner of the widget rect, inside the title bar
    return QRect(wr.right() - BTN_SZ - 4,
                 wr.top() + (PAD_T - BTN_SZ) / 2,
                 BTN_SZ, BTN_SZ);
}

bool PlotWidget::hitTimePlot(const QPoint &pos) const
{
    return plotRect(timeWidgetRect()).contains(pos);
}

bool PlotWidget::hitFreqPlot(const QPoint &pos) const
{
    return plotRect(freqWidgetRect()).contains(pos);
}

bool PlotWidget::hitTimeMaxBtn(const QPoint &pos) const
{
    QRect wr = timeWidgetRect();
    return !wr.isEmpty() && maxBtnRect(wr).contains(pos);
}

bool PlotWidget::hitFreqMaxBtn(const QPoint &pos) const
{
    QRect wr = freqWidgetRect();
    return !wr.isEmpty() && maxBtnRect(wr).contains(pos);
}

// ── Data builders ─────────────────────────────────────────────────────────────
void PlotWidget::buildTimePlot(const QVector<qint16> &s, double rateHz)
{
    int N = s.size();
    m_time.xs.resize(N);
    m_time.ys.resize(N);
    double tScale = 1e6 / rateHz;
    double yMin = 1e9, yMax = -1e9;
    for (int i = 0; i < N; ++i) {
        m_time.xs[i] = i * tScale;
        m_time.ys[i] = s[i];
        if (s[i] < yMin) yMin = s[i];
        if (s[i] > yMax) yMax = s[i];
    }
    double pad = (yMax - yMin) * 0.05;
    m_time.dataXMin = 0;
    m_time.dataXMax = (N - 1) * tScale;
    m_time.dataYMin = yMin - pad;
    m_time.dataYMax = yMax + pad;
    if (m_time.dataYMin == m_time.dataYMax) { m_time.dataYMin -= 1; m_time.dataYMax += 1; }
    m_time.viewXMin = m_time.dataXMin; m_time.viewXMax = m_time.dataXMax;
    m_time.viewYMin = m_time.dataYMin; m_time.viewYMax = m_time.dataYMax;
    m_time.peaks.clear();
}

void PlotWidget::buildFreqPlot(const QVector<qint16> &s, double rateHz)
{
    QVector<double> freqs;
    QVector<double> db = hammingFFT_dBFS(s, rateHz, freqs);
    m_freq.xs = freqs; m_freq.ys = db;
    m_freq.dataXMin = 0;
    m_freq.dataXMax = freqs.isEmpty() ? 1.0 : freqs.last();
    double yMin = 1e9, yMax = -1e9;
    for (double v : db) { if (v < yMin) yMin = v; if (v > yMax) yMax = v; }
    m_freq.dataYMin = yMin - 5.0; m_freq.dataYMax = yMax + 5.0;
    if (m_freq.dataYMin == m_freq.dataYMax) { m_freq.dataYMin -= 1; m_freq.dataYMax += 1; }
    m_freq.viewXMin = m_freq.dataXMin; m_freq.viewXMax = m_freq.dataXMax;
    m_freq.viewYMin = m_freq.dataYMin; m_freq.viewYMax = m_freq.dataYMax;
    findPeaks(m_freq);
}

QVector<double> PlotWidget::hammingFFT_dBFS(
    const QVector<qint16> &s, double rateHz, QVector<double> &freqsMHz)
{
    int N = s.size();
    constexpr double ref = 32768.0;
    std::vector<std::complex<double>> cx(N);
    double winSum = 0;
    for (int i = 0; i < N; ++i) {
        double w = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / (N - 1));
        cx[i] = {s[i] * w, 0.0}; winSum += w;
    }
    auto spec = doFFT(cx);
    int half = N / 2 + 1;
    QVector<double> db(half); freqsMHz.resize(half);
    double scale = 2.0 / winSum;
    for (int i = 0; i < half; ++i) {
        double mag  = std::abs(spec[i]) * scale;
        db[i]       = 20.0 * std::log10(std::max(mag / ref, 1e-12));
        freqsMHz[i] = (double)i * rateHz / (N * 1e6);
    }
    return db;
}

// ── Peak detection (1 peak, FFT only) ────────────────────────────────────────
void PlotWidget::findPeaks(PlotData &d)
{
    d.peaks.clear();
    int N = d.ys.size();
    if (N < 3) return;

    const int    MIN_DIST = 5;
    const double MIN_PROM = 6.0;
    const int    MAX_PEAKS = 1;

    struct Candidate { int idx; double val; };
    QVector<Candidate> cands;

    for (int i = MIN_DIST; i < N - MIN_DIST; ++i) {
        bool isMax = true;
        for (int k = i - MIN_DIST; k <= i + MIN_DIST && isMax; ++k)
            if (k != i && d.ys[k] >= d.ys[i]) isMax = false;
        if (!isMax) continue;
        int   lo   = std::max(0,   i - 20);
        int   hi   = std::min(N-1, i + 20);
        double lMin = *std::min_element(d.ys.begin() + lo, d.ys.begin() + i);
        double rMin = *std::min_element(d.ys.begin() + i + 1, d.ys.begin() + hi + 1);
        if (d.ys[i] - std::max(lMin, rMin) < MIN_PROM) continue;
        cands.append({i, d.ys[i]});
    }

    std::sort(cands.begin(), cands.end(),
              [](const Candidate &a, const Candidate &b){ return a.val > b.val; });
    if (cands.size() > MAX_PEAKS) cands.resize(MAX_PEAKS);

    for (const auto &c : cands) {
        Peak pk;
        pk.x     = d.xs[c.idx];
        pk.y     = c.val;
        pk.label = QString("%1 MHz\n%2 dBFS").arg(pk.x, 0, 'f', 2).arg(pk.y, 0, 'f', 1);
        d.peaks.append(pk);
    }
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check maximize buttons first
        if (hitTimeMaxBtn(event->pos())) {
            m_viewState = (m_viewState == TimeOnly) ? BothPlots : TimeOnly;
            update(); return;
        }
        if (hitFreqMaxBtn(event->pos())) {
            m_viewState = (m_viewState == FreqOnly) ? BothPlots : FreqOnly;
            update(); return;
        }
        // Start rubber-band
        bool inTime = hitTimePlot(event->pos());
        bool inFreq = hitFreqPlot(event->pos());
        if (inTime || inFreq) {
            m_selecting    = true;
            m_selStart     = event->pos();
            m_selEnd       = event->pos();
            m_selectIsTime = inTime;
            setCursor(Qt::CrossCursor);
        }
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos      = event->pos();
    m_mouseInWidget = true;
    if (m_selecting) m_selEnd = event->pos();

    // Change cursor when hovering over max buttons
    if (hitTimeMaxBtn(event->pos()) || hitFreqMaxBtn(event->pos()))
        setCursor(Qt::PointingHandCursor);
    else if (!m_selecting)
        setCursor(Qt::ArrowCursor);

    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting) return;
    m_selecting = false;
    setCursor(Qt::ArrowCursor);

    PlotData &d = m_selectIsTime ? m_time : m_freq;
    QRect wr    = m_selectIsTime ? timeWidgetRect() : freqWidgetRect();
    QRect plot  = plotRect(wr);

    int x1 = qMin(m_selStart.x(), m_selEnd.x());
    int x2 = qMax(m_selStart.x(), m_selEnd.x());
    int y1 = qMin(m_selStart.y(), m_selEnd.y());
    int y2 = qMax(m_selStart.y(), m_selEnd.y());

    if ((x2 - x1) < 5 || (y2 - y1) < 5) { update(); return; }

    x1 = qMax(x1, plot.left());  x2 = qMin(x2, plot.right());
    y1 = qMax(y1, plot.top());   y2 = qMin(y2, plot.bottom());

    double xRange = d.viewXMax - d.viewXMin;
    double yRange = d.viewYMax - d.viewYMin;

    d.viewXMin = d.viewXMin + (double)(x1 - plot.left()) / plot.width()  * xRange;
    d.viewXMax = d.viewXMin + (double)(x2 - x1)          / plot.width()  * xRange;
    double newYMax = d.viewYMax - (double)(y1 - plot.top())  / plot.height() * yRange;
    double newYMin = d.viewYMax - (double)(y2 - plot.top())  / plot.height() * yRange;
    // Apply X first then Y (viewXMin already updated above)
    d.viewXMax = d.viewXMin + (double)(x2 - x1) / plot.width() * xRange;
    d.viewYMin = newYMin; d.viewYMax = newYMax;

    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    auto reset = [](PlotData &d) {
        d.viewXMin = d.dataXMin; d.viewXMax = d.dataXMax;
        d.viewYMin = d.dataYMin; d.viewYMax = d.dataYMax;
    };
    if (hitTimePlot(event->pos()))      reset(m_time);
    else if (hitFreqPlot(event->pos())) reset(m_freq);
    update();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_mouseInWidget = false;
    update();
}

// ── paintEvent ────────────────────────────────────────────────────────────────
void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(COL_BG));

    QRect twr = timeWidgetRect();
    QRect fwr = freqWidgetRect();
    if (!twr.isEmpty()) drawPlot(p, twr, m_time);
    if (!fwr.isEmpty()) drawPlot(p, fwr, m_freq);
}

// ── drawPlot ──────────────────────────────────────────────────────────────────
void PlotWidget::drawPlot(QPainter &p, const QRect &wr, PlotData &d) const
{
    QRect plot = plotRect(wr);
    if (plot.width() <= 0 || plot.height() <= 0) return;

    p.fillRect(wr,   QColor(COL_BG));
    p.fillRect(plot, QColor(COL_PLOTBG));

    // ── title + max button ────────────────────────────────────────────────
    QString titleStr = d.title;
    bool zoomed = (d.viewXMin > d.dataXMin + 1e-9 || d.viewXMax < d.dataXMax - 1e-9 ||
                   d.viewYMin > d.dataYMin + 1e-9 || d.viewYMax < d.dataYMax - 1e-9);
    if (zoomed) titleStr += "  [dbl-click: reset zoom]";

    p.setPen(QColor(COL_TITLE));
    QFont tf = p.font(); tf.setPixelSize(12); tf.setBold(true); p.setFont(tf);
    p.drawText(QRect(wr.left(), wr.top(), wr.width() - BTN_SZ - 8, PAD_T),
               Qt::AlignHCenter | Qt::AlignVCenter, titleStr);

    // Maximize/restore button
    bool isMaximized = (&d == &m_time) ? (m_viewState == TimeOnly)
                                       : (m_viewState == FreqOnly);
    drawMaxButton(p, wr, isMaximized);

    // ── grid ──────────────────────────────────────────────────────────────
    const int NX = 5, NY = 4;
    p.setPen(QPen(QColor(COL_GRID), 1, Qt::DotLine));
    for (int i = 0; i <= NX; ++i) {
        int x = plot.left() + i * plot.width() / NX;
        p.drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int j = 0; j <= NY; ++j) {
        int y = plot.top() + j * plot.height() / NY;
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    // ── axis labels ───────────────────────────────────────────────────────
    QFont af = p.font(); af.setPixelSize(9); af.setBold(false); p.setFont(af);
    p.setPen(QColor(COL_AXIS));
    for (int j = 0; j <= NY; ++j) {
        double val = d.viewYMax - j * (d.viewYMax - d.viewYMin) / NY;
        int    y   = plot.top() + j * plot.height() / NY;
        p.drawText(QRect(wr.left(), y - 9, PAD_L - 3, 18),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(val, 'f', 0));
    }
    for (int i = 0; i <= NX; ++i) {
        double val = d.viewXMin + i * (d.viewXMax - d.viewXMin) / NX;
        int    x   = plot.left() + i * plot.width() / NX;
        p.drawText(QRect(x - 24, plot.bottom() + 2, 48, 14),
                   Qt::AlignHCenter, QString::number(val, 'f', 1));
    }
    p.drawText(QRect(plot.left(), plot.bottom() + 18, plot.width(), 14),
               Qt::AlignHCenter, d.xLabel);
    p.save();
    p.translate(wr.left() + 11, plot.top() + plot.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-40, -6, 80, 13), Qt::AlignHCenter, d.yLabel);
    p.restore();

    // ── waveform ──────────────────────────────────────────────────────────
    if (d.xs.isEmpty()) return;

    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    int N = d.xs.size(), iStart = 0, iEnd = N - 1;
    for (int i = 0;     i < N;  ++i) if (d.xs[i] >= d.viewXMin) { iStart = i; break; }
    for (int i = N - 1; i >= 0; --i) if (d.xs[i] <= d.viewXMax) { iEnd   = i; break; }
    int step = std::max(1, (iEnd - iStart + 1) / plot.width());

    p.setPen(QPen(d.lineColor, 1.2f));
    p.setClipRect(plot);

    QPainterPath path; bool first = true;
    for (int i = iStart; i <= iEnd; i += step) {
        double px = plot.left()   + (d.xs[i] - d.viewXMin) / xRange * plot.width();
        double py = plot.bottom() - (d.ys[i] - d.viewYMin) / yRange * plot.height();
        if (first) { path.moveTo(px, py); first = false; }
        else          path.lineTo(px, py);
    }
    p.drawPath(path);

    drawPeaks(p, plot, d);
    drawRubberBand(p, plot, d);
    p.setClipping(false);
    drawCrosshair(p, plot, d);

    // border
    p.setPen(QPen(QColor(COL_AXIS), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
}

// ── Maximize button ───────────────────────────────────────────────────────────
void PlotWidget::drawMaxButton(QPainter &p, const QRect &wr, bool isMaximized) const
{
    QRect btn = maxBtnRect(wr);
    bool hovered = btn.contains(m_mousePos) && m_mouseInWidget;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(hovered ? COL_BTNHOV : COL_BTN));
    p.drawRoundedRect(btn, 3, 3);

    p.setPen(QPen(QColor(COL_AXIS), 1.5));
    p.setBrush(Qt::NoBrush);

    int m = 4;  // margin inside button
    QRect inner = btn.adjusted(m, m, -m, -m);

    if (isMaximized) {
        // Restore icon: two inward-pointing arrows
        // top-left arrow pointing inward
        p.drawLine(inner.left(),  inner.top(),    inner.left()+4,  inner.top());
        p.drawLine(inner.left(),  inner.top(),    inner.left(),    inner.top()+4);
        // bottom-right arrow pointing inward
        p.drawLine(inner.right(), inner.bottom(), inner.right()-4, inner.bottom());
        p.drawLine(inner.right(), inner.bottom(), inner.right(),   inner.bottom()-4);
        // diagonal
        p.drawLine(inner.left()+1, inner.top()+1, inner.right()-1, inner.bottom()-1);
    } else {
        // Maximize icon: four outward-pointing corners
        p.drawLine(inner.left(),  inner.top(),    inner.left()+4,  inner.top());
        p.drawLine(inner.left(),  inner.top(),    inner.left(),    inner.top()+4);
        p.drawLine(inner.right(), inner.bottom(), inner.right()-4, inner.bottom());
        p.drawLine(inner.right(), inner.bottom(), inner.right(),   inner.bottom()-4);
        p.drawLine(inner.right(), inner.top(),    inner.right()-4, inner.top());
        p.drawLine(inner.right(), inner.top(),    inner.right(),   inner.top()+4);
        p.drawLine(inner.left(),  inner.bottom(), inner.left()+4,  inner.bottom());
        p.drawLine(inner.left(),  inner.bottom(), inner.left(),    inner.bottom()-4);
    }
}

// ── Peak markers ─────────────────────────────────────────────────────────────
void PlotWidget::drawPeaks(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (d.peaks.isEmpty()) return;
    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    QFont pf = p.font(); pf.setPixelSize(9); pf.setBold(true); p.setFont(pf);
    QFontMetrics fm(pf);

    for (const Peak &pk : d.peaks) {
        if (pk.x < d.viewXMin || pk.x > d.viewXMax) continue;
        if (pk.y < d.viewYMin || pk.y > d.viewYMax) continue;

        int px = plot.left()   + (int)((pk.x - d.viewXMin) / xRange * plot.width());
        int py = plot.bottom() - (int)((pk.y - d.viewYMin) / yRange * plot.height());

        // Diamond
        p.setPen(QPen(QColor(COL_PEAK), 1.5));
        p.setBrush(QColor(COL_PEAK));
        const int D = 5;
        QPolygon diamond;
        diamond << QPoint(px, py-D) << QPoint(px+D, py)
                << QPoint(px, py+D) << QPoint(px-D, py);
        p.drawPolygon(diamond);

        // Drop line
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(COL_PEAK), 1, Qt::DashLine));
        p.drawLine(px, py + D, px, plot.bottom());

        // Label box
        QStringList lines = pk.label.split('\n');
        int boxW = 0, boxH = 4;
        for (const QString &ln : lines) {
            boxW = std::max(boxW, fm.horizontalAdvance(ln) + 10);
            boxH += fm.height();
        }
        int bx = px - boxW / 2;
        int by = py - D - boxH - 4;
        if (bx < plot.left())         bx = plot.left();
        if (bx + boxW > plot.right()) bx = plot.right() - boxW;
        if (by < plot.top())          by = py + D + 4;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x88, 0x00, 0x00, 210));
        p.drawRoundedRect(bx, by, boxW, boxH, 3, 3);
        p.setPen(QColor(COL_PEAK));
        p.setBrush(Qt::NoBrush);
        int ly = by + 2 + fm.ascent();
        for (const QString &ln : lines) { p.drawText(bx + 5, ly, ln); ly += fm.height(); }
    }
    p.setBrush(Qt::NoBrush);
}

// ── Rubber-band ───────────────────────────────────────────────────────────────
void PlotWidget::drawRubberBand(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (!m_selecting) return;
    if (m_selectIsTime ? (&d != &m_time) : (&d != &m_freq)) return;

    int x1 = qMax(qMin(m_selStart.x(), m_selEnd.x()), plot.left());
    int x2 = qMin(qMax(m_selStart.x(), m_selEnd.x()), plot.right());
    int y1 = qMax(qMin(m_selStart.y(), m_selEnd.y()), plot.top());
    int y2 = qMin(qMax(m_selStart.y(), m_selEnd.y()), plot.bottom());

    p.setPen(QPen(Qt::white, 1, Qt::DashLine));
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawRect(x1, y1, x2 - x1, y2 - y1);
}

// ── Crosshair ─────────────────────────────────────────────────────────────────
void PlotWidget::drawCrosshair(QPainter &p, const QRect &plot, const PlotData &d) const
{
    if (!m_mouseInWidget || !plot.contains(m_mousePos)) return;

    double xRange = d.viewXMax - d.viewXMin; if (xRange == 0) xRange = 1;
    double yRange = d.viewYMax - d.viewYMin; if (yRange == 0) yRange = 1;

    double dataX = d.viewXMin + (double)(m_mousePos.x() - plot.left()) / plot.width()  * xRange;
    double dataY = d.viewYMax - (double)(m_mousePos.y() - plot.top())  / plot.height() * yRange;

    p.setPen(QPen(QColor(COL_CROSS), 1, Qt::DashLine));
    p.drawLine(m_mousePos.x(), plot.top(),    m_mousePos.x(), plot.bottom());
    p.drawLine(plot.left(),    m_mousePos.y(), plot.right(),   m_mousePos.y());

    QString readout = QString("X: %1  Y: %2").arg(dataX, 0, 'f', 3).arg(dataY, 0, 'f', 1);
    QFont rf = p.font(); rf.setPixelSize(10); rf.setBold(false); p.setFont(rf);
    QFontMetrics fm(rf);
    int rw = fm.horizontalAdvance(readout) + 10, rh = fm.height() + 6;
    int rx = m_mousePos.x() + 12, ry = m_mousePos.y() - rh - 4;
    if (rx + rw > plot.right())  rx = m_mousePos.x() - rw - 4;
    if (ry < plot.top())         ry = m_mousePos.y() + 8;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 46, 210));
    p.drawRoundedRect(rx, ry, rw, rh, 3, 3);
    p.setPen(QColor(COL_AXIS));
    p.drawText(rx + 5, ry + fm.ascent() + 3, readout);
}
