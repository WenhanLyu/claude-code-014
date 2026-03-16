#pragma once
#ifndef PYTHON_INTERPRETER_PYVALUE_H
#define PYTHON_INTERPRETER_PYVALUE_H

#include "BigInteger.h"
#include <string>
#include <vector>
#include <variant>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>

enum class PyType {
    NONE,
    BOOL,
    INT,
    FLOAT,
    STR,
    TUPLE
};

class PyValue {
public:
    PyType type;
    bool boolVal;
    BigInteger intVal;
    double floatVal;
    std::string strVal;
    std::vector<PyValue> tupleVal;

    // Default: None
    PyValue() : type(PyType::NONE), boolVal(false), floatVal(0.0) {}

    static PyValue makeNone() {
        PyValue v;
        v.type = PyType::NONE;
        return v;
    }

    static PyValue makeBool(bool b) {
        PyValue v;
        v.type = PyType::BOOL;
        v.boolVal = b;
        return v;
    }

    static PyValue makeInt(const BigInteger &i) {
        PyValue v;
        v.type = PyType::INT;
        v.intVal = i;
        return v;
    }

    static PyValue makeInt(long long i) {
        return makeInt(BigInteger(i));
    }

    static PyValue makeFloat(double d) {
        PyValue v;
        v.type = PyType::FLOAT;
        v.floatVal = d;
        return v;
    }

    static PyValue makeStr(const std::string &s) {
        PyValue v;
        v.type = PyType::STR;
        v.strVal = s;
        return v;
    }

    static PyValue makeTuple(const std::vector<PyValue> &t) {
        PyValue v;
        v.type = PyType::TUPLE;
        v.tupleVal = t;
        return v;
    }

    // Truthiness
    bool toBool() const {
        switch (type) {
            case PyType::NONE: return false;
            case PyType::BOOL: return boolVal;
            case PyType::INT: return !intVal.isZero();
            case PyType::FLOAT: return floatVal != 0.0;
            case PyType::STR: return !strVal.empty();
            case PyType::TUPLE: return !tupleVal.empty();
        }
        return false;
    }

    // Convert to int
    BigInteger toInt() const {
        switch (type) {
            case PyType::BOOL: return BigInteger(boolVal ? 1 : 0);
            case PyType::INT: return intVal;
            case PyType::FLOAT: {
                // truncate towards zero
                double d = floatVal;
                bool neg = d < 0;
                if (neg) d = -d;
                d = std::floor(d);
                // Convert to string to avoid precision issues
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(0) << d;
                std::string s = oss.str();
                BigInteger result(s);
                if (neg) result = -result;
                return result;
            }
            case PyType::STR: return BigInteger(strVal);
            default: throw std::runtime_error("cannot convert to int");
        }
    }

    // Convert to float
    double toFloat() const {
        switch (type) {
            case PyType::BOOL: return boolVal ? 1.0 : 0.0;
            case PyType::INT: return intVal.toDouble();
            case PyType::FLOAT: return floatVal;
            case PyType::STR: return std::stod(strVal);
            default: throw std::runtime_error("cannot convert to float");
        }
    }

    // Convert to string (for str() builtin and f-strings)
    std::string toStr() const {
        switch (type) {
            case PyType::NONE: return "None";
            case PyType::BOOL: return boolVal ? "True" : "False";
            case PyType::INT: return intVal.toString();
            case PyType::FLOAT: {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(6) << floatVal;
                return oss.str();
            }
            case PyType::STR: return strVal;
            case PyType::TUPLE: {
                std::string result = "(";
                for (size_t i = 0; i < tupleVal.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += tupleVal[i].repr();
                }
                if (tupleVal.size() == 1) result += ",";
                result += ")";
                return result;
            }
        }
        return "";
    }

    // repr() for use inside tuples etc
    std::string repr() const {
        if (type == PyType::STR) {
            return "'" + strVal + "'";
        }
        return toStr();
    }

    // For print(): strings without quotes, float with 6dp
    std::string toPrintStr() const {
        return toStr();
    }

