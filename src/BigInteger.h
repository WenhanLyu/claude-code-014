#pragma once
#ifndef PYTHON_INTERPRETER_BIGINTEGER_H
#define PYTHON_INTERPRETER_BIGINTEGER_H

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>

class BigInteger {
private:
    // digits stored in base 10000, least significant first
    std::vector<int> digits;
    bool negative;

    void trim() {
        while (digits.size() > 1 && digits.back() == 0)
            digits.pop_back();
        if (digits.size() == 1 && digits[0] == 0)
            negative = false;
    }

    static const int BASE = 10000;

    // Compare absolute values. Returns -1, 0, 1
    static int cmpAbs(const BigInteger &a, const BigInteger &b) {
        if (a.digits.size() != b.digits.size())
            return a.digits.size() < b.digits.size() ? -1 : 1;
        for (int i = (int)a.digits.size() - 1; i >= 0; --i) {
            if (a.digits[i] != b.digits[i])
                return a.digits[i] < b.digits[i] ? -1 : 1;
        }
        return 0;
    }

    // Add absolute values
    static BigInteger addAbs(const BigInteger &a, const BigInteger &b) {
        BigInteger result;
        result.negative = false;
        int n = std::max(a.digits.size(), b.digits.size());
        result.digits.resize(n + 1, 0);
        int carry = 0;
        for (int i = 0; i < n || carry; ++i) {
            int sum = carry;
            if (i < (int)a.digits.size()) sum += a.digits[i];
            if (i < (int)b.digits.size()) sum += b.digits[i];
            if (i < (int)result.digits.size())
                result.digits[i] = sum % BASE;
            else
                result.digits.push_back(sum % BASE);
            carry = sum / BASE;
        }
        result.trim();
        return result;
    }

    // Subtract absolute values, assumes |a| >= |b|
    static BigInteger subAbs(const BigInteger &a, const BigInteger &b) {
        BigInteger result;
        result.negative = false;
        result.digits.resize(a.digits.size(), 0);
        int borrow = 0;
        for (int i = 0; i < (int)a.digits.size(); ++i) {
            int diff = a.digits[i] - borrow;
            if (i < (int)b.digits.size()) diff -= b.digits[i];
            if (diff < 0) {
                diff += BASE;
                borrow = 1;
            } else {
                borrow = 0;
            }
            result.digits[i] = diff;
        }
        result.trim();
        return result;
    }

public:
    BigInteger() : digits(1, 0), negative(false) {}

    BigInteger(long long val) {
        negative = val < 0;
        if (val < 0) val = -val;
        if (val == 0) {
            digits.push_back(0);
        } else {
            while (val > 0) {
                digits.push_back(val % BASE);
                val /= BASE;
            }
        }
    }

    BigInteger(const std::string &s) {
        negative = false;
        int start = 0;
        if (!s.empty() && s[0] == '-') {
            negative = true;
            start = 1;
        } else if (!s.empty() && s[0] == '+') {
            start = 1;
        }
        // skip leading zeros
        while (start < (int)s.size() - 1 && s[start] == '0') start++;

        if (start >= (int)s.size()) {
            digits.push_back(0);
            negative = false;
            return;
        }

        int len = (int)s.size() - start;
        digits.resize((len + 3) / 4, 0);
        for (int i = (int)s.size() - 1, idx = 0; i >= start; i -= 4, idx++) {
            int end = i;
            int begin = std::max(start, i - 3);
            int val = 0;
            for (int j = begin; j <= end; ++j)
                val = val * 10 + (s[j] - '0');
            digits[idx] = val;
        }
        trim();
    }

    bool isZero() const {
        return digits.size() == 1 && digits[0] == 0;
    }

    bool isNegative() const { return negative; }

    std::string toString() const {
        std::string result;
        if (negative) result += '-';
        result += std::to_string(digits.back());
        for (int i = (int)digits.size() - 2; i >= 0; --i) {
            std::string part = std::to_string(digits[i]);
            result += std::string(4 - part.size(), '0') + part;
        }
        return result;
    }

    double toDouble() const {
        double result = 0;
        double base = 1;
        for (int i = 0; i < (int)digits.size(); ++i) {
            result += digits[i] * base;
            base *= BASE;
        }
        return negative ? -result : result;
    }

