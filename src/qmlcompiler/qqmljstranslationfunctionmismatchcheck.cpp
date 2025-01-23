// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qqmljstranslationfunctionmismatchcheck_p.h"
#include "qqmljslogger_p.h"

QT_BEGIN_NAMESPACE

void QQmlJSTranslationFunctionMismatchCheck::onCall(const QQmlSA::Element &element,
                                                    const QString &propertyName,
                                                    const QQmlSA::Element &readScope,
                                                    QQmlSA::SourceLocation location)
{
    Q_UNUSED(readScope);

    const QQmlSA::Element globalJSObject = resolveBuiltinType(u"GlobalObject");
    if (element != globalJSObject)
        return;

    static const std::array<QStringView, 4> translationFunctions = { u"qsTranslate",
                                                                     u"QT_TRANSLATE_NOOP", u"qsTr",
                                                                     u"QT_TR_NOOP" };
    static const std::array<QStringView, 2> idTranslationFunctions = { u"qsTrId", u"QT_TRID_NOOP" };

    const bool isTranslation =
            std::find(translationFunctions.cbegin(), translationFunctions.cend(), propertyName)
            != translationFunctions.cend();
    const bool isIdTranslation =
            std::find(idTranslationFunctions.cbegin(), idTranslationFunctions.cend(), propertyName)
            != idTranslationFunctions.cend();

    if (!isTranslation && !isIdTranslation)
        return;

    const TranslationType current = isTranslation ? Normal : IdBased;

    if (m_lastTranslationFunction == None) {
        m_lastTranslationFunction = current;
        return;
    }

    if (m_lastTranslationFunction != current) {
        emitWarning("Do not mix translation functions", qmlTranslationFunctionMismatch, location);
    }
}

QT_END_NAMESPACE
