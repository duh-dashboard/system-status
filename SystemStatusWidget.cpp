// Copyright (C) 2026 Sean Moon
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "SystemStatusWidget.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <QFile>

// ── Platform-specific helpers ─────────────────────────────────────────────────

namespace {

struct CpuSnapshot {
    quint64 active = 0;
    quint64 total  = 0;
};

struct NetSnapshot {
    quint64 rx = 0;
    quint64 tx = 0;
};

CpuSnapshot readCpu() {
    QFile f("/proc/stat");
    if (!f.open(QFile::ReadOnly)) return {};
    const auto parts = QString(f.readLine()).simplified().split(' ');
    // cpu user nice system idle iowait irq softirq
    if (parts.size() < 8) return {};
    quint64 user    = parts[1].toULongLong();
    quint64 nice    = parts[2].toULongLong();
    quint64 system  = parts[3].toULongLong();
    quint64 idle    = parts[4].toULongLong();
    quint64 iowait  = parts[5].toULongLong();
    quint64 irq     = parts[6].toULongLong();
    quint64 softirq = parts[7].toULongLong();
    quint64 active  = user + nice + system + irq + softirq;
    quint64 total   = active + idle + iowait;
    return {active, total};
}

int cpuPercent(const CpuSnapshot& prev, const CpuSnapshot& curr) {
    quint64 dTotal  = curr.total  - prev.total;
    quint64 dActive = curr.active - prev.active;
    if (dTotal == 0) return 0;
    return int(dActive * 100 / dTotal);
}

int readMemoryPercent() {
    QFile f("/proc/meminfo");
    if (!f.open(QFile::ReadOnly)) return 0;
    quint64 total = 0, available = 0;
    while (!f.atEnd()) {
        const auto line = QString(f.readLine()).simplified();
        if (line.startsWith("MemTotal:"))
            total = line.split(' ').value(1).toULongLong();
        else if (line.startsWith("MemAvailable:"))
            available = line.split(' ').value(1).toULongLong();
        if (total && available) break;
    }
    return total ? int((total - available) * 100 / total) : 0;
}

NetSnapshot readNetwork() {
    QFile f("/proc/net/dev");
    if (!f.open(QFile::ReadOnly)) return {};
    f.readLine();  // header row 1
    f.readLine();  // header row 2
    NetSnapshot snap;
    while (!f.atEnd()) {
        const auto line = QString(f.readLine()).trimmed();
        const auto colonIdx = line.indexOf(':');
        if (colonIdx < 0) continue;
        const QString iface = line.left(colonIdx).trimmed();
        if (iface == "lo") continue;
        const auto fields = line.mid(colonIdx + 1).simplified().split(' ');
        if (fields.size() >= 9) {
            snap.rx += fields[0].toULongLong();  // receive bytes
            snap.tx += fields[8].toULongLong();  // transmit bytes
        }
    }
    return snap;
}

int readDiskPercent() {
    const QStorageInfo info(QDir::rootPath());
    if (!info.isValid() || info.bytesTotal() <= 0) return 0;
    qint64 used = info.bytesTotal() - info.bytesFree();
    return int(used * 100 / info.bytesTotal());
}

QString formatRate(quint64 bytesPerSec) {
    if (bytesPerSec < 1024)
        return QString("%1 B/s").arg(bytesPerSec);
    if (bytesPerSec < 1024 * 1024)
        return QString("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    return QString("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 1);
}

}  // namespace

// ── SystemStatusDisplay ───────────────────────────────────────────────────────

class SystemStatusDisplay : public QWidget {
    Q_OBJECT

public:
    explicit SystemStatusDisplay(QWidget* parent = nullptr) : QWidget(parent) {
        setupUi();
        // Prime the delta snapshots before the first tick
        prevCpu_ = readCpu();
        prevNet_ = readNetwork();

        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &SystemStatusDisplay::tick);
        timer->start(1000);
    }

