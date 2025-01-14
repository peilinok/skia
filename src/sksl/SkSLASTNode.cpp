/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkSLString.h"
#include "src/sksl/SkSLASTNode.h"
#include "src/sksl/SkSLOperators.h"

namespace SkSL {

#ifdef SK_DEBUG
String ASTNode::description() const {
    switch (fKind) {
        case Kind::kNull: return "";
        case Kind::kBinary:
            return "(" + this->begin()->description() + " " +
                               getOperator().operatorName() + " " +
                               (this->begin() + 1)->description() + ")";
        case Kind::kBlock: {
            String result = "{\n";
            for (const auto& c : *this) {
                result += c.description();
                result += "\n";
            }
            result += "}";
            return result;
        }
        case Kind::kBool:
            return getBool() ? "true" : "false";
        case Kind::kBreak:
            return "break";
        case Kind::kCall: {
            auto iter = this->begin();
            String result = (iter++)->description();
            result += "(";
            const char* separator = "";
            while (iter != this->end()) {
                result += separator;
                result += (iter++)->description();
                separator = ",";
            }
            result += ")";
            return result;
        }
        case Kind::kContinue:
            return "continue";
        case Kind::kDiscard:
            return "discard";
        case Kind::kDo:
            return "do " + this->begin()->description() + " while (" +
                   (this->begin() + 1)->description() + ")";
        case Kind::kEnum: {
            String result = "enum ";
            result += getStringView();
            result += " {\n";
            for (const auto& c : *this) {
                result += c.description();
                result += "\n";
            }
            result += "};";
            return result;
        }
        case Kind::kEnumCase:
            if (this->begin() != this->end()) {
                return String(getStringView()) + " = " + this->begin()->description();
            }
            return String(getStringView());
        case Kind::kExtension:
            return "#extension " + getStringView();
        case Kind::kField:
            return this->begin()->description() + "." + getStringView();
        case Kind::kFile: {
            String result;
            for (const auto& c : *this) {
                result += c.description();
                result += "\n";
            }
            return result;
        }
        case Kind::kFloat:
            return to_string(getFloat());
        case Kind::kFor:
            return "for (" + this->begin()->description() + "; " +
                   (this->begin() + 1)->description() + "; " + (this->begin() + 2)->description() +
                   ") " + (this->begin() + 3)->description();
        case Kind::kFunction: {
            const FunctionData& fd = getFunctionData();
            String result = fd.fModifiers.description();
            if (result.size()) {
                result += " ";
            }
            auto iter = this->begin();
            result += (iter++)->description() + " " + fd.fName + "(";
            const char* separator = "";
            for (size_t i = 0; i < fd.fParameterCount; ++i) {
                result += separator;
                result += (iter++)->description();
                separator = ", ";
            }
            result += ")";
            if (iter != this->end()) {
                result += " " + (iter++)->description();
                SkASSERT(iter == this->end());
            }
            else {
                result += ";";
            }
            return result;
        }
        case Kind::kIdentifier:
            return String(getStringView());
        case Kind::kIndex:
            return this->begin()->description() + "[" + (this->begin() + 1)->description() + "]";
        case Kind::kIf: {
            String result;
            if (getBool()) {
                result = "@";
            }
            auto iter = this->begin();
            result += "if (" + (iter++)->description() + ") ";
            result += (iter++)->description();
            if (iter != this->end()) {
                result += " else " + (iter++)->description();
                SkASSERT(iter == this->end());
            }
            return result;
        }
        case Kind::kInt:
            return to_string(getInt());
        case Kind::kInterfaceBlock: {
            const InterfaceBlockData& id = getInterfaceBlockData();
            String result = id.fModifiers.description() + " " + id.fTypeName + " {\n";
            auto iter = this->begin();
            for (size_t i = 0; i < id.fDeclarationCount; ++i) {
                result += (iter++)->description() + "\n";
            }
            result += "} ";
            result += id.fInstanceName;
            if (id.fIsArray) {
                result += "[" + (iter++)->description() + "]";
            }
            SkASSERT(iter == this->end());
            result += ";";
            return result;
        }
        case Kind::kModifiers:
            return getModifiers().description();
        case Kind::kParameter: {
            const ParameterData& pd = getParameterData();
            auto iter = this->begin();
            String result = (iter++)->description() + " " + pd.fName;
            if (pd.fIsArray) {
                result += "[" + (iter++)->description() + "]";
            }
            if (iter != this->end()) {
                result += " = " + (iter++)->description();
                SkASSERT(iter == this->end());
            }
            return result;
        }
        case Kind::kPostfix:
            return this->begin()->description() + getOperator().operatorName();
        case Kind::kPrefix:
            return getOperator().operatorName() + this->begin()->description();
        case Kind::kReturn:
            if (this->begin() != this->end()) {
                return "return " + this->begin()->description() + ";";
            }
            return "return;";
        case Kind::kScope:
            return this->begin()->description() + "::" + getStringView();
        case Kind::kSection:
            return "@section { ... }";
        case Kind::kSwitchCase: {
            auto iter = this->begin();
            String result;
            if (*iter) {
                result.appendf("case %s:\n", iter->description().c_str());
            } else {
                result = "default:\n";
            }
            for (++iter; iter != this->end(); ++iter) {
                result += "\n" + iter->description();
            }
            return result;
        }
        case Kind::kSwitch: {
            auto iter = this->begin();
            String result;
            if (getBool()) {
                result = "@";
            }
            result += "switch (" + (iter++)->description() + ") {";
            for (; iter != this->end(); ++iter) {
                result += iter->description() + "\n";
            }
            result += "}";
            return result;
        }
        case Kind::kTernary:
            return "(" + this->begin()->description() + " ? " + (this->begin() + 1)->description() +
                   " : " + (this->begin() + 2)->description() + ")";
        case Kind::kType:
            return String(getStringView());
        case Kind::kVarDeclaration: {
            const VarData& vd = getVarData();
            String result(vd.fName);
            auto iter = this->begin();
            if (vd.fIsArray) {
                result += "[" + (iter++)->description() + "]";
            }
            if (iter != this->end()) {
                result += " = " + (iter++)->description();
                SkASSERT(iter == this->end());
            }
            return result;
        }
        case Kind::kVarDeclarations: {
            auto iter = this->begin();
            String result = (iter++)->description();
            if (result.size()) {
                result += " ";
            }
            result += (iter++)->description();
            const char* separator = " ";
            for (; iter != this->end(); ++iter) {
                result += separator + iter->description();
                separator = ", ";
            }
            return result;
        }
        case Kind::kWhile: {
            return "while (" + this->begin()->description() + ") " +
                   (this->begin() + 1)->description();

        }
        default:
            SkDEBUGFAIL("unrecognized AST node kind");
            return "<error>";
    }
}
#endif

}  // namespace SkSL
