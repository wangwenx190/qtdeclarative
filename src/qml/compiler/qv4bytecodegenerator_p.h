// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QV4BYTECODEGENERATOR_P_H
#define QV4BYTECODEGENERATOR_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//
#include <private/qv4instr_moth_p.h>
#include <private/qv4compileddata_p.h>
#include <private/qv4compilercontext_p.h>
#include <private/qqmljssourcelocation_p.h>

#include <memory>

QT_BEGIN_NAMESPACE

namespace QQmlJS {
class SourceLocation;
}

namespace QV4 {

namespace Compiler {
struct Context;
}

namespace Moth {

class BytecodeGenerator {
public:
    BytecodeGenerator(int line, bool debug, bool storeSourceLocation = false)
        : startLine(line), debugMode(debug)
    {
        if (storeSourceLocation)
            m_sourceLocationTable.reset(new QV4::Compiler::Context::SourceLocationTable {});
    }

    struct Label {
        enum LinkMode {
            LinkNow,
            LinkLater
        };
        Label() = default;
        Label(BytecodeGenerator *generator, LinkMode mode = LinkNow)
            : generator(generator),
              index(generator->labels.size()) {
            generator->labels.append(-1);
            if (mode == LinkNow)
                link();
        }

        void link() const {
            Q_ASSERT(index >= 0);
            Q_ASSERT(generator->labels[index] == -1);
            generator->labels[index] = generator->instructions.size();
            generator->clearLastInstruction();
        }
        bool isValid() const { return generator != nullptr; }

        BytecodeGenerator *generator = nullptr;
        int index = -1;
    };

    struct Jump {
        Jump(BytecodeGenerator *generator, int instruction)
            : generator(generator),
              index(instruction)
        { Q_ASSERT(generator && index != -1); }

        ~Jump() {
            Q_ASSERT(index == -1 || generator->instructions[index].linkedLabel != -1); // make sure link() got called
        }

        Jump(Jump &&j) {
            std::swap(generator, j.generator);
            std::swap(index, j.index);
        }

        BytecodeGenerator *generator = nullptr;
        int index = -1;

        void link() {
            link(generator->label());
        }
        void link(Label l) {
            Q_ASSERT(l.index >= 0);
            Q_ASSERT(generator->instructions[index].linkedLabel == -1);
            generator->instructions[index].linkedLabel = l.index;
        }

    private:
        // make this type move-only:
        Q_DISABLE_COPY(Jump)
        // we never move-assign this type anywhere, so disable it:
        Jump &operator=(Jump &&) = delete;
    };

    struct ExceptionHandler : public Label {
        ExceptionHandler() = default;
        ExceptionHandler(BytecodeGenerator *generator)
            : Label(generator, LinkLater)
        {
        }
        ~ExceptionHandler()
        {
            Q_ASSERT(!generator || generator->currentExceptionHandler != this);
        }
        ExceptionHandler(const ExceptionHandler &) = default;
        ExceptionHandler(ExceptionHandler &&) = default;
        ExceptionHandler &operator=(const ExceptionHandler &) = default;
        ExceptionHandler &operator=(ExceptionHandler &&) = default;
        bool isValid() const { return generator != nullptr; }
    };

    Label label() {
        return Label(this, Label::LinkNow);
    }

    Label newLabel() {
        return Label(this, Label::LinkLater);
    }

    ExceptionHandler newExceptionHandler() {
        return ExceptionHandler(this);
    }

    template<int InstrT>
    void addInstruction(const InstrData<InstrT> &data)
    {
        Instr genericInstr;
        InstrMeta<InstrT>::setData(genericInstr, data);
        addInstructionHelper(Moth::Instr::Type(InstrT), genericInstr);
    }

    Q_REQUIRED_RESULT Jump jump()
    {
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Wmaybe-uninitialized") // broken gcc warns about Instruction::Debug()
        Instruction::Jump data;
        return addJumpInstruction(data);
QT_WARNING_POP
    }

    Q_REQUIRED_RESULT Jump jumpTrue()
    {
        return addJumpInstruction(Instruction::JumpTrue());
    }

    Q_REQUIRED_RESULT Jump jumpFalse()
    {
        return addJumpInstruction(Instruction::JumpFalse());
    }

    Q_REQUIRED_RESULT Jump jumpNotUndefined()
    {
        Instruction::JumpNotUndefined data{};
        return addJumpInstruction(data);
    }

