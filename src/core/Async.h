#pragma once
// Trabalho pesado (remux, miniaturas, varredura da pasta) fora da thread da interface,
// com a continuação de volta na thread do objeto `context`.

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <memory>
#include <type_traits>
#include <utility>

namespace aeonis {

template <typename Work, typename Done>
void runAsync(QObject *context, Work work, Done done, QThreadPool *pool = QThreadPool::globalInstance())
{
    QPointer<QObject> guard(context);
    pool->start([guard, work = std::move(work), done = std::move(done)]() mutable {
        if constexpr (std::is_void_v<std::invoke_result_t<Work>>) {
            work();
            if (guard)
                QMetaObject::invokeMethod(guard.data(), [guard, done = std::move(done)]() mutable {
                    if (guard) done();
                }, Qt::QueuedConnection);
        } else {
            auto result = std::make_shared<std::invoke_result_t<Work>>(work());
            if (guard)
                QMetaObject::invokeMethod(guard.data(), [guard, result, done = std::move(done)]() mutable {
                    if (guard) done(std::move(*result));
                }, Qt::QueuedConnection);
        }
    });
}

/// Executa `f` na thread de `context` (fila de eventos).
template <typename F>
void postTo(QObject *context, F f)
{
    QMetaObject::invokeMethod(context, std::move(f), Qt::QueuedConnection);
}

} // namespace aeonis
