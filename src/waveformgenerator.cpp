#include "waveformgenerator.h"
#include <cmath>
#include <algorithm>

QVector<qint16> WaveformGenerator::generateNormalized(
    Shape   shape,
    qint64  freqHz,
    double  thetaRad,
    double  sampleRateHz,
    int     sampleNum,
    int    &trimmedSamples,
    double  dutyCycle)
{
    QVector<double> tone(sampleNum);
    double period = sampleRateHz / static_cast<double>(freqHz);

    for (int i = 0; i < sampleNum; ++i) {
        double t = static_cast<double>(i) / sampleRateHz;

        if (shape == Shape::Sine) {
            tone[i] = std::sin(2.0 * M_PI * freqHz * t + thetaRad);
        } else {
            // Square: phase within one period [0, 1)
            double phase = std::fmod(i + thetaRad * period / (2.0 * M_PI), period) / period;
            if (phase < 0) phase += 1.0;
            tone[i] = (phase < dutyCycle) ? 1.0 : -1.0;
        }
    }

    // Normalise to int16
    double maxVal = *std::max_element(tone.begin(), tone.end());
    if (maxVal == 0.0) maxVal = 1.0;

    QVector<qint16> out(sampleNum);
    for (int i = 0; i < sampleNum; ++i)
        out[i] = static_cast<qint16>((tone[i] / maxVal) * 32767.0);

    // Find last falling edge (transition from +high to low) for clean trim
    trimmedSamples = sampleNum;
    if (shape == Shape::Square) {
        // Find last complete period boundary
        int samplesPerPeriod = static_cast<int>(std::round(period));
        if (samplesPerPeriod > 0) {
            int nPeriods = (sampleNum - 1) / samplesPerPeriod;
            trimmedSamples = nPeriods * samplesPerPeriod;
            if (trimmedSamples == 0) trimmedSamples = samplesPerPeriod;
        }
    } else {
        // Sine: find last zero crossing
        for (int i = sampleNum - 1; i >= 0; --i) {
            if (std::abs(out[i]) < 10) {
                trimmedSamples = i;
                break;
            }
        }
    }

    return out;
}

// ── Convenience wrappers ──────────────────────────────────────────────────────

QVector<qint16> WaveformGenerator::generateToneNormalized(
    qint64  freqHz,
    double  thetaRad,
    double  sampleRateHz,
    int     sampleNum,
    int    &trimmedSamples)
{
    return generateNormalized(Shape::Sine, freqHz, thetaRad,
                              sampleRateHz, sampleNum, trimmedSamples);
}

QVector<qint16> WaveformGenerator::generateSquareNormalized(
    qint64  freqHz,
    double  thetaRad,
    double  sampleRateHz,
    int     sampleNum,
    int    &trimmedSamples,
    double  dutyCycle)
{
    return generateNormalized(Shape::Square, freqHz, thetaRad,
                              sampleRateHz, sampleNum, trimmedSamples,
                              dutyCycle);
}