    BigInteger operator-() const {
        BigInteger result = *this;
        if (!result.isZero())
            result.negative = !result.negative;
        return result;
    }

    BigInteger operator+(const BigInteger &other) const {
        if (negative == other.negative) {
            BigInteger result = addAbs(*this, other);
            result.negative = negative;
            result.trim();
            return result;
        }
        // Different signs
        int c = cmpAbs(*this, other);
        if (c == 0) return BigInteger(0);
        if (c > 0) {
            BigInteger result = subAbs(*this, other);
            result.negative = negative;
            result.trim();
            return result;
        } else {
            BigInteger result = subAbs(other, *this);
            result.negative = other.negative;
            result.trim();
            return result;
        }
    }

    BigInteger operator-(const BigInteger &other) const {
        BigInteger neg = -other;
        return *this + neg;
    }

    BigInteger operator*(const BigInteger &other) const {
        BigInteger result;
        result.digits.resize(digits.size() + other.digits.size(), 0);
        for (int i = 0; i < (int)digits.size(); ++i) {
            long long carry = 0;
            for (int j = 0; j < (int)other.digits.size() || carry; ++j) {
                long long cur = result.digits[i + j] + carry;
                if (j < (int)other.digits.size())
                    cur += (long long)digits[i] * other.digits[j];
                result.digits[i + j] = (int)(cur % BASE);
                carry = cur / BASE;
            }
        }
        result.negative = negative != other.negative;
        result.trim();
        return result;
    }

    // Floor division: rounds towards negative infinity
    // -5 // 3 = -2 (not -1)
    BigInteger floorDiv(const BigInteger &other) const {
        if (other.isZero()) {
            throw std::runtime_error("division by zero");
        }
        if (isZero()) return BigInteger(0);

        bool resultNeg = (negative != other.negative);

        // Divide absolute values
        BigInteger a = *this; a.negative = false;
        BigInteger b = other; b.negative = false;

        BigInteger quotient = divAbs(a, b);

        if (resultNeg) {
            // Check if there's a remainder
            BigInteger rem = a - quotient * b;
            if (!rem.isZero()) {
                quotient = quotient + BigInteger(1);
            }
            quotient.negative = true;
            quotient.trim();
        }
        return quotient;
    }

    BigInteger operator%(const BigInteger &other) const {
        // a % b = a - (a // b) * b
        BigInteger q = this->floorDiv(other);
        return *this - q * other;
    }

    // Comparison operators
    bool operator==(const BigInteger &other) const {
        return negative == other.negative && digits == other.digits;
    }
    bool operator!=(const BigInteger &other) const {
        return !(*this == other);
    }
    bool operator<(const BigInteger &other) const {
        if (negative != other.negative) return negative;
        int c = cmpAbs(*this, other);
        return negative ? c > 0 : c < 0;
    }
    bool operator>(const BigInteger &other) const {
        return other < *this;
    }
    bool operator<=(const BigInteger &other) const {
        return !(other < *this);
    }
    bool operator>=(const BigInteger &other) const {
        return !(*this < other);
    }

    friend std::ostream &operator<<(std::ostream &os, const BigInteger &b) {
        os << b.toString();
        return os;
    }

private:
    // Divide absolute values, returns quotient
    static BigInteger divAbs(const BigInteger &a, const BigInteger &b) {
        if (cmpAbs(a, b) < 0) return BigInteger(0);

        // Convert to string-based long division for simplicity
        std::string aStr = a.toString();
        std::string bStr = b.toString();

        if (aStr.size() < bStr.size()) return BigInteger(0);

        // Use the digit-by-digit approach
        BigInteger current;
        std::string quotientStr;

        for (int i = 0; i < (int)aStr.size(); ++i) {
            // Shift current left by 1 digit and add next digit
            current = current * BigInteger(10) + BigInteger(aStr[i] - '0');

            // Find the largest digit d such that b * d <= current
            int lo = 0, hi = 9, d = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                BigInteger t = b * BigInteger(mid);
                if (cmpAbs(t, current) <= 0) {
                    d = mid;
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
            quotientStr += (char)('0' + d);
            current = current - b * BigInteger(d);
        }

        return BigInteger(quotientStr);
    }
};

#endif // PYTHON_INTERPRETER_BIGINTEGER_H
