// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qdslintplugin.h"

#include <QtCore/qlist.h>
#include <QtCore/qvarlengtharray.h>
#include <QtCore/qhash.h>
#include <QtCore/qset.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;
using namespace QQmlSA;

// note: is a warning, but is prefixed Err to share the name with its QtC codemodel counterpart.
static constexpr LoggerWarningId ErrFunctionsNotSupportedInQmlUi{
    "QtDesignStudio.FunctionsNotSupportedInQmlUi"
};
static constexpr LoggerWarningId WarnReferenceToParentItemNotSupportedByVisualDesigner{
    "QtDesignStudio.ReferenceToParentItemNotSupportedByVisualDesigner"
};
static constexpr LoggerWarningId WarnImperativeCodeNotEditableInVisualDesigner{
    "QtDesignStudio.ImperativeCodeNotEditableInVisualDesigner"
};

class FunctionCallValidator : public PropertyPass
{
public:
    FunctionCallValidator(PassManager *manager) : PropertyPass(manager) { }

    void onCall(const Element &element, const QString &propertyName, const Element &readScope,
                SourceLocation location) override;
};

class QdsBindingValidator : public PropertyPass
{
public:
    QdsBindingValidator(PassManager *manager, const Element &)
        : PropertyPass(manager), m_statesType(resolveType("QtQuick", "State"))
    {
    }

    void onRead(const QQmlSA::Element &element, const QString &propertyName,
                const QQmlSA::Element &readScope, QQmlSA::SourceLocation location) override;

    void onWrite(const QQmlSA::Element &element, const QString &propertyName,
                 const QQmlSA::Element &value, const QQmlSA::Element &writeScope,
                 QQmlSA::SourceLocation location) override;

private:
    Element m_statesType;
};

void QdsBindingValidator::onRead(const QQmlSA::Element &element, const QString &propertyName,
                                 const QQmlSA::Element &readScope, QQmlSA::SourceLocation location)
{
    Q_UNUSED(readScope);

    if (element.isFileRootComponent() && propertyName == u"parent") {
        emitWarning("Referencing the parent of the root item is not supported in a UI file (.ui.qml)",
                    WarnReferenceToParentItemNotSupportedByVisualDesigner, location);
    }
}

void QdsBindingValidator::onWrite(const QQmlSA::Element &, const QString &propertyName,
                                  const QQmlSA::Element &, const QQmlSA::Element &,
                                  QQmlSA::SourceLocation location)
{
    static constexpr std::array forbiddenAssignments = { "baseline"_L1,
                                                         "baselineOffset"_L1,
                                                         "bottomMargin"_L1,
                                                         "centerIn"_L1,
                                                         "color"_L1,
                                                         "fill"_L1,
                                                         "height"_L1,
                                                         "horizontalCenter"_L1,
                                                         "horizontalCenterOffset"_L1,
                                                         "left"_L1,
                                                         "leftMargin"_L1,
                                                         "margins"_L1,
                                                         "mirrored"_L1,
                                                         "opacity"_L1,
                                                         "right"_L1,
                                                         "rightMargin"_L1,
                                                         "rotation"_L1,
                                                         "scale"_L1,
                                                         "topMargin"_L1,
                                                         "verticalCenter"_L1,
                                                         "verticalCenterOffset"_L1,
                                                         "width"_L1,
                                                         "x"_L1,
                                                         "y"_L1,
                                                         "z"_L1 };
    Q_ASSERT(std::is_sorted(forbiddenAssignments.cbegin(), forbiddenAssignments.cend()));
    if (std::find(forbiddenAssignments.cbegin(), forbiddenAssignments.cend(), propertyName)
        != forbiddenAssignments.cend()) {
        emitWarning("Imperative JavaScript assignments can break the visual tooling in Qt Design "
                    "Studio.",
                    WarnImperativeCodeNotEditableInVisualDesigner, location);
    }
}

