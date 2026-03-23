#pragma once
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>
#include "commontypes.h"

struct DataRequest {
    enum Type { SendWaveform, ReceiveCapture } type = SendWaveform;
    QByteArray command;           // ASCII command sent to board
    QByteArray payload;           // raw int16 bytes (SendWaveform only)
    int        expectedBytes = 0; // bytes to collect (ReceiveCapture only)
    double     sampleRate    = 0.0;
    int        channel       = 0;
};

class DataWorker : public QThread
{
    Q_OBJECT
public:
    explicit DataWorker(const QString &host, quint16 port, QObject *parent = nullptr);
    ~DataWorker() override;

    void enqueueRequest(const DataRequest &req);
    void stop();

    // Timeouts in milliseconds
    static constexpr int CONNECT_TIMEOUT_MS = 3000;
    static constexpr int WRITE_TIMEOUT_MS   = 2000;
    static constexpr int READ_CHUNK_MS      = 5000;
    static constexpr int CAPTURE_TOTAL_MS   = 30000;
    static constexpr int CAPTURE_SETTLE_MS  = 200;   // settle after trigger

signals:
    void captureReady(WaveformData data);
    void sendDone();
    void errorOccurred(const QString &msg);

protected:
    void run() override;

private:
    QString  m_host;
    quint16  m_port;
    bool     m_stop = false;

    QMutex              m_mutex;
    QWaitCondition      m_cond;
    QQueue<DataRequest> m_queue;
};
