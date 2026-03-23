#include "plotwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <cmath>
#include <algorithm>
#include <complex>
#include <numeric>
#include <vector>

// ── Radix-2 DIT FFT ──────────────────────────────────────────────────────────
static std::vector<std::complex<double>> doFFT(std::vector<std::complex<double>> x)
{
    int N=(int)x.size(); if(N<=1)return x;
    int n2=1; while(n2<N)n2<<=1; x.resize(n2,{0,0}); N=n2;
    for(int i=1,j=0;i<N;++i){int bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
    for(int len=2;len<=N;len<<=1){double ang=-2.0*M_PI/len;std::complex<double>wlen(std::cos(ang),std::sin(ang));
        for(int i=0;i<N;i+=len){std::complex<double>w(1,0);for(int j=0;j<len/2;++j){auto u=x[i+j],v=x[i+j+len/2]*w;x[i+j]=u+v;x[i+j+len/2]=u-v;w*=wlen;}}}
    return x;
}

// ═════════════════════════════════════════════════════════════════════════════
// Constructor
// ═════════════════════════════════════════════════════════════════════════════
PlotWidget::PlotWidget(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label)
{
    setMinimumSize(400,460); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setMouseTracking(true);
    m_time.title=label+" \u2013 Time Domain"; m_time.xLabel="Time (\u00b5s)"; m_time.yLabel="Amplitude"; m_time.lineColor=QColor(COL_TIME);
    m_time.dataXMin=m_time.viewXMin=0; m_time.dataXMax=m_time.viewXMax=1; m_time.dataYMin=m_time.viewYMin=-32768; m_time.dataYMax=m_time.viewYMax=32767;
    m_freq.title=label+" \u2013 Frequency Domain"; m_freq.xLabel="Freq (MHz)"; m_freq.yLabel="dBFS"; m_freq.lineColor=QColor(COL_FREQ);
    m_freq.dataXMin=m_freq.viewXMin=0; m_freq.dataXMax=m_freq.viewXMax=1000; m_freq.dataYMin=m_freq.viewYMin=-120; m_freq.dataYMax=m_freq.viewYMax=0;
    m_pn.title=label+" \u2013 Phase Noise"; m_pn.xLabel="Offset (MHz, log)"; m_pn.yLabel="dBc/Hz"; m_pn.lineColor=QColor(COL_PN);
    m_pn.dataXMin=m_pn.viewXMin=-3; m_pn.dataXMax=m_pn.viewXMax=3; m_pn.dataYMin=m_pn.viewYMin=-160; m_pn.dataYMax=m_pn.viewYMax=-60;
}

// ═════════════════════════════════════════════════════════════════════════════
// Public API
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::setSquareMode(bool sq, double duty){ m_isSquare=sq; m_dutyCycle=duty; }

void PlotWidget::setPersistenceEnabled(bool on){ m_persistEnabled=on; if(!on)m_persistTraces.clear(); update(); }
void PlotWidget::setPersistenceDepth(int n){ m_persistDepth=qMax(2,n); while(m_persistTraces.size()>m_persistDepth)m_persistTraces.removeFirst(); }
void PlotWidget::clearPersistence(){ m_persistTraces.clear(); update(); }

void PlotWidget::setPhaseNoiseEnabled(bool on){ m_phaseNoiseEnabled=on; update(); }

void PlotWidget::updateWaveform(const QVector<qint16> &samples, double sampleRateHz)
{
    if(samples.isEmpty())return;
    buildTimePlot(samples,sampleRateHz);
    buildFreqPlot(samples,sampleRateHz);
    update();
}

// ── Export PNG ───────────────────────────────────────────────────────────────
void PlotWidget::exportPng(const QString &filePath)
{
    QPixmap px(size());
    render(&px);
    px.save(filePath, "PNG");
}

// ── Export CSV ───────────────────────────────────────────────────────────────
void PlotWidget::exportCsv(const QString &filePath)
{
    QFile f(filePath);
    if(!f.open(QIODevice::WriteOnly|QIODevice::Text)) return;
    QTextStream s(&f);

    // Time domain
    s << "# Time Domain\n";
    s << "Time_us,Amplitude\n";
    for(int i=0;i<m_time.xs.size();++i)
        s << m_time.xs[i] << "," << m_time.ys[i] << "\n";

    s << "\n# Frequency Domain\n";
    s << "Freq_MHz,dBFS\n";
    for(int i=0;i<m_freq.xs.size();++i)
        s << m_freq.xs[i] << "," << m_freq.ys[i] << "\n";

    if(m_phaseNoiseEnabled && !m_pn.xs.isEmpty()){
        s << "\n# Phase Noise\n";
        s << "Offset_MHz,dBc_Hz\n";
        for(int i=0;i<m_pn.xs.size();++i)
            s << m_pn.xs[i] << "," << m_pn.ys[i] << "\n";
    }
    f.close();
}

// ═════════════════════════════════════════════════════════════════════════════
// Layout
// ═════════════════════════════════════════════════════════════════════════════
QRect PlotWidget::timeWidgetRect() const
{
    if(m_viewState==TimeOnly) return rect();
    if(m_viewState==FreqOnly||m_viewState==PhaseNoiseOnly) return QRect();
    int rows = (m_phaseNoiseEnabled) ? 3 : 2;
    return QRect(0,0,width(),height()/rows);
}

QRect PlotWidget::freqWidgetRect() const
{
    if(m_viewState==FreqOnly) return rect();
    if(m_viewState==TimeOnly||m_viewState==PhaseNoiseOnly) return QRect();
    int rows=(m_phaseNoiseEnabled)?3:2;
    int rowH=height()/rows;
    return QRect(0,rowH,width(),rowH);
}

QRect PlotWidget::phaseNoiseWidgetRect() const
{
    if(!m_phaseNoiseEnabled) return QRect();
    if(m_viewState==PhaseNoiseOnly) return rect();
    if(m_viewState==TimeOnly||m_viewState==FreqOnly) return QRect();
    int rowH=height()/3;
    return QRect(0,rowH*2,width(),height()-rowH*2);
}

QRect PlotWidget::plotRect(const QRect &wr) const
{
    if(wr.isEmpty())return QRect();
    bool isFreq=(m_viewState==FreqOnly||(wr.top()>0&&m_viewState!=PhaseNoiseOnly&&wr.height()<height()*0.5+10));
    int rp=(isFreq&&m_metrics.valid)?PAD_R+MET_W+8:PAD_R;
    return QRect(wr.left()+PAD_L,wr.top()+PAD_T,wr.width()-PAD_L-rp,wr.height()-PAD_T-PAD_B);
}

QRect PlotWidget::maxBtnRect(const QRect &wr) const
{ return QRect(wr.right()-BTN_SZ-4,wr.top()+(PAD_T-BTN_SZ)/2,BTN_SZ,BTN_SZ); }

QRect PlotWidget::metricsPanelRect(const QRect &fwr) const
{ return QRect(fwr.right()-MET_W-6,fwr.top()+PAD_T+4,MET_W,fwr.height()-PAD_T-PAD_B-8); }

bool PlotWidget::hitTimePlot(const QPoint &p)  const { return plotRect(timeWidgetRect()).contains(p); }
bool PlotWidget::hitFreqPlot(const QPoint &p)  const { return plotRect(freqWidgetRect()).contains(p); }
bool PlotWidget::hitPNPlot(const QPoint &p)    const { return plotRect(phaseNoiseWidgetRect()).contains(p); }
bool PlotWidget::hitTimeMaxBtn(const QPoint &p) const { QRect w=timeWidgetRect();  return !w.isEmpty()&&maxBtnRect(w).contains(p); }
bool PlotWidget::hitFreqMaxBtn(const QPoint &p) const { QRect w=freqWidgetRect();  return !w.isEmpty()&&maxBtnRect(w).contains(p); }
bool PlotWidget::hitPNMaxBtn(const QPoint &p)   const { QRect w=phaseNoiseWidgetRect(); return !w.isEmpty()&&maxBtnRect(w).contains(p); }

// ═════════════════════════════════════════════════════════════════════════════
// Data builders
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::buildTimePlot(const QVector<qint16> &s, double rateHz)
{
    int N=s.size(); m_time.xs.resize(N); m_time.ys.resize(N);
    double tScale=1e6/rateHz,yMin=1e9,yMax=-1e9;
    for(int i=0;i<N;++i){ m_time.xs[i]=i*tScale; m_time.ys[i]=s[i]; if(s[i]<yMin)yMin=s[i]; if(s[i]>yMax)yMax=s[i]; }
    double pad=(yMax-yMin)*0.05;
    m_time.dataXMin=0; m_time.dataXMax=(N-1)*tScale;
    m_time.dataYMin=yMin-pad; m_time.dataYMax=yMax+pad;
    if(m_time.dataYMin==m_time.dataYMax){m_time.dataYMin-=1;m_time.dataYMax+=1;}
    m_time.viewXMin=m_time.dataXMin; m_time.viewXMax=m_time.dataXMax;
    m_time.viewYMin=m_time.dataYMin; m_time.viewYMax=m_time.dataYMax;
    m_time.peaks.clear();
}

void PlotWidget::buildFreqPlot(const QVector<qint16> &s, double rateHz)
{
    QVector<double> freqs;
    QVector<double> db=hammingFFT_dBFS(s,rateHz,freqs);
    m_freq.xs=freqs; m_freq.ys=db;
    m_freq.dataXMin=0; m_freq.dataXMax=freqs.isEmpty()?1.0:freqs.last();
    double yMin=1e9,yMax=-1e9;
    for(double v:db){if(v<yMin)yMin=v;if(v>yMax)yMax=v;}
    m_freq.dataYMin=yMin-5.0; m_freq.dataYMax=yMax+5.0;
    if(m_freq.dataYMin==m_freq.dataYMax){m_freq.dataYMin-=1;m_freq.dataYMax+=1;}
    m_freq.viewXMin=m_freq.dataXMin; m_freq.viewXMax=m_freq.dataXMax;
    m_freq.viewYMin=m_freq.dataYMin; m_freq.viewYMax=m_freq.dataYMax;
    findPeaks(m_freq);
    computeMetrics(db,freqs,rateHz);

    // Persistence: push current spectrum
    if(m_persistEnabled){
        m_persistTraces.append(db);
        while(m_persistTraces.size()>m_persistDepth)
            m_persistTraces.removeFirst();
    }

    // Phase noise
    if(m_phaseNoiseEnabled)
        buildPhaseNoise(db,freqs,rateHz);
}

// ── Phase noise builder ───────────────────────────────────────────────────────
void PlotWidget::buildPhaseNoise(const QVector<double> &db,
                                  const QVector<double> &freqsMHz,
                                  double rateHz)
{
    if(db.size()<4||m_metrics.fundFreq<=0) return;

    // Frequency resolution (bin width in Hz)
    double binWidthHz = rateHz / (2.0*(db.size()-1));

    // Find fundamental bin
    int fundIdx=1;
    for(int i=2;i<db.size();++i) if(db[i]>db[fundIdx]) fundIdx=i;

    // SSB phase noise: L(f) = S_phi(f)/2 = (P_sideband / P_carrier) / RBW
    // = dBFS_bin - dBFS_carrier - 10*log10(binWidthHz)  [dBc/Hz]
    double normFactor = 10.0*std::log10(binWidthHz);
    double fundPow    = db[fundIdx];

    // Collect offset points (skip within ±10 bins of fundamental)
    QVector<double> offsets, pn;
    const int SKIP=10;
    for(int i=1;i<db.size();++i){
        if(std::abs(i-fundIdx)<=SKIP) continue;
        double offsetHz  = std::abs(freqsMHz[i]-m_metrics.fundFreq)*1e6;
        if(offsetHz<=0) continue;
        double offsetMHz = offsetHz/1e6;
        double lf        = db[i] - fundPow - normFactor;   // dBc/Hz
        offsets.append(std::log10(offsetMHz));  // log-frequency axis
        pn.append(lf);
    }

    if(offsets.isEmpty()) return;

    m_pn.xs = offsets;
    m_pn.ys = pn;

    double xMin=*std::min_element(offsets.begin(),offsets.end());
    double xMax=*std::max_element(offsets.begin(),offsets.end());
    double yMin=*std::min_element(pn.begin(),pn.end())-5;
    double yMax=*std::max_element(pn.begin(),pn.end())+5;

    m_pn.dataXMin=m_pn.viewXMin=xMin; m_pn.dataXMax=m_pn.viewXMax=xMax;
    m_pn.dataYMin=m_pn.viewYMin=yMin; m_pn.dataYMax=m_pn.viewYMax=yMax;
}

QVector<double> PlotWidget::hammingFFT_dBFS(const QVector<qint16>&s,double rateHz,QVector<double>&freqsMHz)
{
    int N=s.size(); constexpr double ref=32768.0;
    std::vector<std::complex<double>>cx(N); double winSum=0;
    for(int i=0;i<N;++i){double w=0.54-0.46*std::cos(2.0*M_PI*i/(N-1));cx[i]={s[i]*w,0.0};winSum+=w;}
    auto spec=doFFT(cx); int half=N/2+1;
    QVector<double>db(half); freqsMHz.resize(half); double scale=2.0/winSum;
    for(int i=0;i<half;++i){double mag=std::abs(spec[i])*scale;db[i]=20.0*std::log10(std::max(mag/ref,1e-12));freqsMHz[i]=(double)i*rateHz/(N*1e6);}
    return db;
}

// ── Peak detection ────────────────────────────────────────────────────────────
void PlotWidget::findPeaks(PlotData &d)
{
    d.peaks.clear(); int N=d.ys.size(); if(N<3)return;
    const int MD=5; const double MP=6.0; const int MX=1;
    struct C{int idx;double val;};
    QVector<C>cands;
    for(int i=MD;i<N-MD;++i){
        bool ok=true; for(int k=i-MD;k<=i+MD&&ok;++k)if(k!=i&&d.ys[k]>=d.ys[i])ok=false;
        if(!ok)continue;
        int lo=std::max(0,i-20),hi=std::min(N-1,i+20);
        double lm=*std::min_element(d.ys.begin()+lo,d.ys.begin()+i);
        double rm=*std::min_element(d.ys.begin()+i+1,d.ys.begin()+hi+1);
        if(d.ys[i]-std::max(lm,rm)<MP)continue;
        cands.append({i,d.ys[i]});
    }
    std::sort(cands.begin(),cands.end(),[](const C&a,const C&b){return a.val>b.val;});
    if(cands.size()>MX)cands.resize(MX);
    for(const auto&c:cands){Peak pk;pk.x=d.xs[c.idx];pk.y=c.val;pk.label=QString("%1 MHz\n%2 dBFS").arg(pk.x,0,'f',2).arg(pk.y,0,'f',1);d.peaks.append(pk);}
}

// ═════════════════════════════════════════════════════════════════════════════
// RF Metrics (same as before — omitted for brevity, paste from previous version)
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::computeMetrics(const QVector<double>&db,const QVector<double>&freqsMHz,double sampleRateHz)
{
    m_metrics=RfMetrics(); m_metrics.isSquare=m_isSquare;
    int N=db.size(); if(N<4)return;
    const int GUARD=3; const double nyq=sampleRateHz/2.0/1e6;
    int fundIdx=1; for(int i=2;i<N;++i)if(db[i]>db[fundIdx])fundIdx=i;
    m_metrics.fundFreq=freqsMHz[fundIdx]; m_metrics.fundPow=db[fundIdx];
    auto findHBin=[&](double tgt)->int{
        int best=-1;double bd=1e9;for(int i=1;i<N;++i){double d=std::abs(freqsMHz[i]-tgt);if(d<bd){bd=d;best=i;}}
        if(best<0)return -1;int lo=std::max(1,best-5),hi=std::min(N-1,best+5),pk=lo;
        for(int i=lo+1;i<=hi;++i)if(db[i]>db[pk])pk=i;return pk;
    };
    double totPow=0;for(int i=1;i<N;++i)totPow+=std::pow(10.0,db[i]/10.0);
    if(m_isSquare){
        double sigPow=0,lastFreq=m_metrics.fundFreq;
        const double nf=*std::min_element(db.begin()+1,db.end())+3.0;
        for(int k=1;k<=19;k+=2){
            double tgt=k*m_metrics.fundFreq; if(tgt>nyq)tgt=std::abs(2*nyq-tgt);
            int bin=findHBin(tgt); if(bin<0)continue;
            double ideal=m_metrics.fundPow+20.0*std::log10(1.0/k);
            m_metrics.harmonicFreqs.append(freqsMHz[bin]); m_metrics.harmonicPows.append(db[bin]); m_metrics.idealHarmonicPows.append(ideal);
            sigPow+=std::pow(10.0,db[bin]/10.0);
            if(db[bin]>nf)lastFreq=freqsMHz[bin];
        }
        m_metrics.bandwidth=lastFreq;
        double evenDist=0;
        for(int k=2;k<=10;k+=2){double tgt=k*m_metrics.fundFreq;if(tgt>nyq)tgt=std::abs(2*nyq-tgt);int bin=findHBin(tgt);if(bin>=0)evenDist+=std::pow(10.0,db[bin]/10.0);}
        double noisePow=totPow-sigPow; if(noisePow>0)m_metrics.snr=10.0*std::log10(sigPow/noisePow);
        double fp=std::pow(10.0,m_metrics.fundPow/10.0); if(fp>0&&evenDist>0)m_metrics.thd=10.0*std::log10(evenDist/fp);
        double maxSpur=-1e9;
        for(int i=1;i<N;++i){
            if(std::abs(i-fundIdx)<=GUARD)continue;
            bool odd=false;for(int k=3;k<=19;k+=2){double tgt=k*m_metrics.fundFreq;if(tgt>nyq)tgt=std::abs(2*nyq-tgt);int hb=findHBin(tgt);if(hb>=0&&std::abs(i-hb)<=GUARD){odd=true;break;}}
            if(odd)continue; if(db[i]>maxSpur)maxSpur=db[i];
        }
        m_metrics.sfdr=m_metrics.fundPow-maxSpur;
        if(m_metrics.harmonicFreqs.size()>=2){
            double sx=0,sy=0,sxy=0,sx2=0;int nh=m_metrics.harmonicPows.size();
            for(int i=0;i<nh;++i){double x=std::log10(2*i+1),y=m_metrics.harmonicPows[i]-m_metrics.fundPow;sx+=x;sy+=y;sxy+=x*y;sx2+=x*x;}
            m_metrics.enob=(nh*sxy-sx*sy)/(nh*sx2-sx*sx);
        }
    } else {
        double sigPow=0;
        for(int i=1;i<N;++i){double lin=std::pow(10.0,db[i]/10.0);if(std::abs(i-fundIdx)<=GUARD)sigPow+=lin;}
        double noisePow=totPow-sigPow; if(noisePow>0)m_metrics.snr=10.0*std::log10(sigPow/noisePow);
        double hd=0;
        for(int k=2;k<=5;++k){double tgt=k*m_metrics.fundFreq;while(tgt>nyq)tgt=std::abs(2*nyq-tgt);int bin=findHBin(tgt);if(bin<0)continue;m_metrics.harmonicFreqs.append(freqsMHz[bin]);m_metrics.harmonicPows.append(db[bin]);m_metrics.idealHarmonicPows.append(0.0);hd+=std::pow(10.0,db[bin]/10.0);}
        if(sigPow>0&&hd>0)m_metrics.thd=10.0*std::log10(hd/sigPow);
        double ms=-1e9;for(int i=1;i<N;++i){if(std::abs(i-fundIdx)<=GUARD)continue;if(db[i]>ms)ms=db[i];}
        m_metrics.sfdr=m_metrics.fundPow-ms;
        double sinad=0;if((totPow-sigPow)>0)sinad=10.0*std::log10(sigPow/(totPow-sigPow));
        m_metrics.enob=(sinad-1.76)/6.02;
    }
    m_metrics.valid=true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Mouse events
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    if(event->button()!=Qt::LeftButton)return;
    if(hitTimeMaxBtn(event->pos())){m_viewState=(m_viewState==TimeOnly)?BothPlots:TimeOnly;update();return;}
    if(hitFreqMaxBtn(event->pos())){m_viewState=(m_viewState==FreqOnly)?BothPlots:FreqOnly;update();return;}
    if(hitPNMaxBtn(event->pos())){m_viewState=(m_viewState==PhaseNoiseOnly)?BothPlots:PhaseNoiseOnly;update();return;}
    bool it=hitTimePlot(event->pos()),ifq=hitFreqPlot(event->pos());
    if(it||ifq){m_selecting=true;m_selStart=m_selEnd=event->pos();m_selectIsTime=it;setCursor(Qt::CrossCursor);}
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos=event->pos();m_mouseInWidget=true;
    if(m_selecting)m_selEnd=event->pos();
    bool ob=hitTimeMaxBtn(event->pos())||hitFreqMaxBtn(event->pos())||hitPNMaxBtn(event->pos());
    setCursor(m_selecting?Qt::CrossCursor:ob?Qt::PointingHandCursor:Qt::ArrowCursor);
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button()!=Qt::LeftButton||!m_selecting)return;
    m_selecting=false;setCursor(Qt::ArrowCursor);
    PlotData&d=m_selectIsTime?m_time:m_freq;
    QRect wr=m_selectIsTime?timeWidgetRect():freqWidgetRect();
    QRect plot=plotRect(wr);
    int x1=qMin(m_selStart.x(),m_selEnd.x()),x2=qMax(m_selStart.x(),m_selEnd.x());
    int y1=qMin(m_selStart.y(),m_selEnd.y()),y2=qMax(m_selStart.y(),m_selEnd.y());
    if((x2-x1)<5||(y2-y1)<5){update();return;}
    x1=qMax(x1,plot.left());x2=qMin(x2,plot.right());y1=qMax(y1,plot.top());y2=qMin(y2,plot.bottom());
    double xr=d.viewXMax-d.viewXMin,yr=d.viewYMax-d.viewYMin;
    d.viewXMin=d.viewXMin+(double)(x1-plot.left())/plot.width()*xr;
    d.viewXMax=d.viewXMin+(double)(x2-x1)/plot.width()*xr;
    double ny=d.viewYMax-(double)(y1-plot.top())/plot.height()*yr;
    d.viewYMin=d.viewYMax-(double)(y2-plot.top())/plot.height()*yr;
    d.viewYMax=ny;
    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    auto r=[](PlotData&d){d.viewXMin=d.dataXMin;d.viewXMax=d.dataXMax;d.viewYMin=d.dataYMin;d.viewYMax=d.dataYMax;};
    if(hitTimePlot(event->pos()))r(m_time);
    else if(hitFreqPlot(event->pos()))r(m_freq);
    else if(hitPNPlot(event->pos()))r(m_pn);
    update();
}

