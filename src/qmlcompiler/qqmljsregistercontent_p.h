// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef QQMLJSREGISTERCONTENT_P_H
#define QQMLJSREGISTERCONTENT_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.

#include "qqmljsscope_p.h"
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

struct QQmlJSRegisterContentPrivate;
class Q_QMLCOMPILER_EXPORT QQmlJSRegisterContent
{
public:
    enum ContentVariant {
        ObjectById,
        TypeByName,
        Singleton,
        Script,
        MetaType,
        Extension,
        ScopeObject,
        ParentScope,

        Property,
        Method,
        Enum,

        Attachment,
        ModulePrefix,

        MethodCall,

        ListValue,
        ListIterator,

        Literal,
        Operation,

        BaseType,
        Unknown,
    };

    enum { InvalidLookupIndex = -1 };

    QQmlJSRegisterContent();
    bool isValid() const;

    QString descriptiveName() const;
    QString containedTypeName() const;

    bool isType() const;
    bool isProperty() const;
    bool isEnumeration() const;
    bool isMethod() const;
    bool isImportNamespace() const;
    bool isConversion() const;
    bool isMethodCall() const;
    bool isList() const;

    bool isWritable() const;

    bool isJavaScriptReturnValue() const;

    QQmlJSRegisterContent attacher() const;
    QQmlJSRegisterContent attachee() const;

    QQmlJSScope::ConstPtr storedType() const;
    QQmlJSScope::ConstPtr containedType() const;
    QQmlJSRegisterContent scopeType() const;

    QQmlJSScope::ConstPtr type() const;
    QQmlJSMetaProperty property() const;
    int baseLookupIndex() const;

    int resultLookupIndex() const;

    QQmlJSMetaEnum enumeration() const;
    QString enumMember() const;
    QList<QQmlJSMetaMethod> method() const;
    QQmlJSScope::ConstPtr methodType() const;
    uint importNamespace() const;
    QQmlJSScope::ConstPtr importNamespaceType() const;
    QQmlJSScope::ConstPtr conversionResult() const;
    QQmlJSRegisterContent conversionResultScope() const;
    QList<QQmlJSRegisterContent> conversionOrigins() const;
    QQmlJSMetaMethod methodCall() const;
    ContentVariant variant() const;

private:
    friend class QQmlJSRegisterContentPool;
    // TODO: Constant string/number/bool/enumval

    QQmlJSRegisterContent(QQmlJSRegisterContentPrivate *dd) : d(dd) {};

    friend bool operator==(const QQmlJSRegisterContent &a, const QQmlJSRegisterContent &b)
    {
        return a.d == b.d;
    }

    friend bool operator!=(const QQmlJSRegisterContent &a, const QQmlJSRegisterContent &b)
    {
        return !(a == b);
    }

    friend size_t qHash(const QQmlJSRegisterContent &registerContent, size_t seed = 0)
    {
        return qHash(registerContent.d, seed);
    }

    const QQmlJSRegisterContentPrivate *d = nullptr;
};

class Q_QMLCOMPILER_EXPORT QQmlJSRegisterContentPool
{
    Q_DISABLE_COPY_MOVE(QQmlJSRegisterContentPool)
public:
    using ContentVariant = QQmlJSRegisterContent::ContentVariant;

    QQmlJSRegisterContentPool();
    ~QQmlJSRegisterContentPool();

    QQmlJSRegisterContent create(const QQmlJSScope::ConstPtr &type,
                                 int resultLookupIndex, ContentVariant variant,
                                 const QQmlJSRegisterContent &scope = {});

    QQmlJSRegisterContent create(const QQmlJSMetaProperty &property,
                                 int baseLookupIndex, int resultLookupIndex,
                                 ContentVariant variant,
                                 const QQmlJSRegisterContent &scope);

    QQmlJSRegisterContent create(const QQmlJSMetaEnum &enumeration,
                                 const QString &enumMember, ContentVariant variant,
                                 const QQmlJSRegisterContent &scope);

    QQmlJSRegisterContent create(const QList<QQmlJSMetaMethod> &methods,
                                 const QQmlJSScope::ConstPtr &methodType,
                                 ContentVariant variant,
                                 const QQmlJSRegisterContent &scope);

    QQmlJSRegisterContent create(const QQmlJSMetaMethod &method,
                                 const QQmlJSScope::ConstPtr &returnType,
                                 const QQmlJSRegisterContent &scope);

    QQmlJSRegisterContent create(uint importNamespaceStringId,
                                 const QQmlJSScope::ConstPtr &importNamespaceType,
                                 ContentVariant variant,
                                 const QQmlJSRegisterContent &scope = {});

    QQmlJSRegisterContent create(const QList<QQmlJSRegisterContent> &origins,
                                 const QQmlJSScope::ConstPtr &conversion,
                                 const QQmlJSRegisterContent &conversionScope,
                                 ContentVariant variant,
                                 const QQmlJSRegisterContent &scope = {});

    QQmlJSRegisterContent storedIn(
            const QQmlJSRegisterContent &content, const QQmlJSScope::ConstPtr &newStoredType);

    QQmlJSRegisterContent castTo(
            const QQmlJSRegisterContent &content, const QQmlJSScope::ConstPtr &newContainedType);

    static const QQmlJSRegisterContentPrivate *invalid() { return &s_invalid; }

private:
    struct Deleter {
        // It's a template so that we only need the QQmlJSRegisterContentPrivate dtor on usage.
        template<typename Private>
        constexpr void operator()(Private *d) const { delete d; }
    };

    using Pool = std::vector<std::unique_ptr<QQmlJSRegisterContentPrivate, Deleter>>;


    QQmlJSRegisterContentPrivate *clone(const QQmlJSRegisterContentPrivate *from);
    QQmlJSRegisterContentPrivate *create() { return clone(nullptr); }
    QQmlJSRegisterContentPrivate *create(
            const QQmlJSRegisterContent &scope, ContentVariant variant);

    Pool m_pool;
    static const QQmlJSRegisterContentPrivate s_invalid;
};

QT_END_NAMESPACE

#endif // REGISTERCONTENT_H
