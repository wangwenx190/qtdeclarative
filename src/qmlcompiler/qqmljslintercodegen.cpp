// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qqmljslintercodegen_p.h"

#include <QtQmlCompiler/private/qqmljsimportvisitor_p.h>
#include <QtQmlCompiler/private/qqmljsshadowcheck_p.h>
#include <QtQmlCompiler/private/qqmljsstoragegeneralizer_p.h>
#include <QtQmlCompiler/private/qqmljsstorageinitializer_p.h>
#include <QtQmlCompiler/private/qqmljstypepropagator_p.h>
#include <QtQmlCompiler/private/qqmljsfunctioninitializer_p.h>

#include <QFileInfo>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

QQmlJSLinterCodegen::QQmlJSLinterCodegen(QQmlJSImporter *importer, const QString &fileName,
                                         const QStringList &qmldirFiles, QQmlJSLogger *logger)
    : QQmlJSAotCompiler(importer, fileName, qmldirFiles, logger)
{
}

void QQmlJSLinterCodegen::setDocument(const QmlIR::JSCodeGen *codegen,
                                      const QmlIR::Document *document)
{
    Q_UNUSED(codegen);
    m_document = document;
    m_unitGenerator = &document->jsGenerator;
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>>
QQmlJSLinterCodegen::compileBinding(const QV4::Compiler::Context *context,
                                    const QmlIR::Binding &irBinding, QQmlJS::AST::Node *astNode)
{
    const QString name = m_document->stringAt(irBinding.propertyNameIndex);
    m_logger->setCompileErrorPrefix(
            u"Could not determine signature of binding for %1: "_s.arg(name));

    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);

    QList<QQmlJS::DiagnosticMessage> initializationErrors;
    QQmlJSCompilePass::Function function =
            initializer.run(context, name, astNode, irBinding, &initializationErrors);

    for (const auto &error : initializationErrors)
        diagnose(error.message, error.type, error.loc);

    m_logger->setCompileErrorPrefix(u"Could not compile binding for %1: "_s.arg(name));

    QList<QQmlJS::DiagnosticMessage> analyzeErrors;
    if (!analyzeFunction(&function, &analyzeErrors)) {
        for (auto &error : analyzeErrors)
            diagnose(error.message, error.type, error.loc);
        return analyzeErrors;
    }

    return QQmlJSAotFunction {};
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>>
QQmlJSLinterCodegen::compileFunction(const QV4::Compiler::Context *context,
                                     const QString &name, QQmlJS::AST::Node *astNode)
{
    m_logger->setCompileErrorPrefix(u"Could not determine signature of function %1: "_s.arg(name));

    QList<QQmlJS::DiagnosticMessage> initializationErrors;
    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);
    QQmlJSCompilePass::Function function =
            initializer.run(context, name, astNode, &initializationErrors);

    for (const auto &error : initializationErrors)
        diagnose(error.message, error.type, error.loc);

    m_logger->setCompileErrorPrefix(u"Could not compile function %1: "_s.arg(name));
    QList<QQmlJS::DiagnosticMessage> analyzeErrors;
    if (!analyzeFunction(&function, &analyzeErrors)) {
        for (auto &error : analyzeErrors)
            error = diagnose(error.message, error.type, error.loc);
        return analyzeErrors;
    }

    return QQmlJSAotFunction {};
}

void QQmlJSLinterCodegen::setPassManager(QQmlSA::PassManager *passManager)
{
    m_passManager = passManager;
    auto managerPriv = QQmlSA::PassManagerPrivate::get(passManager);
    managerPriv->m_typeResolver = typeResolver();
}

bool QQmlJSLinterCodegen::analyzeFunction(
        QQmlJSCompilePass::Function *function, QList<QQmlJS::DiagnosticMessage> *errors)
{
    QQmlJSTypePropagator propagator(m_unitGenerator, &m_typeResolver, m_logger, errors, {}, {},
                                    m_passManager);
    auto [basicBlocks, annotations] = propagator.run(function);
    if (errors->isEmpty()) {
        QQmlJSShadowCheck shadowCheck(m_unitGenerator, &m_typeResolver, m_logger, errors,
                                      basicBlocks, annotations);
        shadowCheck.run(function);
    }

    if (errors->isEmpty()) {
        QQmlJSStorageInitializer initializer(m_unitGenerator, &m_typeResolver, m_logger, errors,
                                             basicBlocks, annotations);
        initializer.run(function);
    }

    if (errors->isEmpty()) {
        QQmlJSStorageGeneralizer generalizer(m_unitGenerator, &m_typeResolver, m_logger, errors,
                                             basicBlocks, annotations);
        generalizer.run(function);
    }

    return errors->isEmpty();
}

QT_END_NAMESPACE