    // Arithmetic operations
    PyValue operator+(const PyValue &other) const {
        if (type == PyType::STR && other.type == PyType::STR) {
            return makeStr(strVal + other.strVal);
        }
        if (type == PyType::TUPLE && other.type == PyType::TUPLE) {
            std::vector<PyValue> result = tupleVal;
            result.insert(result.end(), other.tupleVal.begin(), other.tupleVal.end());
            return makeTuple(result);
        }
        // Numeric
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            return makeFloat(a.floatVal + b.floatVal);
        }
        return makeInt(a.intVal + b.intVal);
    }

    PyValue operator-(const PyValue &other) const {
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            return makeFloat(a.floatVal - b.floatVal);
        }
        return makeInt(a.intVal - b.intVal);
    }

    PyValue operator*(const PyValue &other) const {
        // str * int or int * str
        if (type == PyType::STR && isNumericType(other.type)) {
            int n = (int)other.toInt().toDouble();
            std::string result;
            for (int i = 0; i < n; ++i) result += strVal;
            return makeStr(result);
        }
        if (isNumericType(type) && other.type == PyType::STR) {
            int n = (int)toInt().toDouble();
            std::string result;
            for (int i = 0; i < n; ++i) result += other.strVal;
            return makeStr(result);
        }
        // Numeric
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            return makeFloat(a.floatVal * b.floatVal);
        }
        return makeInt(a.intVal * b.intVal);
    }

    // True division: always returns float
    PyValue trueDiv(const PyValue &other) const {
        double a = toFloat();
        double b = other.toFloat();
        return makeFloat(a / b);
    }

    // Floor division
    PyValue floorDiv(const PyValue &other) const {
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            return makeFloat(std::floor(a.floatVal / b.floatVal));
        }
        return makeInt(a.intVal.floorDiv(b.intVal));
    }

    // Modulo
    PyValue mod(const PyValue &other) const {
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            double q = std::floor(a.floatVal / b.floatVal);
            return makeFloat(a.floatVal - q * b.floatVal);
        }
        return makeInt(a.intVal % b.intVal);
    }

    // Unary minus
    PyValue negate() const {
        if (type == PyType::FLOAT) return makeFloat(-floatVal);
        auto v = toIntValue();
        return makeInt(-v.intVal);
    }

    // Unary plus
    PyValue pos() const {
        if (type == PyType::FLOAT) return makeFloat(floatVal);
        auto v = toIntValue();
        return makeInt(v.intVal);
    }

    // Comparisons
    // Returns -1, 0, 1 for ordering. Throws if incomparable.
    int compare(const PyValue &other) const {
        // String comparison
        if (type == PyType::STR && other.type == PyType::STR) {
            if (strVal < other.strVal) return -1;
            if (strVal > other.strVal) return 1;
            return 0;
        }
        // Numeric comparison (bool, int, float)
        auto [a, b] = promoteNumeric(*this, other);
        if (a.type == PyType::FLOAT) {
            if (a.floatVal < b.floatVal) return -1;
            if (a.floatVal > b.floatVal) return 1;
            return 0;
        }
        if (a.intVal < b.intVal) return -1;
        if (a.intVal > b.intVal) return 1;
        return 0;
    }

    bool eq(const PyValue &other) const {
        // ==: try to convert both sides to same type but won't convert to str
        // If conversion impossible, result is False
        if (type == PyType::STR || other.type == PyType::STR) {
            if (type != other.type) return false;
            return strVal == other.strVal;
        }
        if (type == PyType::NONE || other.type == PyType::NONE) {
            return type == other.type;
        }
        try {
            return compare(other) == 0;
        } catch (...) {
            return false;
        }
    }

    bool ne(const PyValue &other) const {
        return !eq(other);
    }

    bool lt(const PyValue &other) const { return compare(other) < 0; }
    bool gt(const PyValue &other) const { return compare(other) > 0; }
    bool le(const PyValue &other) const { return compare(other) <= 0; }
    bool ge(const PyValue &other) const { return compare(other) >= 0; }

private:
    static bool isNumericType(PyType t) {
        return t == PyType::BOOL || t == PyType::INT || t == PyType::FLOAT;
    }

    PyValue toIntValue() const {
        PyValue v;
        v.type = PyType::INT;
        v.intVal = toInt();
        return v;
    }

    // Promote two values to compatible numeric types
    static std::pair<PyValue, PyValue> promoteNumeric(const PyValue &a, const PyValue &b) {
        PyValue ra = a, rb = b;
        // Convert bool to int
        if (ra.type == PyType::BOOL) { ra.type = PyType::INT; ra.intVal = BigInteger(ra.boolVal ? 1 : 0); }
        if (rb.type == PyType::BOOL) { rb.type = PyType::INT; rb.intVal = BigInteger(rb.boolVal ? 1 : 0); }
        // If either is float, promote both to float
        if (ra.type == PyType::FLOAT || rb.type == PyType::FLOAT) {
            if (ra.type != PyType::FLOAT) {
                ra = makeFloat(ra.toFloat());
            }
            if (rb.type != PyType::FLOAT) {
                rb = makeFloat(rb.toFloat());
            }
        }
        return {ra, rb};
    }
};

#endif // PYTHON_INTERPRETER_PYVALUE_H