void PlotWidget::leaveEvent(QEvent*){m_mouseInWidget=false;update();}

// ═════════════════════════════════════════════════════════════════════════════
// paintEvent
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(),QColor(COL_BG));
    QRect twr=timeWidgetRect(),fwr=freqWidgetRect(),pwr=phaseNoiseWidgetRect();
    if(!twr.isEmpty())drawPlot(p,twr,m_time);
    if(!fwr.isEmpty()){drawPlot(p,fwr,m_freq);if(m_metrics.valid){drawMetricsPanel(p,fwr);drawHarmonicMarkers(p,plotRect(fwr));}}
    if(!pwr.isEmpty())drawPhaseNoisePlot(p,pwr);
}

// ═════════════════════════════════════════════════════════════════════════════
// drawPlot
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::drawPlot(QPainter&p,const QRect&wr,PlotData&d) const
{
    QRect plot=plotRect(wr); if(plot.width()<=0||plot.height()<=0)return;
    p.fillRect(wr,QColor(COL_BG)); p.fillRect(plot,QColor(COL_PLOTBG));
    bool zoomed=(d.viewXMin>d.dataXMin+1e-9||d.viewXMax<d.dataXMax-1e-9||d.viewYMin>d.dataYMin+1e-9||d.viewYMax<d.dataYMax-1e-9);
    QString title=d.title+(zoomed?"  [dbl-click: reset]":"");
    bool isPersist=(&d==&m_freq)&&m_persistEnabled&&!m_persistTraces.isEmpty();
    if(isPersist) title+="  [persist:"+QString::number(m_persistTraces.size())+"]";
    p.setPen(QColor(COL_TITLE));QFont tf=p.font();tf.setPixelSize(12);tf.setBold(true);p.setFont(tf);
    p.drawText(QRect(wr.left(),wr.top(),wr.width()-BTN_SZ-8,PAD_T),Qt::AlignHCenter|Qt::AlignVCenter,title);
    bool isMax=(&d==&m_time)?(m_viewState==TimeOnly):(m_viewState==FreqOnly);
    drawMaxButton(p,wr,isMax);
    const int NX=5,NY=4;
    p.setPen(QPen(QColor(COL_GRID),1,Qt::DotLine));
    for(int i=0;i<=NX;++i){int x=plot.left()+i*plot.width()/NX;p.drawLine(x,plot.top(),x,plot.bottom());}
    for(int j=0;j<=NY;++j){int y=plot.top()+j*plot.height()/NY;p.drawLine(plot.left(),y,plot.right(),y);}
    QFont af=p.font();af.setPixelSize(9);af.setBold(false);p.setFont(af); p.setPen(QColor(COL_AXIS));
    for(int j=0;j<=NY;++j){double val=d.viewYMax-j*(d.viewYMax-d.viewYMin)/NY;int y=plot.top()+j*plot.height()/NY;p.drawText(QRect(wr.left(),y-9,PAD_L-3,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(val,'f',0));}
    for(int i=0;i<=NX;++i){double val=d.viewXMin+i*(d.viewXMax-d.viewXMin)/NX;int x=plot.left()+i*plot.width()/NX;p.drawText(QRect(x-24,plot.bottom()+2,48,14),Qt::AlignHCenter,QString::number(val,'f',1));}
    p.drawText(QRect(plot.left(),plot.bottom()+18,plot.width(),14),Qt::AlignHCenter,d.xLabel);
    p.save();p.translate(wr.left()+11,plot.top()+plot.height()/2);p.rotate(-90);p.drawText(QRect(-40,-6,80,13),Qt::AlignHCenter,d.yLabel);p.restore();
    if(d.xs.isEmpty())return;
    double xr=d.viewXMax-d.viewXMin;if(xr==0)xr=1;
    double yr=d.viewYMax-d.viewYMin;if(yr==0)yr=1;
    int N=d.xs.size(),iS=0,iE=N-1;
    for(int i=0;i<N;++i)if(d.xs[i]>=d.viewXMin){iS=i;break;}
    for(int i=N-1;i>=0;--i)if(d.xs[i]<=d.viewXMax){iE=i;break;}
    int step=std::max(1,(iE-iS+1)/plot.width());
    p.setClipRect(plot);
    // Persistence traces first (behind main)
    if(isPersist) drawPersistence(p,plot);
    p.setPen(QPen(d.lineColor,1.2f));
    QPainterPath path;bool first=true;
    for(int i=iS;i<=iE;i+=step){double px=plot.left()+(d.xs[i]-d.viewXMin)/xr*plot.width();double py=plot.bottom()-(d.ys[i]-d.viewYMin)/yr*plot.height();if(first){path.moveTo(px,py);first=false;}else path.lineTo(px,py);}
    p.drawPath(path);
    drawPeaks(p,plot,d); drawRubberBand(p,plot,d); p.setClipping(false); drawCrosshair(p,plot,d);
    p.setPen(QPen(QColor(COL_AXIS),1));p.setBrush(Qt::NoBrush);p.drawRect(plot);
}

// ── Persistence overlay ───────────────────────────────────────────────────────
void PlotWidget::drawPersistence(QPainter &p, const QRect &plot) const
{
    if(m_persistTraces.isEmpty()||m_freq.xs.isEmpty())return;
    double xr=m_freq.viewXMax-m_freq.viewXMin;if(xr==0)xr=1;
    double yr=m_freq.viewYMax-m_freq.viewYMin;if(yr==0)yr=1;

    // Older traces more transparent, newest most opaque
    int nT=m_persistTraces.size();
    for(int t=0;t<nT;++t){
        const QVector<double>&tr=m_persistTraces[t];
        // alpha increases from old to new
        int alpha=std::max(8,60*t/std::max(1,nT-1)+8);
        QColor col=QColor(COL_PERSIST); col.setAlpha(alpha);
        p.setPen(QPen(col,1.0));
        int N=tr.size(); if(N==0)continue;
        int step=std::max(1,N/plot.width());
        QPainterPath path;bool first=true;
        for(int i=0;i<N;i+=step){
            if(i>=(int)m_freq.xs.size())break;
            double px=plot.left()+(m_freq.xs[i]-m_freq.viewXMin)/xr*plot.width();
            double py=plot.bottom()-(tr[i]-m_freq.viewYMin)/yr*plot.height();
            if(first){path.moveTo(px,py);first=false;}else path.lineTo(px,py);
        }
        p.drawPath(path);
    }
}

// ── Phase noise plot ──────────────────────────────────────────────────────────
void PlotWidget::drawPhaseNoisePlot(QPainter &p, const QRect &wr) const
{
    QRect plot=plotRect(wr); if(plot.width()<=0||plot.height()<=0)return;
    p.fillRect(wr,QColor(COL_BG)); p.fillRect(plot,QColor(COL_PLOTBG));

    // Title
    p.setPen(QColor(COL_TITLE));QFont tf=p.font();tf.setPixelSize(12);tf.setBold(true);p.setFont(tf);
    p.drawText(QRect(wr.left(),wr.top(),wr.width()-BTN_SZ-8,PAD_T),Qt::AlignHCenter|Qt::AlignVCenter,m_pn.title);
    drawMaxButton(p,wr,m_viewState==PhaseNoiseOnly);

    // Grid
    const int NX=6,NY=4;
    p.setPen(QPen(QColor(COL_GRID),1,Qt::DotLine));
    for(int i=0;i<=NX;++i){int x=plot.left()+i*plot.width()/NX;p.drawLine(x,plot.top(),x,plot.bottom());}
    for(int j=0;j<=NY;++j){int y=plot.top()+j*plot.height()/NY;p.drawLine(plot.left(),y,plot.right(),y);}

    // Axis labels — X is log(MHz)
    QFont af=p.font();af.setPixelSize(9);af.setBold(false);p.setFont(af);p.setPen(QColor(COL_AXIS));
    double xMin=m_pn.viewXMin,xMax=m_pn.viewXMax;
    double yMin=m_pn.viewYMin,yMax=m_pn.viewYMax;
    for(int j=0;j<=NY;++j){
        double val=yMax-j*(yMax-yMin)/NY;int y=plot.top()+j*plot.height()/NY;
        p.drawText(QRect(wr.left(),y-9,PAD_L-3,18),Qt::AlignRight|Qt::AlignVCenter,QString::number((int)val));
    }
    for(int i=0;i<=NX;++i){
        double logVal=xMin+i*(xMax-xMin)/NX;
        double mhz=std::pow(10.0,logVal);
        QString label=(mhz>=1)?QString::number(mhz,'f',0)+"M":QString::number(mhz*1000,'f',0)+"k";
        int x=plot.left()+i*plot.width()/NX;
        p.drawText(QRect(x-20,plot.bottom()+2,40,14),Qt::AlignHCenter,label);
    }
    p.drawText(QRect(plot.left(),plot.bottom()+18,plot.width(),14),Qt::AlignHCenter,"Offset Frequency");
    p.save();p.translate(wr.left()+11,plot.top()+plot.height()/2);p.rotate(-90);
    p.drawText(QRect(-42,-6,84,13),Qt::AlignHCenter,"dBc/Hz");p.restore();

    if(m_pn.xs.isEmpty()){
        p.drawText(plot,Qt::AlignCenter,"No phase noise data\n(acquire a signal first)");
        p.setPen(QPen(QColor(COL_AXIS),1));p.drawRect(plot);return;
    }

    // Draw trace
    double xr=xMax-xMin;if(xr==0)xr=1;
    double yr=yMax-yMin;if(yr==0)yr=1;
    int N=m_pn.xs.size();
    p.setPen(QPen(QColor(COL_PN),1.5f));
    p.setClipRect(plot);
    QPainterPath path;bool first=true;
    for(int i=0;i<N;++i){
        if(m_pn.xs[i]<xMin||m_pn.xs[i]>xMax)continue;
        double px=plot.left()+(m_pn.xs[i]-xMin)/xr*plot.width();
        double py=plot.bottom()-(m_pn.ys[i]-yMin)/yr*plot.height();
        if(first){path.moveTo(px,py);first=false;}else path.lineTo(px,py);
    }
    p.drawPath(path);
    p.setClipping(false);

    // Crosshair on phase noise plot
    if(m_mouseInWidget&&plot.contains(m_mousePos)){
        double dataX=xMin+(double)(m_mousePos.x()-plot.left())/plot.width()*xr;
        double dataY=yMax-(double)(m_mousePos.y()-plot.top())/plot.height()*yr;
        double offsetMHz=std::pow(10.0,dataX);
        p.setPen(QPen(QColor(COL_CROSS),1,Qt::DashLine));
        p.drawLine(m_mousePos.x(),plot.top(),m_mousePos.x(),plot.bottom());
        p.drawLine(plot.left(),m_mousePos.y(),plot.right(),m_mousePos.y());
        QString tip=QString("Offset: %1 MHz   %2 dBc/Hz").arg(offsetMHz,0,'f',3).arg(dataY,0,'f',1);
        QFont rf=p.font();rf.setPixelSize(10);p.setFont(rf);QFontMetrics fm(rf);
        int rw=fm.horizontalAdvance(tip)+10,rh=fm.height()+6;
        int rx=m_mousePos.x()+12,ry=m_mousePos.y()-rh-4;
        if(rx+rw>plot.right())rx=m_mousePos.x()-rw-4;
        if(ry<plot.top())ry=m_mousePos.y()+8;
        p.setPen(Qt::NoPen);p.setBrush(QColor(30,30,46,210));p.drawRoundedRect(rx,ry,rw,rh,3,3);
        p.setPen(QColor(COL_AXIS));p.drawText(rx+5,ry+fm.ascent()+3,tip);
    }

    p.setPen(QPen(QColor(COL_AXIS),1));p.setBrush(Qt::NoBrush);p.drawRect(plot);
}

// ═════════════════════════════════════════════════════════════════════════════
// Metrics panel
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::drawMetricsPanel(QPainter&p,const QRect&fwr) const
{
    QRect panel=metricsPanelRect(fwr); if(panel.width()<=0||panel.height()<=0)return;
    p.setPen(QPen(QColor(COL_AXIS),1));p.setBrush(QColor(COL_METBG));p.drawRoundedRect(panel,5,5);
    QFont hf=p.font();hf.setPixelSize(10);hf.setBold(true);p.setFont(hf);p.setPen(QColor(COL_TITLE));
    p.drawText(QRect(panel.left(),panel.top()+4,panel.width(),16),Qt::AlignHCenter,"RF Metrics");
    p.setPen(QPen(QColor(COL_GRID),1));p.drawLine(panel.left()+6,panel.top()+22,panel.right()-6,panel.top()+22);
    struct Row{QString label,value;QColor col;};
    QVector<Row>rows;
    bool sq=m_metrics.isSquare;
    rows.append({"Fund",QString("%1 MHz").arg(m_metrics.fundFreq,0,'f',2),QColor(COL_PEAK)});
    rows.append({"Pwr", QString("%1 dBFS").arg(m_metrics.fundPow,0,'f',1),QColor(COL_AXIS)});
    rows.append({"SNR", QString("%1 dB").arg(m_metrics.snr,0,'f',1),QColor(COL_FREQ)});
    rows.append({"SFDR",QString("%1 dBc").arg(m_metrics.sfdr,0,'f',1),QColor(COL_FREQ)});
    if(sq){
        rows.append({"THD(e)",QString("%1 dBc").arg(m_metrics.thd,0,'f',1),QColor(COL_AXIS)});
        rows.append({"Slope",QString("%1 dB/dec").arg(m_metrics.enob,0,'f',1),QColor(COL_TIME)});
        rows.append({"Ideal","-20.0 dB/dec",QColor(0x88,0x88,0x88)});
        rows.append({"BW",QString("%1 MHz").arg(m_metrics.bandwidth,0,'f',0),QColor(COL_TIME)});
        for(int k=0;k<m_metrics.harmonicFreqs.size();++k){
            double delta=m_metrics.harmonicPows[k]-m_metrics.idealHarmonicPows[k];
            QString ds=(delta>=0?"+":"")+QString::number(delta,'f',1)+"dB";
            rows.append({QString("H%1").arg(2*k+1),
                         QString("%1M %2(%3)").arg(m_metrics.harmonicFreqs[k],0,'f',0).arg(m_metrics.harmonicPows[k],0,'f',1).arg(ds),
                         QColor(COL_HARM)});
        }
    } else {
        rows.append({"THD",QString("%1 dBc").arg(m_metrics.thd,0,'f',1),QColor(COL_AXIS)});
        rows.append({"ENOB",QString("%1 bits").arg(m_metrics.enob,0,'f',2),QColor(COL_TIME)});
        for(int k=0;k<m_metrics.harmonicFreqs.size();++k)
            rows.append({QString("H%1").arg(k+2),
                         QString("%1M %2dBFS").arg(m_metrics.harmonicFreqs[k],0,'f',0).arg(m_metrics.harmonicPows[k],0,'f',1),
                         QColor(COL_HARM)});
    }
    QFont lf=p.font();lf.setPixelSize(9);lf.setBold(false);
    QFont vf=p.font();vf.setPixelSize(9);vf.setBold(true);
    int y=panel.top()+28;const int RH=17;
    for(const Row&row:rows){
        if(y+RH>panel.bottom()-4)break;
        p.setFont(lf);p.setPen(QColor(0xA6,0xAD,0xC8));
        p.drawText(QRect(panel.left()+4,y,50,RH),Qt::AlignLeft|Qt::AlignVCenter,row.label);
        p.setFont(vf);p.setPen(row.col);
        p.drawText(QRect(panel.left()+54,y,panel.width()-58,RH),Qt::AlignLeft|Qt::AlignVCenter,row.value);
        p.setPen(QPen(QColor(COL_GRID),1));p.drawLine(panel.left()+4,y+RH-1,panel.right()-4,y+RH-1);
        y+=RH;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Harmonic markers
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::drawHarmonicMarkers(QPainter&p,const QRect&plot) const
{
    if(!m_metrics.valid||plot.width()<=0)return;
    double xr=m_freq.viewXMax-m_freq.viewXMin;if(xr==0)xr=1;
    double yr=m_freq.viewYMax-m_freq.viewYMin;if(yr==0)yr=1;
    p.setClipRect(plot);
    QFont hf=p.font();hf.setPixelSize(8);hf.setBold(true);p.setFont(hf);
    for(int k=0;k<m_metrics.harmonicFreqs.size();++k){
        double freq=m_metrics.harmonicFreqs[k],pow=m_metrics.harmonicPows[k];
        if(freq<m_freq.viewXMin||freq>m_freq.viewXMax)continue;
        int px=plot.left()+(int)((freq-m_freq.viewXMin)/xr*plot.width());
        int py=plot.bottom()-(int)((pow-m_freq.viewYMin)/yr*plot.height());
        p.setPen(QPen(QColor(COL_HARM),1.5));p.setBrush(QColor(COL_HARM));
        const int T=6;QPolygon tri;
        tri<<QPoint(px-T,py-T*2)<<QPoint(px+T,py-T*2)<<QPoint(px,py);
        p.drawPolygon(tri);
        p.setPen(QColor(COL_HARM));p.setBrush(Qt::NoBrush);
        int harmNum=m_metrics.isSquare?(2*k+1):(k+2);
        p.drawText(px-8,py-T*2-2,QString("H%1").arg(harmNum));
    }
    p.setClipping(false);p.setBrush(Qt::NoBrush);
}

// ═════════════════════════════════════════════════════════════════════════════
// Maximize button, Peaks, Rubber-band, Crosshair (unchanged helpers)
// ═════════════════════════════════════════════════════════════════════════════
void PlotWidget::drawMaxButton(QPainter&p,const QRect&wr,bool isMax)const
{
    QRect btn=maxBtnRect(wr);bool hov=btn.contains(m_mousePos)&&m_mouseInWidget;
    p.setPen(Qt::NoPen);p.setBrush(QColor(hov?COL_BTNHOV:COL_BTN));p.drawRoundedRect(btn,3,3);
    p.setPen(QPen(QColor(COL_AXIS),1.5));p.setBrush(Qt::NoBrush);
    int m=4;QRect in=btn.adjusted(m,m,-m,-m);
    if(isMax){p.drawLine(in.left(),in.top(),in.left()+4,in.top());p.drawLine(in.left(),in.top(),in.left(),in.top()+4);p.drawLine(in.right(),in.bottom(),in.right()-4,in.bottom());p.drawLine(in.right(),in.bottom(),in.right(),in.bottom()-4);p.drawLine(in.left()+1,in.top()+1,in.right()-1,in.bottom()-1);}
    else{p.drawLine(in.left(),in.top(),in.left()+4,in.top());p.drawLine(in.left(),in.top(),in.left(),in.top()+4);p.drawLine(in.right(),in.bottom(),in.right()-4,in.bottom());p.drawLine(in.right(),in.bottom(),in.right(),in.bottom()-4);p.drawLine(in.right(),in.top(),in.right()-4,in.top());p.drawLine(in.right(),in.top(),in.right(),in.top()+4);p.drawLine(in.left(),in.bottom(),in.left()+4,in.bottom());p.drawLine(in.left(),in.bottom(),in.left(),in.bottom()-4);}
}

void PlotWidget::drawPeaks(QPainter&p,const QRect&plot,const PlotData&d)const
{
    if(d.peaks.isEmpty())return;
    double xr=d.viewXMax-d.viewXMin;if(xr==0)xr=1;double yr=d.viewYMax-d.viewYMin;if(yr==0)yr=1;
    QFont pf=p.font();pf.setPixelSize(9);pf.setBold(true);p.setFont(pf);QFontMetrics fm(pf);
    for(const Peak&pk:d.peaks){
        if(pk.x<d.viewXMin||pk.x>d.viewXMax||pk.y<d.viewYMin||pk.y>d.viewYMax)continue;
        int px=plot.left()+(int)((pk.x-d.viewXMin)/xr*plot.width());
        int py=plot.bottom()-(int)((pk.y-d.viewYMin)/yr*plot.height());
        p.setPen(QPen(QColor(COL_PEAK),1.5));p.setBrush(QColor(COL_PEAK));
        const int D=5;QPolygon di;di<<QPoint(px,py-D)<<QPoint(px+D,py)<<QPoint(px,py+D)<<QPoint(px-D,py);p.drawPolygon(di);
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(COL_PEAK),1,Qt::DashLine));p.drawLine(px,py+D,px,plot.bottom());
        QStringList lines=pk.label.split('\n');int bw=0,bh=4;
        for(const QString&l:lines){bw=std::max(bw,fm.horizontalAdvance(l)+10);bh+=fm.height();}
        int bx=px-bw/2,by=py-D-bh-4;
        if(bx<plot.left())bx=plot.left();if(bx+bw>plot.right())bx=plot.right()-bw;if(by<plot.top())by=py+D+4;
        p.setPen(Qt::NoPen);p.setBrush(QColor(0x88,0,0,210));p.drawRoundedRect(bx,by,bw,bh,3,3);
        p.setPen(QColor(COL_PEAK));p.setBrush(Qt::NoBrush);int ly=by+2+fm.ascent();
        for(const QString&l:lines){p.drawText(bx+5,ly,l);ly+=fm.height();}
    }p.setBrush(Qt::NoBrush);
}

void PlotWidget::drawRubberBand(QPainter&p,const QRect&plot,const PlotData&d)const
{
    if(!m_selecting||(m_selectIsTime?(&d!=&m_time):(&d!=&m_freq)))return;
    int x1=qMax(qMin(m_selStart.x(),m_selEnd.x()),plot.left()),x2=qMin(qMax(m_selStart.x(),m_selEnd.x()),plot.right());
    int y1=qMax(qMin(m_selStart.y(),m_selEnd.y()),plot.top()),y2=qMin(qMax(m_selStart.y(),m_selEnd.y()),plot.bottom());
    p.setPen(QPen(Qt::white,1,Qt::DashLine));p.setBrush(QColor(255,255,255,30));p.drawRect(x1,y1,x2-x1,y2-y1);
}

void PlotWidget::drawCrosshair(QPainter&p,const QRect&plot,const PlotData&d)const
{
    if(!m_mouseInWidget||!plot.contains(m_mousePos))return;
    double xr=d.viewXMax-d.viewXMin;if(xr==0)xr=1;double yr=d.viewYMax-d.viewYMin;if(yr==0)yr=1;
    double dx=d.viewXMin+(double)(m_mousePos.x()-plot.left())/plot.width()*xr;
    double dy=d.viewYMax-(double)(m_mousePos.y()-plot.top())/plot.height()*yr;
    p.setPen(QPen(QColor(COL_CROSS),1,Qt::DashLine));
    p.drawLine(m_mousePos.x(),plot.top(),m_mousePos.x(),plot.bottom());
    p.drawLine(plot.left(),m_mousePos.y(),plot.right(),m_mousePos.y());
    QString r=QString("X:%1 Y:%2").arg(dx,0,'f',3).arg(dy,0,'f',1);
    QFont rf=p.font();rf.setPixelSize(10);rf.setBold(false);p.setFont(rf);QFontMetrics fm(rf);
    int rw=fm.horizontalAdvance(r)+10,rh=fm.height()+6;
    int rx=m_mousePos.x()+12,ry=m_mousePos.y()-rh-4;
    if(rx+rw>plot.right())rx=m_mousePos.x()-rw-4;if(ry<plot.top())ry=m_mousePos.y()+8;
    p.setPen(Qt::NoPen);p.setBrush(QColor(30,30,46,210));p.drawRoundedRect(rx,ry,rw,rh,3,3);
    p.setPen(QColor(COL_AXIS));p.drawText(rx+5,ry+fm.ascent()+3,r);
}
