/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/ir/SkSLType.h"

#include "src/sksl/SkSLConstantFolder.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLConstructorCompoundCast.h"
#include "src/sksl/ir/SkSLConstructorScalarCast.h"
#include "src/sksl/ir/SkSLFunctionReference.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLTypeReference.h"

namespace SkSL {

CoercionCost Type::coercionCost(const Type& other) const {
    if (*this == other) {
        return CoercionCost::Free();
    }
    if (this->isVector() && other.isVector()) {
        if (this->columns() == other.columns()) {
            return this->componentType().coercionCost(other.componentType());
        }
        return CoercionCost::Impossible();
    }
    if (this->isMatrix()) {
        if (this->columns() == other.columns() && this->rows() == other.rows()) {
            return this->componentType().coercionCost(other.componentType());
        }
        return CoercionCost::Impossible();
    }
    if (this->isNumber() && other.isNumber()) {
        if (this->isLiteral() && this->isInteger()) {
            return CoercionCost::Free();
        } else if (this->numberKind() != other.numberKind()) {
            return CoercionCost::Impossible();
        } else if (other.priority() >= this->priority()) {
            return CoercionCost::Normal(other.priority() - this->priority());
        } else {
            return CoercionCost::Narrowing(this->priority() - other.priority());
        }
    }
    if (fCoercibleTypes) {
        for (size_t i = 0; i < fCoercibleTypes->size(); i++) {
            if (*(*fCoercibleTypes)[i] == other) {
                return CoercionCost::Normal((int) i + 1);
            }
        }
    }
    return CoercionCost::Impossible();
}

const Type& Type::toCompound(const Context& context, int columns, int rows) const {
    SkASSERT(this->isScalar());
    if (columns == 1 && rows == 1) {
        return *this;
    }
    if (*this == *context.fTypes.fFloat || *this == *context.fTypes.fFloatLiteral) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fFloat;
                    case 2: return *context.fTypes.fFloat2;
                    case 3: return *context.fTypes.fFloat3;
                    case 4: return *context.fTypes.fFloat4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            case 2:
                switch (columns) {
                    case 2: return *context.fTypes.fFloat2x2;
                    case 3: return *context.fTypes.fFloat3x2;
                    case 4: return *context.fTypes.fFloat4x2;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            case 3:
                switch (columns) {
                    case 2: return *context.fTypes.fFloat2x3;
                    case 3: return *context.fTypes.fFloat3x3;
                    case 4: return *context.fTypes.fFloat4x3;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            case 4:
                switch (columns) {
                    case 2: return *context.fTypes.fFloat2x4;
                    case 3: return *context.fTypes.fFloat3x4;
                    case 4: return *context.fTypes.fFloat4x4;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fHalf) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fHalf;
                    case 2: return *context.fTypes.fHalf2;
                    case 3: return *context.fTypes.fHalf3;
                    case 4: return *context.fTypes.fHalf4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            case 2:
                switch (columns) {
                    case 2: return *context.fTypes.fHalf2x2;
                    case 3: return *context.fTypes.fHalf3x2;
                    case 4: return *context.fTypes.fHalf4x2;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            case 3:
                switch (columns) {
                    case 2: return *context.fTypes.fHalf2x3;
                    case 3: return *context.fTypes.fHalf3x3;
                    case 4: return *context.fTypes.fHalf4x3;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            case 4:
                switch (columns) {
                    case 2: return *context.fTypes.fHalf2x4;
                    case 3: return *context.fTypes.fHalf3x4;
                    case 4: return *context.fTypes.fHalf4x4;
                    default: SK_ABORT("unsupported matrix column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fInt || *this == *context.fTypes.fIntLiteral) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fInt;
                    case 2: return *context.fTypes.fInt2;
                    case 3: return *context.fTypes.fInt3;
                    case 4: return *context.fTypes.fInt4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fShort) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fShort;
                    case 2: return *context.fTypes.fShort2;
                    case 3: return *context.fTypes.fShort3;
                    case 4: return *context.fTypes.fShort4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fUInt) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fUInt;
                    case 2: return *context.fTypes.fUInt2;
                    case 3: return *context.fTypes.fUInt3;
                    case 4: return *context.fTypes.fUInt4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fUShort) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fUShort;
                    case 2: return *context.fTypes.fUShort2;
                    case 3: return *context.fTypes.fUShort3;
                    case 4: return *context.fTypes.fUShort4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    } else if (*this == *context.fTypes.fBool) {
        switch (rows) {
            case 1:
                switch (columns) {
                    case 1: return *context.fTypes.fBool;
                    case 2: return *context.fTypes.fBool2;
                    case 3: return *context.fTypes.fBool3;
                    case 4: return *context.fTypes.fBool4;
                    default: SK_ABORT("unsupported vector column count (%d)", columns);
                }
            default: SK_ABORT("unsupported row count (%d)", rows);
        }
    }
    SkDEBUGFAILF("unsupported toCompound type %s", this->description().c_str());
    return *context.fTypes.fVoid;
}

const Type* Type::clone(SymbolTable* symbolTable) const {
    // Many types are built-ins, and exist in every SymbolTable by default.
    if (this->isInBuiltinTypes()) {
        return this;
    }
    // Even if the type isn't a built-in, it might already exist in the SymbolTable.
    const Symbol* clonedSymbol = (*symbolTable)[this->name()];
    if (clonedSymbol != nullptr) {
        const Type& clonedType = clonedSymbol->as<Type>();
        SkASSERT(clonedType.typeKind() == this->typeKind());
        return &clonedType;
    }
    // This type actually needs to be cloned into the destination SymbolTable.
    switch (this->typeKind()) {
        case TypeKind::kArray:
            return symbolTable->add(Type::MakeArrayType(String(this->name()), this->componentType(),
                                                               this->columns()));

        case TypeKind::kStruct:
            return symbolTable->add(Type::MakeStructType(this->fOffset, String(this->name()),
                                                         this->fields()));

        case TypeKind::kEnum:
            return symbolTable->add(Type::MakeEnumType(String(this->name())));

        default:
            SkDEBUGFAILF("don't know how to clone type '%s'", this->description().c_str());
            return nullptr;
    }
}

std::unique_ptr<Expression> Type::coerceExpression(std::unique_ptr<Expression> expr,
                                                   const Context& context) const {
    if (!expr) {
        return nullptr;
    }
    const int offset = expr->fOffset;
    if (expr->is<FunctionReference>()) {
        context.fErrors.error(offset, "expected '(' to begin function call");
        return nullptr;
    }
    if (expr->is<TypeReference>()) {
        context.fErrors.error(offset, "expected '(' to begin constructor invocation");
        return nullptr;
    }
    if (expr->type() == *this) {
        return expr;
    }

    const Program::Settings& settings = context.fConfig->fSettings;
    if (!expr->coercionCost(*this).isPossible(settings.fAllowNarrowingConversions)) {
        context.fErrors.error(offset, "expected '" + this->displayName() + "', but found '" +
                                      expr->type().displayName() + "'");
        return nullptr;
    }

    if (this->isScalar()) {
        return ConstructorScalarCast::Make(context, offset, *this, std::move(expr));
    }
    if (this->isVector() || this->isMatrix()) {
        return ConstructorCompoundCast::Make(context, offset, *this, std::move(expr));
    }
    context.fErrors.error(offset, "cannot construct '" + this->displayName() + "'");
    return nullptr;
}

bool Type::isOrContainsArray() const {
    if (this->isStruct()) {
        for (const Field& f : this->fields()) {
            if (f.fType->isOrContainsArray()) {
                return true;
            }
        }
        return false;
    }

    return this->isArray();
}

bool Type::checkForOutOfRangeLiteral(const Context& context, const Expression& expr) const {
    bool foundError = false;
    const Type& baseType = this->componentType();
    if (baseType.isInteger()) {
        // Replace constant expressions with their corresponding values.
        const Expression* valueExpr = ConstantFolder::GetConstantValueForVariable(expr);

        // Iterate over every constant subexpression in the value.
        int numSlots = valueExpr->type().slotCount();
        for (int slot = 0; slot < numSlots; ++slot) {
            const Expression* subexpr = valueExpr->getConstantSubexpression(slot);
            if (!subexpr || !subexpr->is<IntLiteral>()) {
                continue;
            }
            // Look for an IntLiteral value that is out of range for the corresponding type.
            SKSL_INT value = subexpr->as<IntLiteral>().value();
            if (value < baseType.minimumValue() || value > baseType.maximumValue()) {
                // We found a value that can't fit in the type. Flag it as an error.
                context.fErrors.error(expr.fOffset,
                                      String("integer is out of range for type '") +
                                      this->displayName().c_str() + "': " + to_string(value));
                foundError = true;
            }
        }
    }

    // We don't need range checks for floats or booleans; any matched-type value is acceptable.
    return foundError;
}

}  // namespace SkSL
