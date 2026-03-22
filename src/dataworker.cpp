#include "dataworker.h"
#include <QTcpSocket>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QDebug>
#include <cstring>

DataWorker::DataWorker(const QString &host, quint16 port, QObject *parent)
    : QThread(parent), m_host(host), m_port(port)
{}

DataWorker::~DataWorker()
{
    stop();
    wait(5000);
}

void DataWorker::enqueueRequest(const DataRequest &req)
{
    QMutexLocker lk(&m_mutex);
    m_queue.enqueue(req);
    m_cond.wakeOne();
}

void DataWorker::stop()
{
    QMutexLocker lk(&m_mutex);
    m_stop = true;
    m_cond.wakeOne();
}

void DataWorker::run()
{
    QTcpSocket sock;

    // ── Connect once and stay connected — mirrors Python data_rx_task ─────
    while (true) {
        {
            QMutexLocker lk(&m_mutex);
            if (m_stop) return;
        }

        sock.connectToHost(m_host, m_port);
        if (!sock.waitForConnected(CONNECT_TIMEOUT_MS)) {
            emit errorOccurred(
                QString("DATA connect failed (%1 ms): %2")
                    .arg(CONNECT_TIMEOUT_MS).arg(sock.errorString()));
            msleep(2000);
            continue;
        }
        qDebug() << "DataWorker: connected to" << m_host << "port" << m_port;

        // ── Persistent session loop ───────────────────────────────────────
        while (true) {
            // Check stop / socket health first
            {
                QMutexLocker lk(&m_mutex);
                if (m_stop) {
                    sock.disconnectFromHost();
                    return;
                }
            }
            if (sock.state() != QAbstractSocket::ConnectedState) {
                qDebug() << "DataWorker: connection lost, reconnecting";
                emit errorOccurred("DATA socket disconnected");
                break; // reconnect
            }

            // Wait for a request (with timeout so we can check socket health)
            DataRequest req;
            bool haveReq = false;
            {
                QMutexLocker lk(&m_mutex);
                if (m_queue.isEmpty())
                    m_cond.wait(&m_mutex, 200);
                if (m_stop) {
                    sock.disconnectFromHost();
                    return;
                }
                if (!m_queue.isEmpty()) {
                    req     = m_queue.dequeue();
                    haveReq = true;
                }
            }
            if (!haveReq) continue;

            // ── SendWaveform ──────────────────────────────────────────────
            if (req.type == DataRequest::SendWaveform) {

                // Send command header
                sock.write(req.command);
                if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                    emit errorOccurred(
                        QString("DATA cmd write timeout (%1 ms)").arg(WRITE_TIMEOUT_MS));
                    continue;
                }
                qDebug() << "DataWorker TX cmd:" << req.command.trimmed();

                // Send raw payload
                const char *buf  = req.payload.constData();
                int          left = req.payload.size();
                bool ok = true;

                QElapsedTimer timer;
                timer.start();

                while (left > 0) {
                    qint64 written = sock.write(buf, left);
                    if (written < 0) {
                        emit errorOccurred(
                            QString("DATA payload write error: %1").arg(sock.errorString()));
                        ok = false;
                        break;
                    }
                    if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                        emit errorOccurred(
                            QString("DATA payload write timeout (%1 ms), "
                                    "%2 of %3 bytes remaining")
                                .arg(WRITE_TIMEOUT_MS).arg(left).arg(req.payload.size()));
                        ok = false;
                        break;
                    }
                    buf  += written;
                    left -= static_cast<int>(written);
                }

                if (ok) {
                    qDebug() << "DataWorker: waveform sent"
                             << req.payload.size() << "bytes in"
                             << timer.elapsed() << "ms";
                    emit sendDone();
                }

            // ── ReceiveCapture ────────────────────────────────────────────
            } else {

                // Send read command
                sock.write(req.command);
                if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                    emit errorOccurred(
                        QString("DATA RX cmd write timeout (%1 ms)").arg(WRITE_TIMEOUT_MS));
                    continue;
                }
                qDebug() << "DataWorker RX cmd:" << req.command.trimmed();

                // Collect exactly expectedBytes with overall deadline
                QByteArray payload;
                payload.reserve(req.expectedBytes);

                QElapsedTimer deadline;
                deadline.start();

                while (payload.size() < req.expectedBytes) {
                    qint64 elapsed = deadline.elapsed();
                    if (elapsed >= CAPTURE_TOTAL_MS) {
                        emit errorOccurred(
                            QString("DATA capture timeout (%1 ms): got %2 of %3 bytes")
                                .arg(CAPTURE_TOTAL_MS)
                                .arg(payload.size())
                                .arg(req.expectedBytes));
                        break;
                    }

                    int chunkWait = static_cast<int>(
                        qMin(static_cast<qint64>(READ_CHUNK_MS),
                             static_cast<qint64>(CAPTURE_TOTAL_MS) - elapsed));

                    if (!sock.waitForReadyRead(chunkWait)) {
                        emit errorOccurred(
                            QString("DATA capture chunk timeout (%1 ms): "
                                    "got %2 of %3 bytes")
                                .arg(chunkWait)
                                .arg(payload.size())
                                .arg(req.expectedBytes));
                        break;
                    }
                    payload.append(sock.readAll());
                }

                if (payload.size() >= req.expectedBytes) {
                    const int numSamples =
                        req.expectedBytes / static_cast<int>(sizeof(qint16));
                    WaveformData wd;
                    wd.sampleRate = req.sampleRate;
                    wd.channel    = req.channel;
                    wd.samples.resize(numSamples);
                    memcpy(wd.samples.data(), payload.constData(),
                           numSamples * sizeof(qint16));
                    qDebug() << "DataWorker: capture complete"
                             << numSamples << "samples in"
                             << deadline.elapsed() << "ms";
                    emit captureReady(wd);
                }
            }
        } // persistent session loop

        sock.disconnectFromHost();
        msleep(1000); // brief pause before reconnect attempt
    }
}
