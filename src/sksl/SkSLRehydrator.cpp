/*
 * Copyright 2020 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/SkSLRehydrator.h"

#include <memory>
#include <unordered_set>

#include "include/private/SkSLModifiers.h"
#include "include/private/SkSLProgramElement.h"
#include "include/private/SkSLStatement.h"
#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLBreakStatement.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLConstructorArray.h"
#include "src/sksl/ir/SkSLConstructorCompound.h"
#include "src/sksl/ir/SkSLConstructorCompoundCast.h"
#include "src/sksl/ir/SkSLConstructorDiagonalMatrix.h"
#include "src/sksl/ir/SkSLConstructorMatrixResize.h"
#include "src/sksl/ir/SkSLConstructorScalarCast.h"
#include "src/sksl/ir/SkSLConstructorSplat.h"
#include "src/sksl/ir/SkSLConstructorStruct.h"
#include "src/sksl/ir/SkSLContinueStatement.h"
#include "src/sksl/ir/SkSLDiscardStatement.h"
#include "src/sksl/ir/SkSLDoStatement.h"
#include "src/sksl/ir/SkSLEnum.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLField.h"
#include "src/sksl/ir/SkSLFieldAccess.h"
#include "src/sksl/ir/SkSLFloatLiteral.h"
#include "src/sksl/ir/SkSLForStatement.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"
#include "src/sksl/ir/SkSLFunctionDefinition.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLIndexExpression.h"
#include "src/sksl/ir/SkSLInlineMarker.h"
#include "src/sksl/ir/SkSLIntLiteral.h"
#include "src/sksl/ir/SkSLInterfaceBlock.h"
#include "src/sksl/ir/SkSLPostfixExpression.h"
#include "src/sksl/ir/SkSLPrefixExpression.h"
#include "src/sksl/ir/SkSLReturnStatement.h"
#include "src/sksl/ir/SkSLSetting.h"
#include "src/sksl/ir/SkSLStructDefinition.h"
#include "src/sksl/ir/SkSLSwitchCase.h"
#include "src/sksl/ir/SkSLSwitchStatement.h"
#include "src/sksl/ir/SkSLSwizzle.h"
#include "src/sksl/ir/SkSLSymbolAlias.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLTernaryExpression.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLUnresolvedFunction.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"

namespace SkSL {

class AutoRehydratorSymbolTable {
public:
    AutoRehydratorSymbolTable(Rehydrator* rehydrator)
        : fRehydrator(rehydrator)
        , fOldSymbols(fRehydrator->fSymbolTable) {
        fRehydrator->fSymbolTable = fRehydrator->symbolTable();
    }

    ~AutoRehydratorSymbolTable() {
        fRehydrator->fSymbolTable = std::move(fOldSymbols);
    }

private:
    Rehydrator* fRehydrator;
    std::shared_ptr<SymbolTable> fOldSymbols;
};

Rehydrator::Rehydrator(const Context* context,  std::shared_ptr<SymbolTable> symbolTable,
                       const uint8_t* src, size_t length)
                : fContext(*context)
                , fSymbolTable(std::move(symbolTable))
                , fStart(src)
    SkDEBUGCODE(, fEnd(fStart + length)) {
    SkASSERT(fSymbolTable);
    SkASSERT(fSymbolTable->isBuiltin());
    // skip past string data
    fIP = fStart;
    fIP += this->readU16();
}

Layout Rehydrator::layout() {
    switch (this->readU8()) {
        case kBuiltinLayout_Command: {
            Layout result;
            result.fBuiltin = this->readS16();
            return result;
        }
        case kDefaultLayout_Command:
            return Layout();
        case kLayout_Command: {
            int flags = this->readU32();
            int location = this->readS8();
            int offset = this->readS8();
            int binding = this->readS8();
            int index = this->readS8();
            int set = this->readS8();
            int builtin = this->readS16();
            int inputAttachmentIndex = this->readS8();
            int primitive = this->readS8();
            int maxVertices = this->readS8();
            int invocations = this->readS8();
            skstd::string_view when = this->readString();
            int ctype = this->readS8();
            return Layout(flags, location, offset, binding, index, set, builtin,
                          inputAttachmentIndex, (Layout::Primitive)primitive, maxVertices,
                          invocations, when, (Layout::CType)ctype);
        }
        default:
            SkASSERT(false);
            return Layout();
    }
}

Modifiers Rehydrator::modifiers() {
    switch (this->readU8()) {
        case kDefaultModifiers_Command:
            return Modifiers();
        case kModifiers8Bit_Command: {
            Layout l = this->layout();
            int flags = this->readU8();
            return Modifiers(l, flags);
        }
        case kModifiers_Command: {
            Layout l = this->layout();
            int flags = this->readS32();
            return Modifiers(l, flags);
        }
        default:
            SkASSERT(false);
            return Modifiers();
    }
}

const Symbol* Rehydrator::symbol() {
    int kind = this->readU8();
    switch (kind) {
        case kArrayType_Command: {
            uint16_t id = this->readU16();
            const Type* componentType = this->type();
            int8_t count = this->readS8();
            String name(componentType->name());
            if (count == Type::kUnsizedArray) {
                name += "[]";
            } else {
                name += "[" + to_string(count) + "]";
            }
            const Type* result = fSymbolTable->takeOwnershipOfSymbol(
                    Type::MakeArrayType(name, *componentType, count));
            this->addSymbol(id, result);
            return result;
        }
        case kEnumType_Command: {
            uint16_t id = this->readU16();
            skstd::string_view name = this->readString();
            const Type* result =
                    fSymbolTable->takeOwnershipOfSymbol(Type::MakeEnumType(String(name)));
            this->addSymbol(id, result);
            return result;
        }
        case kFunctionDeclaration_Command: {
            uint16_t id = this->readU16();
            Modifiers modifiers = this->modifiers();
            skstd::string_view name = this->readString();
            int parameterCount = this->readU8();
            std::vector<const Variable*> parameters;
            parameters.reserve(parameterCount);
            for (int i = 0; i < parameterCount; ++i) {
                parameters.push_back(this->symbolRef<Variable>(Symbol::Kind::kVariable));
            }
            const Type* returnType = this->type();
            const FunctionDeclaration* result =
                    fSymbolTable->takeOwnershipOfSymbol(std::make_unique<FunctionDeclaration>(
                            /*offset=*/-1,
                            this->modifiersPool().add(modifiers),
                            name,
                            std::move(parameters),
                            returnType,
                            /*builtin=*/true));
            this->addSymbol(id, result);
            return result;
        }
        case kField_Command: {
            const Variable* owner = this->symbolRef<Variable>(Symbol::Kind::kVariable);
            uint8_t index = this->readU8();
            const Field* result = fSymbolTable->takeOwnershipOfSymbol(
                    std::make_unique<Field>(/*offset=*/-1, owner, index));
            return result;
        }
        case kStructType_Command: {
            uint16_t id = this->readU16();
            String name(this->readString());
            uint8_t fieldCount = this->readU8();
            std::vector<Type::Field> fields;
            fields.reserve(fieldCount);
            for (int i = 0; i < fieldCount; ++i) {
                Modifiers m = this->modifiers();
                skstd::string_view fieldName = this->readString();
                const Type* type = this->type();
                fields.emplace_back(m, fieldName, type);
            }
            const Type* result = fSymbolTable->takeOwnershipOfSymbol(
                    Type::MakeStructType(/*offset=*/-1, name, std::move(fields)));
            this->addSymbol(id, result);
            return result;
        }
        case kSymbolRef_Command: {
            uint16_t id = this->readU16();
            SkASSERT(fSymbols.size() > id);
            return fSymbols[id];
        }
        case kSymbolAlias_Command: {
            uint16_t id = this->readU16();
            skstd::string_view name = this->readString();
            const Symbol* origSymbol = this->symbol();
            const SymbolAlias* symbolAlias = fSymbolTable->takeOwnershipOfSymbol(
                    std::make_unique<SymbolAlias>(/*offset=*/-1, name, origSymbol));
            this->addSymbol(id, symbolAlias);
            return symbolAlias;
        }
        case kSystemType_Command: {
            uint16_t id = this->readU16();
            skstd::string_view name = this->readString();
            const Symbol* result = (*fSymbolTable)[name];
            SkASSERT(result && result->kind() == Symbol::Kind::kType);
            this->addSymbol(id, result);
            return result;
        }
        case kUnresolvedFunction_Command: {
            uint16_t id = this->readU16();
            int length = this->readU8();
            std::vector<const FunctionDeclaration*> functions;
            functions.reserve(length);
            for (int i = 0; i < length; ++i) {
                const Symbol* f = this->symbol();
                SkASSERT(f && f->kind() == Symbol::Kind::kFunctionDeclaration);
                functions.push_back((const FunctionDeclaration*) f);
            }
            const UnresolvedFunction* result = fSymbolTable->takeOwnershipOfSymbol(
                    std::make_unique<UnresolvedFunction>(std::move(functions)));
            this->addSymbol(id, result);
            return result;
        }
        case kVariable_Command: {
            uint16_t id = this->readU16();
            const Modifiers* m = this->modifiersPool().add(this->modifiers());
            skstd::string_view name = this->readString();
            const Type* type = this->type();
            Variable::Storage storage = (Variable::Storage) this->readU8();
            const Variable* result = fSymbolTable->takeOwnershipOfSymbol(std::make_unique<Variable>(
                    /*offset=*/-1, m, name, type, /*builtin=*/true, storage));
            this->addSymbol(id, result);
            return result;
        }
        default:
            printf("unsupported symbol %d\n", kind);
            SkASSERT(false);
            return nullptr;
    }
}