void QmlLintQdsPlugin::registerPasses(PassManager *manager, const Element &rootElement)
{
    if (!rootElement.filePath().endsWith(u".ui.qml"))
        return;

    manager->registerPropertyPass(std::make_shared<FunctionCallValidator>(manager),
                                  QAnyStringView(), QAnyStringView());
    manager->registerPropertyPass(std::make_shared<QdsBindingValidator>(manager, rootElement),
                                  QAnyStringView(), QAnyStringView());
}

void FunctionCallValidator::onCall(const Element &element, const QString &propertyName,
                                   const Element &readScope, SourceLocation location)
{
    Q_UNUSED(readScope);

    // all math functions are allowed
    const Element globalJSObject = resolveBuiltinType(u"GlobalObject");
    const Element mathObjectType = globalJSObject.property(u"Math"_s).type();
    if (element.inherits(mathObjectType))
        return;

    const Element qjsValue = resolveBuiltinType(u"QJSValue");
    if (element.inherits(qjsValue)) {
        // Workaround because the Date method has methods and those are only represented in
        // QQmlJSTypePropagator as QJSValue.
        // This is an overapproximation and might flag unrelated methods with the same name as ok
        // even if they are not, but this is better than bogus warnings about the valid Date methods.
        const std::array<QStringView, 4> dateMethodmethods{ u"now", u"parse", u"prototype",
                                                            u"UTC" };
        if (auto it = std::find(dateMethodmethods.cbegin(), dateMethodmethods.cend(), propertyName);
            it != dateMethodmethods.cend())
            return;
    }

    static const std::vector<std::pair<Element, std::unordered_set<QString>>>
            whiteListedFunctions = {
                { Element(),
                  {
                          // used on JS objects and many other types
                          u"valueOf"_s,
                          u"toString"_s,
                          u"toLocaleString"_s,
                  } },
                { globalJSObject, { u"isNaN"_s, u"isFinite"_s } },
                { resolveBuiltinType(u"ArrayPrototype"_s), { u"indexOf"_s, u"lastIndexOf"_s } },
                { resolveBuiltinType(u"NumberPrototype"_s),
                  {
                          u"isNaN"_s,
                          u"isFinite"_s,
                          u"toFixed"_s,
                          u"toExponential"_s,
                          u"toPrecision"_s,
                          u"isInteger"_s,
                  } },
                { resolveBuiltinType(u"StringPrototype"_s),
                  {
                          u"arg"_s,
                          u"toLowerCase"_s,
                          u"toLocaleLowerCase"_s,
                          u"toUpperCase"_s,
                          u"toLocaleUpperCase"_s,
                          u"substring"_s,
                          u"charAt"_s,
                          u"charCodeAt"_s,
                          u"concat"_s,
                          u"includes"_s,
                          u"endsWith"_s,
                          u"indexOf"_s,
                          u"lastIndexOf"_s,
                  } },
                { resolveType(u"QtQml"_s, u"Qt"_s),
                  { u"lighter"_s, u"darker"_s, u"rgba"_s, u"tint"_s, u"hsla"_s, u"hsva"_s,
                    u"point"_s, u"rect"_s, u"size"_s, u"vector2d"_s, u"vector3d"_s, u"vector4d"_s,
                    u"quaternion"_s, u"matrix4x4"_s, u"formatDate"_s, u"formatDateTime"_s,
                    u"formatTime"_s, u"resolvedUrl"_s } },
            };

    for (const auto &[currentElement, methods] : whiteListedFunctions) {
        if ((!currentElement || element.inherits(currentElement)) && methods.count(propertyName)) {
            return;
        }
    }

    // all other functions are forbidden
    emitWarning(u"Arbitrary functions and function calls outside of a Connections object are not "
                u"supported in a UI file (.ui.qml)",
                ErrFunctionsNotSupportedInQmlUi, location);
}

QT_END_NAMESPACE

#include "moc_qdslintplugin.cpp"
