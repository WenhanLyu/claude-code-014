#pragma once
#ifndef PYTHON_INTERPRETER_EVALVISITOR_H
#define PYTHON_INTERPRETER_EVALVISITOR_H

#include "Python3ParserBaseVisitor.h"
#include "PyValue.h"
#include <map>
#include <vector>
#include <functional>
#include <any>
#include <iostream>
#include <iomanip>
#include <sstream>

// Signal types for control flow
enum class SignalType {
    NONE,
    BREAK,
    CONTINUE,
    RETURN
};

struct Signal {
    SignalType type;
    PyValue value; // for return
    Signal() : type(SignalType::NONE) {}
    static Signal makeBreak() { Signal s; s.type = SignalType::BREAK; return s; }
    static Signal makeContinue() { Signal s; s.type = SignalType::CONTINUE; return s; }
    static Signal makeReturn(const PyValue &v) { Signal s; s.type = SignalType::RETURN; s.value = v; return s; }
};

// Function definition
struct FuncDef {
    std::vector<std::string> paramNames;
    std::vector<PyValue> defaultValues; // aligned with paramNames from the end
    int numDefaults;
    Python3Parser::SuiteContext *body;
};

class EvalVisitor : public Python3ParserBaseVisitor {
private:
    // Scope: vector of maps, last is current scope
    // scope[0] is global scope
    std::vector<std::map<std::string, PyValue>> scopes;
    std::map<std::string, FuncDef> functions;
    Signal currentSignal;

    PyValue &getVar(const std::string &name) {
        // Check current scope first (if not global)
        if (scopes.size() > 1) {
            auto &local = scopes.back();
            auto it = local.find(name);
            if (it != local.end()) return it->second;
        }
        // Then global
        auto &global = scopes[0];
        return global[name];
    }

    void setVar(const std::string &name, const PyValue &val) {
        if (scopes.size() > 1) {
            // In a function: check if it's a local variable (exists in local scope)
            auto &local = scopes.back();
            auto it = local.find(name);
            if (it != local.end()) {
                it->second = val;
                return;
            }
        }
        // Set in global scope
        scopes[0][name] = val;
    }

    bool hasVar(const std::string &name) const {
        if (scopes.size() > 1) {
            auto &local = scopes.back();
            if (local.find(name) != local.end()) return true;
        }
        return scopes[0].find(name) != scopes[0].end();
    }

    PyValue toPyValue(std::any val) {
        if (!val.has_value()) return PyValue::makeNone();
        try { return std::any_cast<PyValue>(val); } catch (...) {}
        try { return std::any_cast<Signal>(val).value; } catch (...) {}
        return PyValue::makeNone();
    }

