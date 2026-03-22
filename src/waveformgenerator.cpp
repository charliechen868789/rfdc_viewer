#include "waveformgenerator.h"
#include <cmath>
#include <algorithm>

QVector<qint16> WaveformGenerator::generateToneNormalized(
    qint64  freqHz,
    double  thetaRad,
    double  sampleRateHz,
    int     sampleNum,
    int    &trimmedSamples)
{
    QVector<double> tone(sampleNum);

    for (int i = 0; i < sampleNum; ++i) {
        double t = static_cast<double>(i) / sampleRateHz;
        tone[i]  = std::sin(2.0 * M_PI * freqHz * t + thetaRad);
    }

    // Normalise to int16
    double maxVal = *std::max_element(tone.begin(), tone.end());
    if (maxVal == 0.0) maxVal = 1.0;

    QVector<qint16> out(sampleNum);
    for (int i = 0; i < sampleNum; ++i)
        out[i] = static_cast<qint16>((tone[i] / maxVal) * 32767.0);

    // Find last index where |sample| < 10  (zero-crossing approximation)
    trimmedSamples = sampleNum;
    for (int i = sampleNum - 1; i >= 0; --i) {
        if (std::abs(out[i]) < 10) {
            trimmedSamples = i;
            break;
        }
    }

    return out;
}
