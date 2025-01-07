// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qqmlpropertytopropertybinding_p.h"

#include <private/qqmlanybinding_p.h>
#include <private/qqmlengine_p.h>
#include <private/qqmlvmemetaobject_p.h>
#include <private/qv4alloca_p.h>
#include <private/qv4jscall_p.h>

#include <QtQml/qqmlinfo.h>

QT_BEGIN_NAMESPACE

/*!
 * \internal
 * \class QQmlPropertyToPropertyBinding
 *
 * This class can be used to create a direct binding from a source property to
 * a target property, without going through QQmlJavaScriptExpression and
 * QV4::Function. In particular you don't need a compilation unit or byte code
 * to set this up.
 *
 * \note The target cannot be a group property, but the source can.
 */

QQmlAnyBinding QQmlPropertyToPropertyBinding::create(
        QQmlEngine *engine, const QQmlProperty &source, const QQmlProperty &target)
{
    QQmlAnyBinding result;
    if (target.isBindable()) {
        if (source.isBindable()) {
            result = QUntypedPropertyBinding(new QQmlBindableToBindablePropertyBinding(
                    engine, source.object(), QQmlPropertyPrivate::get(source)->encodedIndex(),
                    target.object(), target.index()));
            return result;
        }

        result = QUntypedPropertyBinding(new QQmlUnbindableToBindablePropertyBinding(
                engine, source.object(), QQmlPropertyPrivate::get(source)->encodedIndex(),
                target.object(), target.index()));
        return result;
    }

    if (source.isBindable()) {
        result = new QQmlBindableToUnbindablePropertyBinding(
                engine, source.object(), QQmlPropertyPrivate::get(source)->encodedIndex(),
                target.object(), target.index());
        return result;
    }

    result = new QQmlUnbindableToUnbindablePropertyBinding(
            engine, source.object(), QQmlPropertyPrivate::get(source)->encodedIndex(),
            target.object(), target.index());
    return result;
}

QQmlPropertyToPropertyBinding::QQmlPropertyToPropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex)
    : engine(engine)
    , sourceObject(sourceObject)
    , sourcePropertyIndex(sourcePropertyIndex)
{
}

QQmlUnbindableToUnbindablePropertyBinding::QQmlUnbindableToUnbindablePropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex,
        QObject *targetObject, int targetPropertyIndex)
    : QQmlNotifierEndpoint(QQmlUnbindableToUnbindableGuard)
    , QQmlPropertyToUnbindablePropertyBinding(
            engine, sourceObject, sourcePropertyIndex, targetObject, targetPropertyIndex)
{
}

QQmlAbstractBinding::Kind QQmlPropertyToUnbindablePropertyBinding::kind() const
{
    return PropertyToPropertyBinding;
}

void QQmlPropertyToUnbindablePropertyBinding::setEnabled(
        bool e, QQmlPropertyData::WriteFlags flags)
{
    const bool wasEnabled = enabledFlag();
    setEnabledFlag(e);
    updateCanUseAccessor();
    if (e && !wasEnabled)
        update(flags);
}

void QQmlPropertyToUnbindablePropertyBinding::update(QQmlPropertyData::WriteFlags flags)
{
    if (!enabledFlag())
        return;

    // Check that the target has not been deleted
    QObject *target = targetObject();
    if (QQmlData::wasDeleted(target))
        return;

    const QQmlPropertyData *d = nullptr;
    QQmlPropertyData vtd;
    getPropertyData(&d, &vtd);
    Q_ASSERT(d);

    // Check for a binding update loop
    if (Q_UNLIKELY(updatingFlag())) {
        printBindingLoopError(QQmlPropertyPrivate::restore(target, *d, &vtd, nullptr));
        return;
    }

    setUpdatingFlag(true);

    if (canUseAccessor())
        flags.setFlag(QQmlPropertyData::BypassInterceptor);

    QVariant value = m_binding.readSourceValue(
            [&](const QMetaObject *sourceMetaObject, const QMetaProperty &property) {
        captureProperty(sourceMetaObject, property);
    });

    QQmlPropertyPrivate::writeValueProperty(target, *d, vtd, value, {}, flags);
    setUpdatingFlag(false);
}

QQmlPropertyToUnbindablePropertyBinding::QQmlPropertyToUnbindablePropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex,
        QObject *targetObject, int targetPropertyIndex)
    : m_binding(engine, sourceObject, sourcePropertyIndex)
{
    setTarget(targetObject, targetPropertyIndex, false, -1);
}

QQmlBindableToUnbindablePropertyBinding::QQmlBindableToUnbindablePropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex,
        QObject *targetObject, int targetPropertyIndex)
    : QPropertyObserver(QQmlBindableToUnbindablePropertyBinding::update)
    , QQmlPropertyToUnbindablePropertyBinding(
              engine, sourceObject, sourcePropertyIndex, targetObject, targetPropertyIndex)
{
}

void QQmlBindableToUnbindablePropertyBinding::update(
        QPropertyObserver *observer, QUntypedPropertyData *)
{
    static_cast<QQmlBindableToUnbindablePropertyBinding *>(observer)
            ->QQmlPropertyToUnbindablePropertyBinding::update();
}