    // Parse a Python string literal (remove quotes, handle escape sequences)
    std::string parseStringLiteral(const std::string &raw) {
        std::string s = raw;
        // Remove prefix (r, u, f, etc.)
        size_t start = 0;
        while (start < s.size() && (s[start] == 'r' || s[start] == 'R' ||
               s[start] == 'u' || s[start] == 'U' || s[start] == 'b' || s[start] == 'B' ||
               s[start] == 'f' || s[start] == 'F')) {
            start++;
        }
        s = s.substr(start);

        bool isTriple = false;
        char quoteChar;
        if (s.size() >= 6 && (s.substr(0, 3) == "'''" || s.substr(0, 3) == "\"\"\"")) {
            isTriple = true;
            quoteChar = s[0];
            s = s.substr(3, s.size() - 6);
        } else {
            quoteChar = s[0];
            s = s.substr(1, s.size() - 2);
        }

        // Check if raw string
        bool isRaw = false;
        for (size_t i = 0; i < start; ++i) {
            if (raw[i] == 'r' || raw[i] == 'R') { isRaw = true; break; }
        }

        if (isRaw) return s;

        // Process escape sequences
        std::string result;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char next = s[i + 1];
                switch (next) {
                    case 'n': result += '\n'; i++; break;
                    case 't': result += '\t'; i++; break;
                    case 'r': result += '\r'; i++; break;
                    case '\\': result += '\\'; i++; break;
                    case '\'': result += '\''; i++; break;
                    case '"': result += '"'; i++; break;
                    case '0': result += '\0'; i++; break;
                    case 'a': result += '\a'; i++; break;
                    case 'b': result += '\b'; i++; break;
                    case 'f': result += '\f'; i++; break;
                    case 'v': result += '\v'; i++; break;
                    default: result += '\\'; result += next; i++; break;
                }
            } else {
                result += s[i];
            }
        }
        return result;
    }

    // Built-in print function
    PyValue builtinPrint(const std::vector<PyValue> &args) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << ' ';
            std::cout << args[i].toPrintStr();
        }
        std::cout << '\n';
        return PyValue::makeNone();
    }

    // Built-in type conversion functions
    PyValue builtinInt(const std::vector<PyValue> &args) {
        if (args.empty()) return PyValue::makeInt(0);
        return PyValue::makeInt(args[0].toInt());
    }

    PyValue builtinFloat(const std::vector<PyValue> &args) {
        if (args.empty()) return PyValue::makeFloat(0.0);
        return PyValue::makeFloat(args[0].toFloat());
    }

    PyValue builtinStr(const std::vector<PyValue> &args) {
        if (args.empty()) return PyValue::makeStr("");
        return PyValue::makeStr(args[0].toStr());
    }

    PyValue builtinBool(const std::vector<PyValue> &args) {
        if (args.empty()) return PyValue::makeBool(false);
        return PyValue::makeBool(args[0].toBool());
    }

    PyValue callFunction(const std::string &name, const std::vector<PyValue> &posArgs,
                          const std::map<std::string, PyValue> &kwArgs) {
        // Built-ins
        if (name == "print") return builtinPrint(posArgs);
        if (name == "int") return builtinInt(posArgs);
        if (name == "float") return builtinFloat(posArgs);
        if (name == "str") return builtinStr(posArgs);
        if (name == "bool") return builtinBool(posArgs);

        auto it = functions.find(name);
        if (it == functions.end()) {
            throw std::runtime_error("undefined function: " + name);
        }

        const FuncDef &func = it->second;

        // Create new scope with parameters
        std::map<std::string, PyValue> localScope;

        // Assign positional args
        size_t posIdx = 0;
        for (size_t i = 0; i < func.paramNames.size(); ++i) {
            const std::string &pname = func.paramNames[i];
            auto kwIt = kwArgs.find(pname);
            if (kwIt != kwArgs.end()) {
                localScope[pname] = kwIt->second;
            } else if (posIdx < posArgs.size()) {
                localScope[pname] = posArgs[posIdx++];
            } else {
                // Use default value
                int defaultIdx = (int)i - ((int)func.paramNames.size() - func.numDefaults);
                if (defaultIdx >= 0 && defaultIdx < (int)func.defaultValues.size()) {
                    localScope[pname] = func.defaultValues[defaultIdx];
                }
            }
        }

        scopes.push_back(localScope);
        Signal savedSignal = currentSignal;
        currentSignal = Signal();

        visit(func.body);

        PyValue retVal = PyValue::makeNone();
        if (currentSignal.type == SignalType::RETURN) {
            retVal = currentSignal.value;
        }

        currentSignal = savedSignal;
        scopes.pop_back();
        return retVal;
    }

