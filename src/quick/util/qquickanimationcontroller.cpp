// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qquickanimationcontroller_p.h"
#include <QtQml/qqmlinfo.h>
#include <private/qqmlengine_p.h>

QT_BEGIN_NAMESPACE


class QQuickAnimationControllerPrivate : public QObjectPrivate, QAnimationJobChangeListener
{
    Q_DECLARE_PUBLIC(QQuickAnimationController)
public:
    QQuickAnimationControllerPrivate()
        : progress(0.0), animation(nullptr), animationInstance(nullptr), finalized(false) {}
    void animationFinished(QAbstractAnimationJob *job) override;
    void animationCurrentTimeChanged(QAbstractAnimationJob *job, int currentTime) override;


    qreal progress;
    QQuickAbstractAnimation *animation;
    QAbstractAnimationJob *animationInstance;
    bool finalized:1;

};

void QQuickAnimationControllerPrivate::animationFinished(QAbstractAnimationJob *job)
{
    Q_Q(QQuickAnimationController);
    Q_ASSERT(animationInstance && animationInstance == job);
    Q_UNUSED(job);

    animationInstance->removeAnimationChangeListener(this, QAbstractAnimationJob::Completion | QAbstractAnimationJob::CurrentTime);

    if (animationInstance->direction() == QAbstractAnimationJob::Forward && progress != 1) {
        progress = 1;
        emit q->progressChanged();
    } else if (animationInstance->direction() == QAbstractAnimationJob::Backward && progress != 0) {
        progress = 0;
        emit q->progressChanged();
    }

}

void QQuickAnimationControllerPrivate::animationCurrentTimeChanged(QAbstractAnimationJob *job, int currentTime)
{
    Q_Q(QQuickAnimationController);
    Q_ASSERT(animationInstance && animationInstance == job);
    Q_UNUSED(job);
    const qreal newProgress = currentTime * 1.0 / animationInstance->duration();
    if (progress != newProgress) {
        progress = newProgress;
        emit q->progressChanged();
    }
}

/*!
    \qmltype AnimationController
    \nativetype QQuickAnimationController
    \inqmlmodule QtQuick
    \ingroup qtquick-animation-control
    \brief Enables manual control of animations.

    Normally animations are driven by an internal timer, but the AnimationController
    allows the given \a animation to be driven by a \a progress value explicitly.
*/


QQuickAnimationController::QQuickAnimationController(QObject *parent)
: QObject(*(new QQuickAnimationControllerPrivate), parent)
{
}

QQuickAnimationController::~QQuickAnimationController()
{
    Q_D(QQuickAnimationController);
    delete d->animationInstance;
}

/*!
    \qmlproperty real QtQuick::AnimationController::progress
    This property holds the animation progress value.

    The valid \c progress value is 0.0 to 1.0, setting values less than 0 will be converted to 0,
    setting values great than 1 will be converted to 1.
*/
qreal QQuickAnimationController::progress() const
{
    Q_D(const QQuickAnimationController);
    return d->progress;
}

void QQuickAnimationController::setProgress(qreal progress)
{
    Q_D(QQuickAnimationController);
    progress = qBound(qreal(0), progress, qreal(1));

    if (progress != d->progress) {
        d->progress = progress;
        updateProgress();
        emit progressChanged();
    }
}

/*!
    \qmlproperty Animation QtQuick::AnimationController::animation
    \qmldefault

    This property holds the animation to be controlled by the AnimationController.

    Note:An animation controlled by AnimationController will always have its
         \c running and \c paused properties set to true. It can not be manually
         started or stopped (much like an animation in a Behavior can not be manually started or stopped).
*/
QQuickAbstractAnimation *QQuickAnimationController::animation() const
{
    Q_D(const QQuickAnimationController);
    return d->animation;
}

