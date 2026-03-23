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
    // Drop any stale pending requests of the same type to avoid queue pile-up
    // when auto-acquire fires faster than the board responds
    for (int i = m_queue.size()-1; i >= 0; --i)
        if (m_queue[i].type == req.type)
            m_queue.removeAt(i);
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

    // ── Connect once, stay connected (mirrors Python data_rx_task) ───────
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
            // Stop check
            {
                QMutexLocker lk(&m_mutex);
                if (m_stop) { sock.disconnectFromHost(); return; }
            }

            // Socket health check
            if (sock.state() != QAbstractSocket::ConnectedState) {
                qDebug() << "DataWorker: connection lost — reconnecting";
                emit errorOccurred("DATA socket disconnected");
                break;
            }

            // Drain any unsolicited data the board may have sent
            if (sock.bytesAvailable() > 0) {
                qDebug() << "DataWorker: draining" << sock.bytesAvailable() << "stale bytes";
                sock.readAll();
            }

            // Wait for a request
            DataRequest req;
            bool haveReq = false;
            {
                QMutexLocker lk(&m_mutex);
                if (m_queue.isEmpty())
                    m_cond.wait(&m_mutex, 200);
                if (m_stop) { sock.disconnectFromHost(); return; }
                if (!m_queue.isEmpty()) {
                    req     = m_queue.dequeue();
                    haveReq = true;
                }
            }
            if (!haveReq) continue;

            // ── SendWaveform ──────────────────────────────────────────────
            if (req.type == DataRequest::SendWaveform) {

                sock.write(req.command);
                if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                    emit errorOccurred(
                        QString("DATA cmd write timeout (%1 ms)").arg(WRITE_TIMEOUT_MS));
                    continue;
                }
                qDebug() << "DataWorker TX cmd:" << req.command.trimmed();

                const char *buf  = req.payload.constData();
                int          left = req.payload.size();
                bool ok = true;
                QElapsedTimer timer; timer.start();

                while (left > 0) {
                    qint64 written = sock.write(buf, left);
                    if (written < 0) {
                        emit errorOccurred("DATA payload write error: " + sock.errorString());
                        ok = false; break;
                    }
                    if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                        emit errorOccurred(
                            QString("DATA payload write timeout (%1 ms), %2/%3 bytes")
                                .arg(WRITE_TIMEOUT_MS).arg(req.payload.size()-left)
                                .arg(req.payload.size()));
                        ok = false; break;
                    }
                    buf  += written;
                    left -= static_cast<int>(written);
                }

                if (ok) {
                    qDebug() << "DataWorker: waveform sent"
                             << req.payload.size() << "bytes in" << timer.elapsed() << "ms";
                    emit sendDone();
                }

            // ── ReceiveCapture ────────────────────────────────────────────
            } else {

                // Extra delay: give board time to complete the ADC capture
                // after LocalMemTrigger before we send the read command.
                // The original Python had sleep(0.1) here via the CMD queue
                // delay, but the CMD and DATA queues run in parallel so we
                // need an explicit wait on the data side too.
                msleep(CAPTURE_SETTLE_MS);

                sock.write(req.command);
                if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                    emit errorOccurred(
                        QString("DATA RX cmd write timeout (%1 ms)").arg(WRITE_TIMEOUT_MS));
                    continue;
                }
                qDebug() << "DataWorker RX cmd:" << req.command.trimmed();

                // Collect exactly expectedBytes
                QByteArray payload;
                payload.reserve(req.expectedBytes);
                QElapsedTimer deadline; deadline.start();

                while (payload.size() < req.expectedBytes) {
                    qint64 elapsed = deadline.elapsed();
                    if (elapsed >= CAPTURE_TOTAL_MS) {
                        emit errorOccurred(
                            QString("DATA capture timeout (%1 ms): got %2 of %3 bytes")
                                .arg(CAPTURE_TOTAL_MS)
                                .arg(payload.size()).arg(req.expectedBytes));
                        break;
                    }
                    int chunkWait = static_cast<int>(
                        qMin(static_cast<qint64>(READ_CHUNK_MS),
                             static_cast<qint64>(CAPTURE_TOTAL_MS) - elapsed));
                    if (!sock.waitForReadyRead(chunkWait)) {
                        emit errorOccurred(
                            QString("DATA capture chunk timeout (%1 ms): got %2 of %3 bytes")
                                .arg(chunkWait)
                                .arg(payload.size()).arg(req.expectedBytes));
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
                             << numSamples << "samples in" << deadline.elapsed() << "ms";
                    emit captureReady(wd);
                }
            }
        } // persistent session loop

        sock.disconnectFromHost();
        msleep(1000);
    }
}