public:
    EvalVisitor() {
        scopes.push_back({}); // global scope
    }

    std::any visitFile_input(Python3Parser::File_inputContext *ctx) override {
        for (auto stmt : ctx->stmt()) {
            visit(stmt);
            if (currentSignal.type != SignalType::NONE) break;
        }
        return {};
    }

    std::any visitStmt(Python3Parser::StmtContext *ctx) override {
        if (ctx->simple_stmt()) return visit(ctx->simple_stmt());
        return visit(ctx->compound_stmt());
    }

    std::any visitSimple_stmt(Python3Parser::Simple_stmtContext *ctx) override {
        return visit(ctx->small_stmt());
    }

    std::any visitSmall_stmt(Python3Parser::Small_stmtContext *ctx) override {
        if (ctx->expr_stmt()) return visit(ctx->expr_stmt());
        return visit(ctx->flow_stmt());
    }

    std::any visitCompound_stmt(Python3Parser::Compound_stmtContext *ctx) override {
        if (ctx->if_stmt()) return visit(ctx->if_stmt());
        if (ctx->while_stmt()) return visit(ctx->while_stmt());
        return visit(ctx->funcdef());
    }

    std::any visitExpr_stmt(Python3Parser::Expr_stmtContext *ctx) override {
        auto testlists = ctx->testlist();
        auto augOp = ctx->augassign();
        auto assigns = ctx->ASSIGN();

        if (augOp) {
            // Augmented assignment: target augop value
            PyValue target = toPyValue(visit(testlists[0]));
            PyValue value = toPyValue(visit(testlists[1]));
            std::string op = augOp->getText();

            PyValue result;
            if (op == "+=") result = target + value;
            else if (op == "-=") result = target - value;
            else if (op == "*=") result = target * value;
            else if (op == "/=") result = target.trueDiv(value);
            else if (op == "//=") result = target.floorDiv(value);
            else if (op == "%=") result = target.mod(value);

            assignToTarget(testlists[0], result);
            return {};
        }

        if (!assigns.empty()) {
            // Chained assignment: a = b = val
            // Evaluate RHS (last testlist)
            PyValue value = toPyValue(visit(testlists.back()));

            // Assign to all targets (all but last testlist)
            for (int i = 0; i < (int)testlists.size() - 1; ++i) {
                assignToTarget(testlists[i], value);
            }
            return {};
        }

        // Just an expression (e.g., function call)
        return visit(testlists[0]);
    }

    void assignToTarget(Python3Parser::TestlistContext *ctx, const PyValue &value) {
        auto tests = ctx->test();
        if (tests.size() == 1) {
            // Single assignment
            std::string name = tests[0]->getText();
            setVar(name, value);
        } else {
            // Tuple unpacking: a, b = tuple_value
            if (value.type == PyType::TUPLE) {
                for (size_t i = 0; i < tests.size(); ++i) {
                    std::string name = tests[i]->getText();
                    setVar(name, value.tupleVal[i]);
                }
            } else {
                // Multiple assignment from non-tuple shouldn't happen in valid Python,
                // but handle single value
                for (size_t i = 0; i < tests.size(); ++i) {
                    std::string name = tests[i]->getText();
                    setVar(name, value);
                }
            }
        }
    }

    std::any visitFlow_stmt(Python3Parser::Flow_stmtContext *ctx) override {
        if (ctx->break_stmt()) return visit(ctx->break_stmt());
        if (ctx->continue_stmt()) return visit(ctx->continue_stmt());
        return visit(ctx->return_stmt());
    }

    std::any visitBreak_stmt(Python3Parser::Break_stmtContext *ctx) override {
        currentSignal = Signal::makeBreak();
        return {};
    }

    std::any visitContinue_stmt(Python3Parser::Continue_stmtContext *ctx) override {
        currentSignal = Signal::makeContinue();
        return {};
    }

    std::any visitReturn_stmt(Python3Parser::Return_stmtContext *ctx) override {
        PyValue retVal = PyValue::makeNone();
        if (ctx->testlist()) {
            retVal = toPyValue(visit(ctx->testlist()));
        }
        currentSignal = Signal::makeReturn(retVal);
        return {};
    }

    std::any visitIf_stmt(Python3Parser::If_stmtContext *ctx) override {
        auto tests = ctx->test();
        auto suites = ctx->suite();

        // if test: suite
        PyValue cond = toPyValue(visit(tests[0]));
        if (cond.toBool()) {
            visit(suites[0]);
            return {};
        }

        // elif test: suite (ELIF tokens)
        auto elifs = ctx->ELIF();
        for (size_t i = 0; i < elifs.size(); ++i) {
            PyValue elifCond = toPyValue(visit(tests[i + 1]));
            if (elifCond.toBool()) {
                visit(suites[i + 1]);
                return {};
            }
        }

        // else: suite
        if (ctx->ELSE()) {
            visit(suites.back());
        }
        return {};
    }

    std::any visitWhile_stmt(Python3Parser::While_stmtContext *ctx) override {
        while (true) {
            PyValue cond = toPyValue(visit(ctx->test()));
            if (!cond.toBool()) break;

            visit(ctx->suite());

            if (currentSignal.type == SignalType::BREAK) {
                currentSignal = Signal();
                break;
            }
            if (currentSignal.type == SignalType::CONTINUE) {
                currentSignal = Signal();
                continue;
            }
            if (currentSignal.type == SignalType::RETURN) {
                break; // propagate return
            }
        }
        return {};
    }

    std::any visitSuite(Python3Parser::SuiteContext *ctx) override {
        if (ctx->simple_stmt()) {
            return visit(ctx->simple_stmt());
        }
        for (auto stmt : ctx->stmt()) {
            visit(stmt);
            if (currentSignal.type != SignalType::NONE) break;
        }
        return {};
    }

    std::any visitFuncdef(Python3Parser::FuncdefContext *ctx) override {
        std::string name = ctx->NAME()->getText();
        FuncDef func;
        func.body = ctx->suite();

        auto params = ctx->parameters();
        if (params->typedargslist()) {
            auto targs = params->typedargslist();
            auto tfpdefs = targs->tfpdef();
            auto tests = targs->test();

            // Count defaults: defaults are aligned to the end of params
            // Grammar: tfpdef ('=' test)? (',' tfpdef ('=' test)?)*
            // We need to figure out which params have defaults
            // Parse by walking children
            func.paramNames.clear();
            func.defaultValues.clear();

            // Walk through the children to match tfpdef with their defaults
            size_t testIdx = 0;
            auto children = targs->children;
            for (size_t i = 0; i < children.size(); ++i) {
                auto *tf = dynamic_cast<Python3Parser::TfpdefContext*>(children[i]);
                if (tf) {
                    func.paramNames.push_back(tf->NAME()->getText());
                    // Check if next non-comma child is '='
                    if (i + 1 < children.size()) {
                        auto *nextTok = dynamic_cast<antlr4::tree::TerminalNode*>(children[i+1]);
                        if (nextTok && nextTok->getSymbol()->getType() == Python3Parser::ASSIGN) {
                            // Has default value
                            func.defaultValues.push_back(toPyValue(visit(tests[testIdx++])));
                        }
                    }
                }
            }
            func.numDefaults = (int)func.defaultValues.size();
        } else {
            func.numDefaults = 0;
        }

        functions[name] = func;
        return {};
    }

    std::any visitTest(Python3Parser::TestContext *ctx) override {
        return visit(ctx->or_test());
    }

    std::any visitOr_test(Python3Parser::Or_testContext *ctx) override {
        auto andTests = ctx->and_test();
        if (andTests.size() == 1) return visit(andTests[0]);

        // Short-circuit: return actual value
        for (size_t i = 0; i < andTests.size(); ++i) {
            PyValue val = toPyValue(visit(andTests[i]));
            if (val.toBool() || i == andTests.size() - 1) return val;
        }
        return toPyValue(visit(andTests.back()));
    }

    std::any visitAnd_test(Python3Parser::And_testContext *ctx) override {
        auto notTests = ctx->not_test();
        if (notTests.size() == 1) return visit(notTests[0]);

        // Short-circuit: return actual value
        for (size_t i = 0; i < notTests.size(); ++i) {
            PyValue val = toPyValue(visit(notTests[i]));
            if (!val.toBool() || i == notTests.size() - 1) return val;
        }
        return toPyValue(visit(notTests.back()));
    }

    std::any visitNot_test(Python3Parser::Not_testContext *ctx) override {
        if (ctx->NOT()) {
            PyValue val = toPyValue(visit(ctx->not_test()));
            return PyValue::makeBool(!val.toBool());
        }
        return visit(ctx->comparison());
    }

    std::any visitComparison(Python3Parser::ComparisonContext *ctx) override {
        auto arithExprs = ctx->arith_expr();
        if (arithExprs.size() == 1) return visit(arithExprs[0]);

        auto compOps = ctx->comp_op();
        // Chained comparison: a < b < c => a < b and b < c, with b evaluated once
        PyValue left = toPyValue(visit(arithExprs[0]));
        for (size_t i = 0; i < compOps.size(); ++i) {
            PyValue right = toPyValue(visit(arithExprs[i + 1]));
            std::string op = compOps[i]->getText();
            bool result;
            if (op == "==") result = left.eq(right);
            else if (op == "!=") result = left.ne(right);
            else if (op == "<") result = left.lt(right);
            else if (op == ">") result = left.gt(right);
            else if (op == "<=") result = left.le(right);
            else if (op == ">=") result = left.ge(right);
            else result = false;

            if (!result) return PyValue::makeBool(false);
            left = right;
        }
        return PyValue::makeBool(true);
    }

    std::any visitArith_expr(Python3Parser::Arith_exprContext *ctx) override {
        auto terms = ctx->term();
        if (terms.size() == 1) return visit(terms[0]);

        auto ops = ctx->addorsub_op();
        PyValue result = toPyValue(visit(terms[0]));
        for (size_t i = 0; i < ops.size(); ++i) {
            PyValue right = toPyValue(visit(terms[i + 1]));
            if (ops[i]->ADD()) {
                result = result + right;
            } else {
                result = result - right;
            }
        }
        return result;
    }

    std::any visitTerm(Python3Parser::TermContext *ctx) override {
        auto factors = ctx->factor();
        if (factors.size() == 1) return visit(factors[0]);

        auto ops = ctx->muldivmod_op();
        PyValue result = toPyValue(visit(factors[0]));
        for (size_t i = 0; i < ops.size(); ++i) {
            PyValue right = toPyValue(visit(factors[i + 1]));
            if (ops[i]->STAR()) {
                result = result * right;
            } else if (ops[i]->DIV()) {
                result = result.trueDiv(right);
            } else if (ops[i]->IDIV()) {
                result = result.floorDiv(right);
            } else if (ops[i]->MOD()) {
                result = result.mod(right);
            }
        }
        return result;
    }

    std::any visitFactor(Python3Parser::FactorContext *ctx) override {
        if (ctx->factor()) {
            PyValue val = toPyValue(visit(ctx->factor()));
            if (ctx->ADD()) return val.pos();
            if (ctx->MINUS()) return val.negate();
        }
        return visit(ctx->atom_expr());
    }

    std::any visitAtom_expr(Python3Parser::Atom_exprContext *ctx) override {
        if (ctx->trailer()) {
            // Function call: atom trailer
            std::string funcName = ctx->atom()->getText();

            // Parse arguments
            auto trailer = ctx->trailer();
            std::vector<PyValue> posArgs;
            std::map<std::string, PyValue> kwArgs;

            if (trailer->arglist()) {
                auto arglist = trailer->arglist();
                for (auto arg : arglist->argument()) {
                    if (arg->ASSIGN()) {
                        // keyword arg: name=value
                        std::string name = arg->test(0)->getText();
                        PyValue value = toPyValue(visit(arg->test(1)));
                        kwArgs[name] = value;
                    } else {
                        // positional arg
                        posArgs.push_back(toPyValue(visit(arg->test(0))));
                    }
                }
            }

            return callFunction(funcName, posArgs, kwArgs);
        }
        return visit(ctx->atom());
    }

    std::any visitAtom(Python3Parser::AtomContext *ctx) override {
        if (ctx->NAME()) {
            std::string name = ctx->NAME()->getText();
            return getVar(name);
        }
        if (ctx->NUMBER()) {
            std::string num = ctx->NUMBER()->getText();
            // Check if float
            if (num.find('.') != std::string::npos || num.find('e') != std::string::npos ||
                num.find('E') != std::string::npos) {
                return PyValue::makeFloat(std::stod(num));
            }
            return PyValue::makeInt(BigInteger(num));
        }
        if (!ctx->STRING().empty()) {
            // Adjacent strings concatenate
            std::string result;
            for (auto s : ctx->STRING()) {
                result += parseStringLiteral(s->getText());
            }
            return PyValue::makeStr(result);
        }
        if (ctx->NONE()) return PyValue::makeNone();
        if (ctx->TRUE()) return PyValue::makeBool(true);
        if (ctx->FALSE()) return PyValue::makeBool(false);
        if (ctx->test()) {
            // Parenthesized expression
            return visit(ctx->test());
        }
        if (ctx->format_string()) {
            return visit(ctx->format_string());
        }
        return PyValue::makeNone();
    }

    std::any visitTestlist(Python3Parser::TestlistContext *ctx) override {
        auto tests = ctx->test();
        if (tests.size() == 1 && ctx->COMMA().empty()) {
            // Single value, no trailing comma
            return visit(tests[0]);
        }
        // Multiple values or trailing comma -> tuple
        std::vector<PyValue> vals;
        for (auto t : tests) {
            vals.push_back(toPyValue(visit(t)));
        }
        return PyValue::makeTuple(vals);
    }

    std::any visitFormat_string(Python3Parser::Format_stringContext *ctx) override {
        std::string result;
        auto children = ctx->children;
        // Skip FORMAT_QUOTATION and QUOTATION
        for (size_t i = 1; i < children.size() - 1; ++i) {
            auto *term = dynamic_cast<antlr4::tree::TerminalNode*>(children[i]);
            if (term) {
                if (term->getSymbol()->getType() == Python3Parser::FORMAT_STRING_LITERAL) {
                    std::string lit = term->getText();
                    // Handle {{ and }} escapes
                    std::string processed;
                    for (size_t j = 0; j < lit.size(); ++j) {
                        if (j + 1 < lit.size() && lit[j] == '{' && lit[j+1] == '{') {
                            processed += '{';
                            j++;
                        } else if (j + 1 < lit.size() && lit[j] == '}' && lit[j+1] == '}') {
                            processed += '}';
                            j++;
                        } else if (lit[j] == '\\' && j + 1 < lit.size()) {
                            char next = lit[j + 1];
                            switch (next) {
                                case 'n': processed += '\n'; j++; break;
                                case 't': processed += '\t'; j++; break;
                                case 'r': processed += '\r'; j++; break;
                                case '\\': processed += '\\'; j++; break;
                                case '\'': processed += '\''; j++; break;
                                case '"': processed += '"'; j++; break;
                                default: processed += '\\'; processed += next; j++; break;
                            }
                        } else {
                            processed += lit[j];
                        }
                    }
                    result += processed;
                }
                // OPEN_BRACE and CLOSE_BRACE are handled by skipping
            } else {
                // Should be a testlist context (expression inside {})
                auto *testlist = dynamic_cast<Python3Parser::TestlistContext*>(children[i]);
                if (testlist) {
                    PyValue val = toPyValue(visit(testlist));
                    result += val.toStr();
                }
            }
        }
        return PyValue::makeStr(result);
    }
};

#endif // PYTHON_INTERPRETER_EVALVISITOR_H