const Type* Rehydrator::type() {
    const Symbol* result = this->symbol();
    SkASSERT(result->kind() == Symbol::Kind::kType);
    return (const Type*) result;
}

std::vector<std::unique_ptr<ProgramElement>> Rehydrator::elements() {
    SkDEBUGCODE(uint8_t command = )this->readU8();
    SkASSERT(command == kElements_Command);
    std::vector<std::unique_ptr<ProgramElement>> result;
    while (std::unique_ptr<ProgramElement> elem = this->element()) {
        result.push_back(std::move(elem));
    }
    return result;
}

std::unique_ptr<ProgramElement> Rehydrator::element() {
    int kind = this->readU8();
    switch (kind) {
        case Rehydrator::kEnum_Command: {
            skstd::string_view typeName = this->readString();
            std::shared_ptr<SymbolTable> symbols = this->symbolTable(/*inherit=*/false);
            for (auto& s : symbols->fOwnedSymbols) {
                SkASSERT(s->kind() == Symbol::Kind::kVariable);
                Variable& v = (Variable&) *s;
                int value = this->readS32();
                // enum variables aren't really 'declared', but we have to create a declaration to
                // store the value
                auto valueLiteral = IntLiteral::Make(fContext, /*offset=*/-1, value);
                auto declaration = VarDeclaration::Make(fContext, &v, &v.type(), /*arraySize=*/0,
                                                        std::move(valueLiteral));
                symbols->takeOwnershipOfIRNode(std::move(declaration));
            }
            return std::make_unique<Enum>(/*offset=*/-1, typeName, std::move(symbols),
                                          /*isSharedWithCpp=*/true, /*isBuiltin=*/true);
        }
        case Rehydrator::kFunctionDefinition_Command: {
            const FunctionDeclaration* decl = this->symbolRef<FunctionDeclaration>(
                                                                Symbol::Kind::kFunctionDeclaration);
            std::unique_ptr<Statement> body = this->statement();
            std::unordered_set<const FunctionDeclaration*> refs;
            uint8_t refCount = this->readU8();
            for (int i = 0; i < refCount; ++i) {
                refs.insert(this->symbolRef<FunctionDeclaration>(
                                                               Symbol::Kind::kFunctionDeclaration));
            }
            auto result = std::make_unique<FunctionDefinition>(/*offset=*/-1, decl,
                                                               /*builtin=*/true, std::move(body),
                                                               std::move(refs));
            decl->setDefinition(result.get());
            return std::move(result);
        }
        case Rehydrator::kInterfaceBlock_Command: {
            const Symbol* var = this->symbol();
            SkASSERT(var && var->is<Variable>());
            String typeName(this->readString());
            String instanceName(this->readString());
            int arraySize = this->readS8();
            return std::make_unique<InterfaceBlock>(/*offset=*/-1, &var->as<Variable>(),
                                                    std::move(typeName), std::move(instanceName),
                                                    arraySize, nullptr);
        }
        case Rehydrator::kVarDeclarations_Command: {
            std::unique_ptr<Statement> decl = this->statement();
            return std::make_unique<GlobalVarDeclaration>(std::move(decl));
        }
        case Rehydrator::kStructDefinition_Command: {
            const Symbol* type = this->symbol();
            SkASSERT(type && type->is<Type>());
            return std::make_unique<StructDefinition>(/*offset=*/-1, type->as<Type>());
        }
        case Rehydrator::kElementsComplete_Command:
            return nullptr;
        default:
            SkDEBUGFAILF("unsupported element %d\n", kind);
            return nullptr;
    }
}

