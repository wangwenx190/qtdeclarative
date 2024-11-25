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
    // ContentVariant determines the relation between this register content and its scope().
    // For example, a property is always a property of a type. That type is given as scope.
    // Most content variants can carry either a specific kind of content, as commented below,
    // or a conversion. If two or more register contents of the same content variant are merged,
    // they retain their content variant but become a conversion with the original register
    // contents linked as conversion origins.

    enum ContentVariant {
        ObjectById,   // type (scope is QML scope of binding/function)
        TypeByName,   // type (TODO: scope is not guaranteed to be useful)
        Singleton,    // type (scope is either import namespace or QML scope)
        Script,       // type (scope is either import namespace or QML scope)
        MetaType,     // type (always QMetaObject, scope is the type reprented by the metaobject)
        Extension,    // type (scope is the type being extended)
        ScopeObject,  // type (either QML scope of binding/function or JS global object)
        ParentScope,  // type (scope is the child scope)

        Property,     // property (scope is the owner (hasOwnProperty) of the property)
        Method,       // method (retrieved as property, including overloads), like property
        Enum,         // enumeration (scope is the type the enumeration belongs to)

        Attachment,   // type (scope is attacher; use attacher() and attachee() for clarity)
        ModulePrefix, // import namespace (scope is either QML scope or type the prefix is used on)

        MethodCall,   // method call (resolved to specific overload), like property

        ListValue,    // property (scope is list retrieved from)
        ListIterator, // property (scope is list being iterated)

        Literal,      // type (scope does not exist)
        Operation,    // type (scope does not exist)

        BaseType,     // type (scope is derived type)
        Cast,         // type (scope is type casted from)

        Storage,      // type (scope does not exist)

        // Either a synthetic type or a merger of multiple different variants.
        // In the latter case, look at conversion origins to find out more.
        // Synthetic types should be short lived.
        Unknown,the
    };

    enum { InvalidLookupIndex = -1 };

    QQmlJSRegisterContent() = default;


    // General properties of the register content, (mostly) independent of kind or variant

    bool isNull() const { return !d; }
    bool isValid() const;

    bool isList() const;
    bool isWritable() const;

    ContentVariant variant() const;

    QString descriptiveName() const;
    QString containedTypeName() const;

    int resultLookupIndex() const;

    QQmlJSScope::ConstPtr storedType() const;
    QQmlJSScope::ConstPtr containedType() const;
    QQmlJSScope::ConstPtr scopeType() const;

    bool contains(const QQmlJSScope::ConstPtr &type) const { return type == containedType(); }
    bool isStoredIn(const QQmlJSScope::ConstPtr &type) const { return type == storedType(); }


    // Properties of specific kinds of register contents

    bool isType() const;
    QQmlJSScope::ConstPtr type() const;

    bool isProperty() const;
    QQmlJSMetaProperty property() const;
    int baseLookupIndex() const;

    bool isEnumeration() const;
    QQmlJSMetaEnum enumeration() const;
    QString enumMember() const;

    bool isMethod() const;
    QList<QQmlJSMetaMethod> method() const;
    QQmlJSScope::ConstPtr methodType() const;

    bool isImportNamespace() const;
    uint importNamespace() const;
    QQmlJSScope::ConstPtr importNamespaceType() const;

    bool isConversion() const;
    QQmlJSScope::ConstPtr conversionResultType() const;
    QQmlJSRegisterContent conversionResultScope() const;
    QList<QQmlJSRegisterContent> conversionOrigins() const;

    bool isMethodCall() const;
    QQmlJSMetaMethod methodCall() const;
    bool isJavaScriptReturnValue() const;


    // Linked register contents

    QQmlJSRegisterContent attacher() const;
    QQmlJSRegisterContent attachee() const;

    QQmlJSRegisterContent scope() const;
    QQmlJSRegisterContent storage() const;
    QQmlJSRegisterContent original() const;
    QQmlJSRegisterContent shadowed() const;

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

    QQmlJSRegisterContentPrivate *d = nullptr;
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

    QQmlJSRegisterContent clone(const QQmlJSRegisterContent from) { return clone(from.d); }

    void adjustType(
            const QQmlJSRegisterContent &content, const QQmlJSScope::ConstPtr &adjusted);
    void generalizeType(
            const QQmlJSRegisterContent &content, const QQmlJSScope::ConstPtr &generalized);


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
};

QT_END_NAMESPACE

#endif // REGISTERCONTENT_H
