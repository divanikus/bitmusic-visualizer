#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickWidget>
#include <QVector>
#include <functional>

struct ScopeLane {
    int id = 0;
    QRectF clip;
    QColor color;
    QVector<QPointF> points;
    QVector<QRectF> bars;
};
struct ScopeEffects {
    bool trail = true, glow = true;
    int trailMs = 180;
    float trailStrength = .35f, glowStrength = .18f;
};
class ScopeScene;

// One scene graph for the whole window. Widgets remain responsible for editing
// and a cached chrome texture; waveform ribbons are rasterized by the GPU.
class ScopeGpu : public QQuickWidget {
public:
    explicit ScopeGpu(QWidget *parent, bool externallyPaced = false);
    void submit(const QImage &chrome, const QVector<ScopeLane> &lanes,
                const QByteArray &layoutKey, quint64 generation, int positionMs,
                bool playing, const ScopeEffects &effects, quint64 outputSequence = 0);
    void suspend();
    QString backend() const;
    int historySize() const;
    quint64 renderedFrames() const;
    quint64 completedFrames() const { return completed_; }
    bool checkHealth();
    std::function<void(const QString &)> failed;
private:
    ScopeScene *scene_;
    bool externallyPaced_;
    void reportFailure(const QString &reason);
    QElapsedTimer healthClock_;
    qint64 requestedAt_ = 0;
    quint64 completed_ = 0;
    bool pending_ = false, failed_ = false;
};
