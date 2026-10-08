#include "scopegpu.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace {
struct Trace {
    quint64 serial;
    double born, weight;
    QVector<ScopeLane> lanes;
};
struct SceneRoot : QSGNode {
    QSGSimpleTextureNode *chrome = new QSGSimpleTextureNode;
    quint64 imageKey = 0;
    std::deque<std::pair<quint64, QSGOpacityNode *>> traces;
    SceneRoot() { appendChildNode(chrome); }
    ~SceneRoot() override { delete chrome->texture(); }
};

QSGGeometryNode *bars(const ScopeLane &lane, const ScopeEffects &effects, qreal dpr) {
    if (lane.bars.isEmpty()) return nullptr;
    const float halo = effects.glow ? effects.glowStrength*.3f : 0.f;
    auto geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), int(lane.bars.size())*(halo > 0 ? 54 : 30));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto vertices = geometry->vertexDataAsColoredPoint2D();
    const QColor top = lane.color.lighter(125), bottom = lane.color.darker(135);
    int out = 0;
    auto vertex = [&](QPointF point, const QColor &color, float opacity) {
        opacity *= float(color.alphaF());
        vertices[out++].set(float(point.x()), float(point.y()), uchar(color.red()*opacity),
            uchar(color.green()*opacity), uchar(color.blue()*opacity), uchar(255*opacity));
    };
    auto corners = [](const QRectF &r) { return std::array<QPointF, 4>{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}; };
    auto ring = [&](const QRectF &outer, float a, const QRectF &inner, float b) {
        const auto o = corners(outer), i = corners(inner);
        for (int edge = 0; edge < 4; ++edge) {
            const int next = (edge+1)%4;
            const QColor ca = edge < 2 ? top : bottom, cb = next < 2 ? top : bottom;
            vertex(o[edge], ca, a); vertex(o[next], cb, a); vertex(i[edge], ca, b);
            vertex(i[edge], ca, b); vertex(o[next], cb, a); vertex(i[next], cb, b);
        }
    };
    for (const auto &r : lane.bars) {
        const double fringe = 1./dpr;
        const auto aa = r.adjusted(-fringe, -fringe, fringe, fringe);
        if (halo > 0) ring(r.adjusted(-4, -4, 4, 4), 0, aa, halo);
        ring(aa, halo, r, 1);
        vertex(r.topLeft(), top, 1); vertex(r.topRight(), top, 1); vertex(r.bottomLeft(), bottom, 1);
        vertex(r.bottomLeft(), bottom, 1); vertex(r.topRight(), top, 1); vertex(r.bottomRight(), bottom, 1);
    }
    auto node = new QSGGeometryNode;
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial); node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

QSGGeometryNode *ribbon(const ScopeLane &lane, const ScopeEffects &effects, qreal dpr) {
    const int count = int(lane.points.size());
    if (count < 2) return nullptr;
    // Continuous strips avoid overlapping segment caps on steep/noisy waves.
    // Vertex alpha gives the core an approximately one-device-pixel fringe;
    // the wider, faint bands interpolate a soft halo without blur render passes.
    const float fringe = float(1.0 / dpr);
    const float halo = effects.glow ? effects.glowStrength : 0.f;
    const QVector<float> distances = halo > 0
        ? QVector<float>{-5.f, -2.5f, -.8f-fringe, -.8f, .8f, .8f+fringe, 2.5f, 5.f}
        : QVector<float>{-.8f-fringe, -.8f, .8f, .8f+fringe};
    const QVector<float> alpha = halo > 0
        ? QVector<float>{0, halo*.35f, halo, 1, 1, halo, halo*.35f, 0}
        : QVector<float>{0, 1, 1, 0};
    const int bands = int(distances.size()) - 1;
    auto geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), bands * (count*2 + 2));
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    auto vertices = geometry->vertexDataAsColoredPoint2D();
    QVector<QPointF> normals; normals.reserve(count);
    auto normal = [](QPointF delta) {
        const auto length = std::hypot(delta.x(), delta.y());
        return length > .0001 ? QPointF(-delta.y()/length, delta.x()/length) : QPointF(0, 1);
    };
    for (int i = 0; i < count; ++i) {
        const auto a = normal(lane.points[i] - lane.points[std::max(0, i-1)]);
        const auto b = normal(lane.points[std::min(count-1, i+1)] - lane.points[i]);
        auto n = i == 0 ? b : i == count-1 ? a : (a+b)*.5;
        const auto length = std::hypot(n.x(), n.y());
        if (length > .0001) n /= length;
        // Bounded miter prevents spikes at rapid polarity reversals.
        const double dot = n.x()*b.x() + n.y()*b.y();
        normals.push_back(n / std::max(.5, std::abs(dot)));
    }
    int out = 0;
    auto vertex = [&](int point, int edge) {
        auto p = lane.points[point] + normals[point]*distances[edge];
        const float opacity = alpha[edge]*float(lane.color.alphaF());
        // QSGVertexColorMaterial expects premultiplied vertex colors.
        vertices[out++].set(float(p.x()), float(p.y()),
            uchar(lane.color.red()*opacity), uchar(lane.color.green()*opacity),
            uchar(lane.color.blue()*opacity), uchar(255*opacity));
    };
    for (int band = 0; band < bands; ++band) {
        vertex(0, band); // degenerate joins between strips
        for (int i = 0; i < count; ++i) { vertex(i, band); vertex(i, band+1); }
        vertex(count-1, band+1);
    }
    auto node = new QSGGeometryNode;
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    auto material = new QSGVertexColorMaterial;
    node->setMaterial(material); node->setFlag(QSGNode::OwnsMaterial);
    return node;
}
}