void QQmlUnbindableToUnbindablePropertyBinding::captureProperty(
        const QMetaObject *sourceMetaObject, const QMetaProperty &sourceProperty)
{
    Q_UNUSED(sourceMetaObject);
    m_binding.doConnectNotify(this, sourceProperty);
}

void QQmlBindableToUnbindablePropertyBinding::captureProperty(
        const QMetaObject *sourceMetaObject, const QMetaProperty &sourceProperty)
{
    Q_UNUSED(sourceProperty);

    // We have already captured.
    if (m_isObserving)
        return;

    QUntypedBindable bindable;
    void *argv[] = { &bindable };
    sourceMetaObject->metacall(
            m_binding.sourceObject, QMetaObject::BindableProperty,
            m_binding.sourcePropertyIndex.coreIndex(), argv);
    bindable.observe(this);
    m_isObserving = true;
}

namespace QtPrivate {
template<typename Binding>
inline constexpr BindingFunctionVTable
    bindingFunctionVTableForQQmlPropertyToBindablePropertyBinding = {
        &Binding::update,
        [](void *qpropertyBinding) { delete reinterpret_cast<Binding *>(qpropertyBinding); },
        [](void *, void *){},
        0
    };
}

QQmlUnbindableToBindablePropertyBinding::QQmlUnbindableToBindablePropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex,
        QObject *targetObject, int targetPropertyIndex)
    : QPropertyBindingPrivate(
              targetObject->metaObject()->property(targetPropertyIndex).metaType(),
              &QtPrivate::bindingFunctionVTableForQQmlPropertyToBindablePropertyBinding<
                      QQmlUnbindableToBindablePropertyBinding>,
              QPropertyBindingSourceLocation(), true)
    , QQmlNotifierEndpoint(QQmlUnbindableToBindableGuard)
    , m_binding(engine, sourceObject, sourcePropertyIndex)
    , m_targetObject(targetObject)
    , m_targetPropertyIndex(targetPropertyIndex)
{
}

bool QQmlUnbindableToBindablePropertyBinding::update(
        QMetaType metaType, QUntypedPropertyData *dataPtr, void *f)
{
    QQmlUnbindableToBindablePropertyBinding *self
            = reinterpret_cast<QQmlUnbindableToBindablePropertyBinding *>(
                // Address of QPropertyBindingPrivate subobject
                static_cast<std::byte *>(f) - QPropertyBindingPrivate::getSizeEnsuringAlignment());

    // Unbindable source property needs capturing
    const QVariant value = self->m_binding.readSourceValue(
            [self](const QMetaObject *sourceMetaObject, const QMetaProperty &property) {
                Q_UNUSED(sourceMetaObject);
                self->m_binding.doConnectNotify(self, property);
            });

    QV4::coerce(
            self->m_binding.engine->handle(), value.metaType(), value.constData(),
            metaType, dataPtr);
    return true;
}

void QQmlUnbindableToBindablePropertyBinding::update()
{
    PendingBindingObserverList bindingObservers;
    evaluateRecursive(bindingObservers);

    if (const QPropertyBindingError error = bindingError();
            Q_UNLIKELY(error.type() == QPropertyBindingError::BindingLoop)) {
        qmlWarning(m_targetObject)
                << "Binding loop detected for property"
                << m_targetObject->metaObject()->property(m_targetPropertyIndex.coreIndex()).name();
        return;
    }

    notifyNonRecursive(bindingObservers);
}

QQmlBindableToBindablePropertyBinding::QQmlBindableToBindablePropertyBinding(
        QQmlEngine *engine, QObject *sourceObject, QQmlPropertyIndex sourcePropertyIndex,
        QObject *targetObject, int targetPropertyIndex)
    : QPropertyBindingPrivate(
              targetObject->metaObject()->property(targetPropertyIndex).metaType(),
              &QtPrivate::bindingFunctionVTableForQQmlPropertyToBindablePropertyBinding<
                      QQmlBindableToBindablePropertyBinding>,
              QPropertyBindingSourceLocation(), true)
    , m_binding(engine, sourceObject, sourcePropertyIndex)
{
}

bool QQmlBindableToBindablePropertyBinding::update(
        QMetaType metaType, QUntypedPropertyData *dataPtr, void *f)
{
    QQmlBindableToBindablePropertyBinding *self
            = reinterpret_cast<QQmlBindableToBindablePropertyBinding *>(
                // Address of QPropertyBindingPrivate subobject
                static_cast<std::byte *>(f) - QPropertyBindingPrivate::getSizeEnsuringAlignment());

    // Bindable-to-bindable captures automatically.
    const QVariant value = self->m_binding.readSourceValue(
            [](const QMetaObject *, const QMetaProperty &) {});

    QV4::coerce(
            self->m_binding.engine->handle(), value.metaType(), value.constData(),
            metaType, dataPtr);
    return true;
}

void QQmlUnbindableToUnbindableGuard_callback(QQmlNotifierEndpoint *e, void **)
{
    static_cast<QQmlUnbindableToUnbindablePropertyBinding *>(e)->update();
}

void QQmlUnbindableToBindableGuard_callback(QQmlNotifierEndpoint *e, void **)
{
    static_cast<QQmlUnbindableToBindablePropertyBinding *>(e)->update();
}

QT_END_NAMESPACE
