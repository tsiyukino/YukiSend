#pragma once

#include <QObject>
#include <QTimer>
#include <functional>
#include <chrono>

// Lightweight timer-based animator, same pattern as Telegram's Animations::Simple.
// Drives a 0.0→1.0 progress value through a callback; caller repaints on each tick.
class Animator : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void()>;

    explicit Animator(QObject *parent = nullptr);

    // Start animating from current progress toward `to` over `durationMs`.
    void start(Callback callback, double from, double to, int durationMs);
    void stop();

    // Current interpolated value. Pass the final target as `defaultValue`
    // so calls after the animation ends return the settled value.
    double value(double defaultValue) const;

    bool animating() const;

    // Easing functions — match Telegram's anim:: namespace
    static double easeOutCirc(double t);
    static double easeOutCubic(double t);
    static double linear(double t);

private:
    void tick();

    Callback m_callback;
    QTimer   m_timer;

    double m_from     = 0.0;
    double m_to       = 1.0;
    double m_current  = 0.0;
    int    m_duration = 200;

    std::chrono::steady_clock::time_point m_startTime;
    bool m_animating = false;

    // Easing applied during this animation run
    double (*m_easing)(double) = &Animator::easeOutCirc;
};