class ScopeScene : public QQuickItem {
public:
    ScopeScene() {
        setFlag(ItemHasContents); clock.start(); timer.setInterval(16);
        QObject::connect(&timer, &QTimer::timeout, this, [this] { advance(); expire(); update(); });
    }
    QImage image;
    QByteArray layout;
    std::deque<Trace> traces;
    ScopeEffects effects;
    QElapsedTimer clock;
    QTimer timer;
    double now = 0;
    qint64 lastTick = 0;
    quint64 serial = 0, generation = 0, frames = 0, outputSequence = 0;
    int position = -1;
    bool running = false;
    std::function<void(const QString &)> failed;
    void advance() {
        const auto tick = clock.elapsed();
        if (running) now += (tick - lastTick) / 1000.0;
        lastTick = tick;
    }
    void expire() {
        const double life = effects.trailMs / 1000.0;
        // Covers 999 ms at 120 FPS, with headroom for occasional extra UI paints.
        while (traces.size() > 1 && (now - traces.front().born >= life || traces.size() > 128 || !effects.trail)) traces.pop_front();
    }
    void clear() { traces.clear(); position = -1; timer.stop(); update(); }
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override {
        // Expose/resize can arrive before a first image, or after allocation failure.
        // A texture node without a texture must never reach the renderer.
        if (image.isNull()) { delete old; return nullptr; }
        // Qt's software adaptation cannot render QSGGeometry ribbons. Avoid
        // submitting them even during the first frame, before the queued fallback.
        if (window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            delete old; return nullptr;
        }
        auto root = static_cast<SceneRoot *>(old);
        if (!root) root = new SceneRoot;
        ++frames;
        if (root->imageKey != quint64(image.cacheKey())) {
            auto texture = window()->createTextureFromImage(image);
            if (!texture) {
                delete root;
                QMetaObject::invokeMethod(this, [this] {
                    if (failed) failed("Could not create the scope texture");
                }, Qt::QueuedConnection);
                return nullptr;
            }
            auto previous = root->chrome->texture();
            root->chrome->setTexture(texture);
            // Ownership stays here, including replacement and scene invalidation.
            delete previous;
            root->imageKey = quint64(image.cacheKey());
        }
        root->chrome->setRect(boundingRect());
        while (!root->traces.empty() && (traces.empty() || root->traces.front().first < traces.front().serial)) {
            auto node = root->traces.front().second; root->removeChildNode(node); delete node; root->traces.pop_front();
        }
        for (size_t i = 0; i < traces.size(); ++i) {
            const auto &trace = traces[i];
            if (i >= root->traces.size()) {
                auto opacity = new QSGOpacityNode;
                for (const auto &lane : trace.lanes) {
                    const auto dpr = window()->effectiveDevicePixelRatio();
                    auto mesh = lane.bars.isEmpty() ? ribbon(lane, effects, dpr) : bars(lane, effects, dpr);
                    if (!mesh) continue;
                    auto clip = new QSGClipNode; clip->setIsRectangular(true); clip->setClipRect(lane.clip);
                    clip->appendChildNode(mesh); opacity->appendChildNode(clip);
                }
                root->appendChildNode(opacity); root->traces.emplace_back(trace.serial, opacity);
            }
            const double age = std::max(0., now - trace.born);
            const double opacity = i+1 == traces.size() ? 1.0
                : trace.weight * effects.trailStrength * std::exp(-4*age/(effects.trailMs/1000.0));
            root->traces[i].second->setOpacity(float(opacity));
        }
        return root;
    }
};

