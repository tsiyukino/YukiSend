#include "Animator.h"

#include <cmath>
#include <algorithm>

Animator::Animator(QObject *parent) : QObject(parent) {
    m_timer.setInterval(16); // ~60fps
    connect(&m_timer, &QTimer::timeout, this, &Animator::tick);
}

void Animator::start(Callback callback, double from, double to, int durationMs) {
    m_callback  = std::move(callback);
    m_from      = from;
    m_to        = to;
    m_duration  = durationMs;
    m_current   = from;
    m_startTime = std::chrono::steady_clock::now();
    m_animating = true;
    m_timer.start();
}

void Animator::stop() {
    m_timer.stop();
    m_animating = false;
}

double Animator::value(double defaultValue) const {
    if (!m_animating) return defaultValue;
    return m_current;
}

bool Animator::animating() const {
    return m_animating;
}

void Animator::tick() {
    const auto now     = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_startTime).count();

    double t = (m_duration > 0)
        ? std::clamp(static_cast<double>(elapsed) / m_duration, 0.0, 1.0)
        : 1.0;

    t = m_easing(t);
    m_current = m_from + (m_to - m_from) * t;

    if (m_callback) m_callback();

    if (elapsed >= m_duration) {
        m_current   = m_to;
        m_animating = false;
        m_timer.stop();
        if (m_callback) m_callback();
    }
}

double Animator::easeOutCirc(double t) {
    return std::sqrt(1.0 - std::pow(t - 1.0, 2.0));
}

double Animator::easeOutCubic(double t) {
    return 1.0 - std::pow(1.0 - t, 3.0);
}

double Animator::linear(double t) {
    return t;
}
