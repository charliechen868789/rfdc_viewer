#include "cmdworker.h"
#include <QTcpSocket>
#include <QMutexLocker>
#include <QDebug>

CmdWorker::CmdWorker(const QString &host, quint16 port, QObject *parent)
    : QThread(parent), m_host(host), m_port(port)
{}

CmdWorker::~CmdWorker()
{
    stop();
    wait(3000);
}

void CmdWorker::enqueueCommand(const QByteArray &cmd)
{
    CmdItem item;
    item.cmd          = cmd;
    item.delayAfterMs = 0;
    item.finalItem    = false;
    QMutexLocker lk(&m_mutex);
    m_queue.enqueue(item);
    m_cond.wakeOne();
}

void CmdWorker::enqueueSequence(const QList<CmdItem> &items)
{
    QMutexLocker lk(&m_mutex);
    for (int i = 0; i < items.size(); ++i) {
        CmdItem it = items[i];
        it.finalItem = (i == items.size() - 1);
        m_queue.enqueue(it);
    }
    m_cond.wakeOne();
}

void CmdWorker::stop()
{
    QMutexLocker lk(&m_mutex);
    m_stop = true;
    m_cond.wakeOne();
}

void CmdWorker::run()
{
    while (true) {
        {
            QMutexLocker lk(&m_mutex);
            if (m_stop) return;
        }

        // ── Connect ───────────────────────────────────────────────────────
        QTcpSocket sock;
        sock.connectToHost(m_host, m_port);
        if (!sock.waitForConnected(CONNECT_TIMEOUT_MS)) {
            emit errorOccurred(
                QString("CMD connect failed (%1 ms): %2")
                    .arg(CONNECT_TIMEOUT_MS).arg(sock.errorString()));
            msleep(RECONNECT_DELAY_MS);
            continue;
        }
        emit connected();
        qDebug() << "CmdWorker: connected";

        // ── Main loop ─────────────────────────────────────────────────────
        while (true) {
            CmdItem item;
            bool haveItem = false;

            {
                QMutexLocker lk(&m_mutex);
                if (m_stop) {
                    sock.disconnectFromHost();
                    emit disconnected();
                    return;
                }
                if (!m_queue.isEmpty()) {
                    item     = m_queue.dequeue();
                    haveItem = true;
                } else {
                    m_cond.wait(&m_mutex, IDLE_WAIT_MS);
                    continue;
                }
            }

            if (!haveItem) continue;

            // Send command
            qDebug() << "CmdWorker TX:" << item.cmd.trimmed();
            sock.write(item.cmd);
            if (!sock.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
                emit errorOccurred(
                    QString("CMD write timeout (%1 ms): %2")
                        .arg(WRITE_TIMEOUT_MS)
                        .arg(QString::fromUtf8(item.cmd.trimmed())));
                // continue anyway — don't drop the connection
            }

            // Read any response
            if (sock.waitForReadyRead(READ_POLL_MS)) {
                const QByteArray resp = sock.readAll();
                if (resp.isEmpty()) {
                    emit disconnected();
                    goto reconnect;
                }
                qDebug() << "CmdWorker RX:" << resp.trimmed();
            }

            // Post-command delay (keeps board happy between commands)
            if (item.delayAfterMs > 0)
                msleep(item.delayAfterMs);

            // Signal end of sequence
            if (item.finalItem)
                emit sequenceDone();

            // Check socket still alive
            if (sock.state() != QAbstractSocket::ConnectedState) {
                emit errorOccurred("CMD socket lost");
                emit disconnected();
                goto reconnect;
            }
        }
        reconnect:;
        msleep(RECONNECT_DELAY_MS);
    }
}