ScopeGpu::ScopeGpu(QWidget *parent, bool externallyPaced)
    : QQuickWidget(parent), scene_(new ScopeScene), externallyPaced_(externallyPaced) {
    healthClock_.start();
    setObjectName("scopeGpu"); setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(QColor("#171e24"));
    setContent(QUrl(), nullptr, scene_);
    scene_->failed = [this](const QString &reason) { reportFailure(reason); };
    connect(this, &QQuickWidget::sceneGraphError, this, [this](QQuickWindow::SceneGraphError, const QString &message) {
        reportFailure(message);
    }, Qt::QueuedConnection);
    connect(quickWindow(), &QQuickWindow::sceneGraphInitialized, this, [this] {
        if (quickWindow()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            reportFailure("Qt Quick software backend selected");
    }, Qt::QueuedConnection);
    connect(quickWindow(), &QQuickWindow::afterRendering, this, [this] {
        ++completed_; pending_ = false;
    }, Qt::QueuedConnection);
    auto watchdog = new QTimer(this); watchdog->setInterval(500);
    connect(watchdog, &QTimer::timeout, this, [this] { checkHealth(); }); watchdog->start();
}
void ScopeGpu::reportFailure(const QString &reason) {
    if (failed_) return;
    failed_ = true;
    qWarning().noquote() << "Scope GPU fallback:" << reason << "backend=" << backend();
    // Leave paint/resize/scene callbacks before the owner releases this widget.
    QMetaObject::invokeMethod(this, [this, reason] { if (failed) failed(reason); }, Qt::QueuedConnection);
}
bool ScopeGpu::checkHealth() {
    if (failed_) return false;
    auto top = window();
    if (!isVisible() || top->isMinimized() || !top->windowHandle() || !top->windowHandle()->isExposed()) {
        pending_ = false; return true;
    }
    if (pending_ && healthClock_.elapsed() - requestedAt_ >= 5000) {
        reportFailure("No completed scope frame for 5 seconds"); return false;
    }
    return true;
}
void ScopeGpu::submit(const QImage &chrome, const QVector<ScopeLane> &lanes,
                      const QByteArray &layoutKey, quint64 generation, int positionMs,
                      bool playing, const ScopeEffects &effects, quint64 outputSequence) {
    if (!checkHealth()) return;
    if (chrome.isNull()) { reportFailure("Could not allocate the scope image"); return; }
    if (!pending_) { pending_ = true; requestedAt_ = healthClock_.elapsed(); }
    scene_->advance();
    const auto &old = scene_->effects;
    const bool changed = old.trail != effects.trail || old.glow != effects.glow || old.trailMs != effects.trailMs
        || old.glowStrength != effects.glowStrength || old.trailStrength != effects.trailStrength;
    if (scene_->layout != layoutKey || scene_->generation != generation || changed || positionMs < scene_->position)
        scene_->clear();
    scene_->effects = effects; scene_->layout = layoutKey; scene_->generation = generation;
    scene_->image = chrome; scene_->running = playing;
    if (scene_->position != positionMs || scene_->outputSequence != outputSequence || scene_->traces.empty()) {
        if (!scene_->traces.empty()) {
            // Normalize historical brightness by capture interval, not frame rate.
            scene_->traces.back().weight = 1 - std::exp(-4 * std::max(0., scene_->now-scene_->traces.back().born)/(effects.trailMs/1000.0));
        }
        scene_->traces.push_back({++scene_->serial, scene_->now, 0, lanes});
        scene_->position = positionMs;
        scene_->outputSequence = outputSequence;
    }
    scene_->expire();
    // The player uses one cadence for both new waves and fading their history.
    if (!externallyPaced_ && playing && effects.trail && scene_->traces.size() > 1) scene_->timer.start(); else scene_->timer.stop();
    scene_->update();
}
void ScopeGpu::suspend() { pending_ = false; scene_->advance(); scene_->running = false; scene_->clear(); }
QString ScopeGpu::backend() const {
    switch (quickWindow()->rendererInterface()->graphicsApi()) {
    case QSGRendererInterface::Direct3D11: return "Direct3D 11";
    case QSGRendererInterface::Direct3D12: return "Direct3D 12";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Software: return "Software";
    default: return "Initializing";
    }
}
int ScopeGpu::historySize() const { return int(scene_->traces.size()); }
quint64 ScopeGpu::renderedFrames() const { return scene_->frames; }