void QQuickAnimationController::setAnimation(QQuickAbstractAnimation *animation)
{
    Q_D(QQuickAnimationController);

    if (animation != d->animation) {
        if (animation) {
            if (animation->userControlDisabled()) {
                qmlWarning(this) << "QQuickAnimationController::setAnimation: the animation is controlled by others, can't be used in AnimationController.";
                return;
            }
            animation->setDisableUserControl();
        }

        if (d->animation)
            d->animation->setEnableUserControl();

        d->animation = animation;
        reload();
        emit animationChanged();
    }
}

/*!
    \qmlmethod QtQuick::AnimationController::reload()
    \brief Reloads the animation properties

    If the animation properties changed, calling this method to reload the animation definations.
*/
void QQuickAnimationController::reload()
{
    Q_D(QQuickAnimationController);
    if (!d->finalized)
        return;

    if (!d->animation) {
        d->animationInstance = nullptr;
    } else {
        QQuickStateActions actions;
        QQmlProperties properties;
        QAbstractAnimationJob *oldInstance = d->animationInstance;
        d->animationInstance = d->animation->transition(actions, properties, QQuickAbstractAnimation::Forward);
        if (oldInstance && oldInstance != d->animationInstance)
            delete oldInstance;
        if (d->animationInstance) {
            d->animationInstance->setLoopCount(1);
            d->animationInstance->setDisableUserControl();
            d->animationInstance->start();
            d->animationInstance->pause();
            updateProgress();
        }
    }
}

void QQuickAnimationController::updateProgress()
{
    Q_D(QQuickAnimationController);
    if (!d->animationInstance)
        return;

    d->animationInstance->setDisableUserControl();
    d->animationInstance->start();
    QQmlAnimationTimer::instance()->unregisterAnimation(d->animationInstance);
    d->animationInstance->setCurrentTime(d->progress * d->animationInstance->duration());
}

void QQuickAnimationController::componentFinalized()
{
    Q_D(QQuickAnimationController);
    d->finalized = true;
    reload();
}

/*!
    \qmlmethod QtQuick::AnimationController::completeToBeginning()
    \brief Finishes running the controlled animation in a backwards direction.

    After calling this method, the animation runs normally from the current progress point
    in a backwards direction to the beginning state.

    The animation controller's progress value will be automatically updated while the animation is running.

    \sa completeToEnd(), progress
*/
void QQuickAnimationController::completeToBeginning()
{
    Q_D(QQuickAnimationController);
    if (!d->animationInstance)
        return;

    if (d->progress == 0)
        return;

    d->animationInstance->addAnimationChangeListener(d, QAbstractAnimationJob::Completion | QAbstractAnimationJob::CurrentTime);
    d->animationInstance->setDirection(QAbstractAnimationJob::Backward);

    //Disable and then enable user control to trigger the animation instance's state change
    d->animationInstance->setDisableUserControl();
    d->animationInstance->setEnableUserControl();
    d->animationInstance->start();
}

/*!
    \qmlmethod QtQuick::AnimationController::completeToEnd()
    \brief Finishes running the controlled animation in a forwards direction.

    After calling this method, the animation runs normally from the current progress point
    in a forwards direction to the end state.

    The animation controller's progress value will be automatically updated while the animation is running.

    \sa completeToBeginning(), progress
*/
void QQuickAnimationController::completeToEnd()
{
    Q_D(QQuickAnimationController);
    if (!d->animationInstance)
        return;

    if (d->progress == 1)
        return;

    d->animationInstance->addAnimationChangeListener(d, QAbstractAnimationJob::Completion | QAbstractAnimationJob::CurrentTime);
    d->animationInstance->setDirection(QAbstractAnimationJob::Forward);

    //Disable and then enable user control to trigger the animation instance's state change
    d->animationInstance->setDisableUserControl();
    d->animationInstance->setEnableUserControl();
    d->animationInstance->start();
}



QT_END_NAMESPACE


#include "moc_qquickanimationcontroller_p.cpp"