    Q_REQUIRED_RESULT Jump jumpNoException()
    {
        Instruction::JumpNoException data{};
        return addJumpInstruction(data);
    }

    Q_REQUIRED_RESULT Jump jumpOptionalLookup(int index)
    {
        Instruction::GetOptionalLookup data{};
        data.index = index;
        return addJumpInstruction(data);
    }

    Q_REQUIRED_RESULT Jump jumpOptionalProperty(int name)
    {
        Instruction::LoadOptionalProperty data{};
        data.name = name;
        return addJumpInstruction(data);
    }

    void jumpStrictEqual(const StackSlot &lhs, const Label &target)
    {
        Instruction::CmpStrictEqual cmp;
        cmp.lhs = lhs;
        addInstruction(std::move(cmp));
        addJumpInstruction(Instruction::JumpTrue()).link(target);
    }

    void jumpStrictNotEqual(const StackSlot &lhs, const Label &target)
    {
        Instruction::CmpStrictNotEqual cmp;
        cmp.lhs = lhs;
        addInstruction(std::move(cmp));
        addJumpInstruction(Instruction::JumpTrue()).link(target);
    }

    void checkException()
    {
        Instruction::CheckException chk;
        addInstruction(chk);
    }

    void setUnwindHandler(ExceptionHandler *handler)
    {
        currentExceptionHandler = handler;
        Instruction::SetUnwindHandler data;
        data.offset = 0;
        if (!handler)
            addInstruction(data);
        else
            addJumpInstruction(data).link(*handler);
    }

    void unwindToLabel(int level, const Label &target)
    {
        if (level) {
            Instruction::UnwindToLabel unwind;
            unwind.level = level;
            addJumpInstruction(unwind).link(target);
        } else {
            jump().link(target);
        }
    }



    void setLocation(const QQmlJS::SourceLocation &loc);
    void incrementStatement();

    ExceptionHandler *exceptionHandler() const {
        return currentExceptionHandler;
    }

    int newRegister();
    int newRegisterArray(int n);
    int registerCount() const { return regCount; }
    int currentRegister() const { return currentReg; }

    void finalize(Compiler::Context *context);

    template<int InstrT>
    Jump addJumpInstruction(const InstrData<InstrT> &data)
    {
        Instr genericInstr;
        InstrMeta<InstrT>::setData(genericInstr, data);
        return Jump(this, addInstructionHelper(Moth::Instr::Type(InstrT), genericInstr, offsetof(InstrData<InstrT>, offset)));
    }

    void addCJumpInstruction(bool jumpOnFalse, const Label *trueLabel, const Label *falseLabel)
    {
        if (jumpOnFalse)
            addJumpInstruction(Instruction::JumpFalse()).link(*falseLabel);
        else
            addJumpInstruction(Instruction::JumpTrue()).link(*trueLabel);
    }

    void clearLastInstruction()
    {
        lastInstrType = -1;
    }

    void addLoopStart(const Label &start)
    {
        _labelInfos.push_back({ start.index });
    }

private:
    friend struct Jump;
    friend struct Label;
    friend struct ExceptionHandler;

    int addInstructionHelper(Moth::Instr::Type type, const Instr &i, int offsetOfOffset = -1);

    struct I {
        Moth::Instr::Type type;
        short size;
        uint position;
        int line;
        int statement;
        int offsetForJump;
        int linkedLabel;
        unsigned char packed[sizeof(Instr) + 2]; // 2 for instruction type
    };

    void compressInstructions();
    void packInstruction(I &i);
    void adjustJumpOffsets();

    QVector<I> instructions;
    QVector<int> labels;
    ExceptionHandler *currentExceptionHandler = nullptr;
    int regCount = 0;
public:
    int currentReg = 0;
private:
    int startLine = 0;
    int currentLine = 0;
    int currentStatement = 0;
    QQmlJS::SourceLocation currentSourceLocation;
    std::unique_ptr<QV4::Compiler::Context::SourceLocationTable> m_sourceLocationTable;
    bool debugMode = false;

    int lastInstrType = -1;
    Moth::Instr lastInstr;

    struct LabelInfo {
        int labelIndex;
    };
    std::vector<LabelInfo> _labelInfos;
};

}
}

QT_END_NAMESPACE

#endif