std::unique_ptr<Statement> Rehydrator::statement() {
    int kind = this->readU8();
    switch (kind) {
        case Rehydrator::kBlock_Command: {
            AutoRehydratorSymbolTable symbols(this);
            int count = this->readU8();
            StatementArray statements;
            statements.reserve_back(count);
            for (int i = 0; i < count; ++i) {
                statements.push_back(this->statement());
            }
            bool isScope = this->readU8();
            return Block::Make(/*offset=*/-1, std::move(statements), fSymbolTable, isScope);
        }
        case Rehydrator::kBreak_Command:
            return BreakStatement::Make(/*offset=*/-1);
        case Rehydrator::kContinue_Command:
            return ContinueStatement::Make(/*offset=*/-1);
        case Rehydrator::kDiscard_Command:
            return DiscardStatement::Make(/*offset=*/-1);
        case Rehydrator::kDo_Command: {
            std::unique_ptr<Statement> stmt = this->statement();
            std::unique_ptr<Expression> expr = this->expression();
            return DoStatement::Make(fContext, std::move(stmt), std::move(expr));
        }
        case Rehydrator::kExpressionStatement_Command: {
            std::unique_ptr<Expression> expr = this->expression();
            return ExpressionStatement::Make(fContext, std::move(expr));
        }
        case Rehydrator::kFor_Command: {
            std::unique_ptr<Statement> initializer = this->statement();
            std::unique_ptr<Expression> test = this->expression();
            std::unique_ptr<Expression> next = this->expression();
            std::unique_ptr<Statement> body = this->statement();
            std::shared_ptr<SymbolTable> symbols = this->symbolTable();
            return ForStatement::Make(fContext, /*offset=*/-1, std::move(initializer),
                                      std::move(test), std::move(next), std::move(body),
                                      std::move(symbols));
        }
        case Rehydrator::kIf_Command: {
            bool isStatic = this->readU8();
            std::unique_ptr<Expression> test = this->expression();
            std::unique_ptr<Statement> ifTrue = this->statement();
            std::unique_ptr<Statement> ifFalse = this->statement();
            return IfStatement::Make(fContext, /*offset=*/-1, isStatic, std::move(test),
                                     std::move(ifTrue), std::move(ifFalse));
        }
        case Rehydrator::kInlineMarker_Command: {
            const FunctionDeclaration* funcDecl = this->symbolRef<FunctionDeclaration>(
                                                          Symbol::Kind::kFunctionDeclaration);
            return InlineMarker::Make(funcDecl);
        }
        case Rehydrator::kReturn_Command: {
            std::unique_ptr<Expression> expr = this->expression();
            return ReturnStatement::Make(/*offset=*/-1, std::move(expr));
        }
        case Rehydrator::kSwitch_Command: {
            bool isStatic = this->readU8();
            AutoRehydratorSymbolTable symbols(this);
            std::unique_ptr<Expression> expr = this->expression();
            int caseCount = this->readU8();
            StatementArray cases;
            cases.reserve_back(caseCount);
            for (int i = 0; i < caseCount; ++i) {
                std::unique_ptr<Expression> value = this->expression();
                std::unique_ptr<Statement> statement = this->statement();
                cases.push_back(std::make_unique<SwitchCase>(/*offset=*/-1, std::move(value),
                                                             std::move(statement)));
            }
            return SwitchStatement::Make(fContext, /*offset=*/-1, isStatic, std::move(expr),
                                         std::move(cases), fSymbolTable);
        }
        case Rehydrator::kVarDeclaration_Command: {
            Variable* var = this->symbolRef<Variable>(Symbol::Kind::kVariable);
            const Type* baseType = this->type();
            int arraySize = this->readS8();
            std::unique_ptr<Expression> value = this->expression();
            return VarDeclaration::Make(fContext, var, baseType, arraySize, std::move(value));
        }
        case Rehydrator::kVoid_Command:
            return nullptr;
        default:
            printf("unsupported statement %d\n", kind);
            SkASSERT(false);
            return nullptr;
    }
}