private:
    // ── UI ────────────────────────────────────────────────────────────────────
    void setupUi() {
        auto* vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(6, 6, 6, 6);
        vbox->setSpacing(8);

        // Section label style
        const QString sectionStyle =
            "QLabel { color: #606080; font-size: 10px; font-weight: 700;"
            " letter-spacing: 1px; background: transparent; }";
        // Metric name style
        const QString nameStyle =
            "QLabel { color: #8890b8; font-size: 11px; background: transparent; }";
        // Value style
        const QString valueStyle =
            "QLabel { color: #c8cee8; font-size: 11px; background: transparent; }";

        // ── CPU / Memory / Disk rows ─────────────────────────────────────────
        auto* perfLabel = new QLabel("PERFORMANCE", this);
        perfLabel->setStyleSheet(sectionStyle);
        vbox->addWidget(perfLabel);

        auto makeRow = [&](const QString& name, const QString& barColor)
            -> std::pair<QProgressBar*, QLabel*> {
            auto* row = new QHBoxLayout();
            row->setSpacing(8);

            auto* nameLabel = new QLabel(name, this);
            nameLabel->setFixedWidth(52);
            nameLabel->setStyleSheet(nameStyle);

            auto* bar = new QProgressBar(this);
            bar->setRange(0, 100);
            bar->setValue(0);
            bar->setTextVisible(false);
            bar->setFixedHeight(6);
            bar->setStyleSheet(
                "QProgressBar {"
                "  background: #252540; border: none; border-radius: 3px; }"
                "QProgressBar::chunk {"
                "  background: " + barColor + "; border-radius: 3px; }");

            auto* pctLabel = new QLabel("0%", this);
            pctLabel->setFixedWidth(34);
            pctLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pctLabel->setStyleSheet(valueStyle);

            row->addWidget(nameLabel);
            row->addWidget(bar, 1);
            row->addWidget(pctLabel);
            vbox->addLayout(row);
            return {bar, pctLabel};
        };

        auto [cpuBar, cpuPct]    = makeRow("CPU",    "#4a7ae0");
        auto [memBar, memPct]    = makeRow("Memory", "#7a5ae0");
        auto [diskBar, diskPct]  = makeRow("Disk",   "#40c0a0");
        cpuBar_  = cpuBar;  cpuPct_  = cpuPct;
        memBar_  = memBar;  memPct_  = memPct;
        diskBar_ = diskBar; diskPct_ = diskPct;

        // ── Network section ──────────────────────────────────────────────────
        auto* netLabel = new QLabel("NETWORK", this);
        netLabel->setStyleSheet(sectionStyle);
        vbox->addWidget(netLabel);

        auto makeNetRow = [&](const QString& icon) -> QLabel* {
            auto* row = new QHBoxLayout();
            row->setSpacing(8);
            auto* iconLabel = new QLabel(icon, this);
            iconLabel->setFixedWidth(52);
            iconLabel->setStyleSheet(nameStyle);
            auto* rateLabel = new QLabel("-- B/s", this);
            rateLabel->setStyleSheet(valueStyle);
            row->addWidget(iconLabel);
            row->addWidget(rateLabel, 1);
            vbox->addLayout(row);
            return rateLabel;
        };

        upLabel_   = makeNetRow("\u2191 Upload");    // ↑
        downLabel_ = makeNetRow("\u2193 Download");  // ↓

        vbox->addStretch();
    }

    // ── Tick ──────────────────────────────────────────────────────────────────
    void tick() {
        // CPU
        const auto currCpu = readCpu();
        int cpu = cpuPercent(prevCpu_, currCpu);
        prevCpu_ = currCpu;
        cpuBar_->setValue(cpu);
        cpuPct_->setText(QString("%1%").arg(cpu));

        // Memory
        int mem = readMemoryPercent();
        memBar_->setValue(mem);
        memPct_->setText(QString("%1%").arg(mem));

        // Disk (stable, fine to read every second)
        int disk = readDiskPercent();
        diskBar_->setValue(disk);
        diskPct_->setText(QString("%1%").arg(disk));

        // Network
        const auto currNet = readNetwork();
        quint64 rxRate = currNet.rx > prevNet_.rx ? currNet.rx - prevNet_.rx : 0;
        quint64 txRate = currNet.tx > prevNet_.tx ? currNet.tx - prevNet_.tx : 0;
        prevNet_ = currNet;
        upLabel_->setText(formatRate(txRate));
        downLabel_->setText(formatRate(rxRate));
    }

    // Bars + labels
    QProgressBar* cpuBar_  = nullptr;
    QProgressBar* memBar_  = nullptr;
    QProgressBar* diskBar_ = nullptr;
    QLabel*       cpuPct_  = nullptr;
    QLabel*       memPct_  = nullptr;
    QLabel*       diskPct_ = nullptr;
    QLabel*       upLabel_   = nullptr;
    QLabel*       downLabel_ = nullptr;

    // Delta state
    CpuSnapshot prevCpu_;
    NetSnapshot prevNet_;
};

#include "SystemStatusWidget.moc"

// ── SystemStatusWidget (IWidget plugin) ───────────────────────────────────────

SystemStatusWidget::SystemStatusWidget(QObject* parent) : QObject(parent) {}

void SystemStatusWidget::initialize(dashboard::WidgetContext* /*context*/) {}

QWidget* SystemStatusWidget::createWidget(QWidget* parent) {
    return new SystemStatusDisplay(parent);
}

QJsonObject SystemStatusWidget::serialize() const { return {}; }

void SystemStatusWidget::deserialize(const QJsonObject& /*data*/) {}

dashboard::WidgetMetadata SystemStatusWidget::metadata() const {
    return {
        .name        = "System Status",
        .version     = "1.0.0",
        .author      = "Dashboard",
        .description = "Live CPU, memory, disk, and network usage",
        .minSize     = QSize(200, 180),
        .maxSize     = QSize(480, 400),
        .defaultSize = QSize(260, 220),
    };
}