ExpressionArray Rehydrator::expressionArray() {
    uint8_t count = this->readU8();
    ExpressionArray array;
    array.reserve_back(count);
    for (int i = 0; i < count; ++i) {
        array.push_back(this->expression());
    }
    return array;
}

std::unique_ptr<Expression> Rehydrator::expression() {
    int kind = this->readU8();
    switch (kind) {
        case Rehydrator::kBinary_Command: {
            std::unique_ptr<Expression> left = this->expression();
            Token::Kind op = (Token::Kind) this->readU8();
            std::unique_ptr<Expression> right = this->expression();
            return BinaryExpression::Make(fContext, std::move(left), op, std::move(right));
        }
        case Rehydrator::kBoolLiteral_Command: {
            bool value = this->readU8();
            return BoolLiteral::Make(fContext, /*offset=*/-1, value);
        }
        case Rehydrator::kConstructorArray_Command: {
            const Type* type = this->type();
            return ConstructorArray::Make(fContext, /*offset=*/-1, *type, this->expressionArray());
        }
        case Rehydrator::kConstructorCompound_Command: {
            const Type* type = this->type();
            return ConstructorCompound::Make(fContext, /*offset=*/-1, *type,
                                              this->expressionArray());
        }
        case Rehydrator::kConstructorDiagonalMatrix_Command: {
            const Type* type = this->type();
            ExpressionArray args = this->expressionArray();
            SkASSERT(args.size() == 1);
            return ConstructorDiagonalMatrix::Make(fContext, /*offset=*/-1, *type,
                                                   std::move(args[0]));
        }
        case Rehydrator::kConstructorMatrixResize_Command: {
            const Type* type = this->type();
            ExpressionArray args = this->expressionArray();
            SkASSERT(args.size() == 1);
            return ConstructorMatrixResize::Make(fContext, /*offset=*/-1, *type,
                                                 std::move(args[0]));
        }
        case Rehydrator::kConstructorScalarCast_Command: {
            const Type* type = this->type();
            ExpressionArray args = this->expressionArray();
            SkASSERT(args.size() == 1);
            return ConstructorScalarCast::Make(fContext, /*offset=*/-1, *type, std::move(args[0]));
        }
        case Rehydrator::kConstructorSplat_Command: {
            const Type* type = this->type();
            ExpressionArray args = this->expressionArray();
            SkASSERT(args.size() == 1);
            return ConstructorSplat::Make(fContext, /*offset=*/-1, *type, std::move(args[0]));
        }
        case Rehydrator::kConstructorStruct_Command: {
            const Type* type = this->type();
            return ConstructorStruct::Make(fContext, /*offset=*/-1, *type, this->expressionArray());
        }
        case Rehydrator::kConstructorCompoundCast_Command: {
            const Type* type = this->type();
            ExpressionArray args = this->expressionArray();
            SkASSERT(args.size() == 1);
            return ConstructorCompoundCast::Make(fContext,/*offset=*/-1, *type, std::move(args[0]));
        }
        case Rehydrator::kFieldAccess_Command: {
            std::unique_ptr<Expression> base = this->expression();
            int index = this->readU8();
            FieldAccess::OwnerKind ownerKind = (FieldAccess::OwnerKind) this->readU8();
            return FieldAccess::Make(fContext, std::move(base), index, ownerKind);
        }
        case Rehydrator::kFloatLiteral_Command: {
            const Type* type = this->type();
            FloatIntUnion u;
            u.fInt = this->readS32();
            return FloatLiteral::Make(/*offset=*/-1, u.fFloat, type);
        }
        case Rehydrator::kFunctionCall_Command: {
            const Type* type = this->type();
            const FunctionDeclaration* f = this->symbolRef<FunctionDeclaration>(
                                                                Symbol::Kind::kFunctionDeclaration);
            ExpressionArray args = this->expressionArray();
            return FunctionCall::Make(fContext, /*offset=*/-1, type, *f, std::move(args));
        }
        case Rehydrator::kIndex_Command: {
            std::unique_ptr<Expression> base = this->expression();
            std::unique_ptr<Expression> index = this->expression();
            return IndexExpression::Make(fContext, std::move(base), std::move(index));
        }
        case Rehydrator::kIntLiteral_Command: {
            const Type* type = this->type();
            int value = this->readS32();
            return IntLiteral::Make(/*offset=*/-1, value, type);
        }
        case Rehydrator::kPostfix_Command: {
            Token::Kind op = (Token::Kind) this->readU8();
            std::unique_ptr<Expression> operand = this->expression();
            return PostfixExpression::Make(fContext, std::move(operand), op);
        }
        case Rehydrator::kPrefix_Command: {
            Token::Kind op = (Token::Kind) this->readU8();
            std::unique_ptr<Expression> operand = this->expression();
            return PrefixExpression::Make(fContext, op, std::move(operand));
        }
        case Rehydrator::kSetting_Command: {
            String name(this->readString());
            return Setting::Convert(fContext, /*offset=*/-1, name);
        }
        case Rehydrator::kSwizzle_Command: {
            std::unique_ptr<Expression> base = this->expression();
            int count = this->readU8();
            ComponentArray components;
            for (int i = 0; i < count; ++i) {
                components.push_back(this->readU8());
            }
            return Swizzle::Make(fContext, std::move(base), components);
        }
        case Rehydrator::kTernary_Command: {
            std::unique_ptr<Expression> test = this->expression();
            std::unique_ptr<Expression> ifTrue = this->expression();
            std::unique_ptr<Expression> ifFalse = this->expression();
            return TernaryExpression::Make(fContext, std::move(test),
                                           std::move(ifTrue), std::move(ifFalse));
        }
        case Rehydrator::kVariableReference_Command: {
            const Variable* var = this->symbolRef<Variable>(Symbol::Kind::kVariable);
            VariableReference::RefKind refKind = (VariableReference::RefKind) this->readU8();
            return VariableReference::Make(/*offset=*/-1, var, refKind);
        }
        case Rehydrator::kVoid_Command:
            return nullptr;
        default:
            printf("unsupported expression %d\n", kind);
            SkASSERT(false);
            return nullptr;
    }
}

std::shared_ptr<SymbolTable> Rehydrator::symbolTable(bool inherit) {
    int command = this->readU8();
    if (command == kVoid_Command) {
        return nullptr;
    }
    SkASSERT(command == kSymbolTable_Command);
    uint16_t ownedCount = this->readU16();
    std::shared_ptr<SymbolTable> oldTable = fSymbolTable;
    std::shared_ptr<SymbolTable> result =
            inherit ? std::make_shared<SymbolTable>(fSymbolTable, /*builtin=*/true)
                    : std::make_shared<SymbolTable>(this->errorReporter(), /*builtin=*/true);
    fSymbolTable = result;
    std::vector<const Symbol*> ownedSymbols;
    ownedSymbols.reserve(ownedCount);
    for (int i = 0; i < ownedCount; ++i) {
        ownedSymbols.push_back(this->symbol());
    }
    uint16_t symbolCount = this->readU16();
    std::vector<std::pair<skstd::string_view, int>> symbols;
    symbols.reserve(symbolCount);
    for (int i = 0; i < symbolCount; ++i) {
        int index = this->readU16();
        fSymbolTable->addWithoutOwnership(ownedSymbols[index]);
    }
    fSymbolTable = oldTable;
    return result;
}

}  // namespace SkSL
