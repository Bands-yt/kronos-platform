#include "core/MathExpression.hpp"

#include <cctype>
#include <cstdlib>

namespace engine::core {

namespace {

// Real, small recursive-descent parser -- expr := term (('+'|'-') term)*,
// term := factor (('*'|'/') factor)*, factor := ['-'|'+'] primary,
// primary := NUMBER | '(' expr ')'. `failed` latches permanently true on
// the first real problem (a stray character, unbalanced parens, division
// by zero) -- once set, every subsequent parse step is a no-op that just
// returns 0.0, so the top-level caller only has to check it once at the
// end rather than propagate a bool through every recursive call.
class Parser {
public:
    // Owns a real copy, not a reference -- a caller passing a temporary
    // (e.g. trimmedText.substr(1) for the relative-operator case below)
    // would otherwise leave text_ dangling the instant the constructor
    // call finishes; a reference member's lifetime-extension rule
    // doesn't reach through a by-value/by-reference constructor
    // parameter to the temporary that was passed in.
    explicit Parser(std::string text) : text_(std::move(text)) {}

    float parseExpression() {
        float value = parseTerm();
        while (!failed_) {
            skipWhitespace();
            if (peek() == '+') {
                advance();
                value += parseTerm();
            } else if (peek() == '-') {
                advance();
                value -= parseTerm();
            } else {
                break;
            }
        }
        return value;
    }

    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] bool atEnd() {
        skipWhitespace();
        return pos_ >= text_.size();
    }

private:
    float parseTerm() {
        float value = parseFactor();
        while (!failed_) {
            skipWhitespace();
            if (peek() == '*') {
                advance();
                value *= parseFactor();
            } else if (peek() == '/') {
                advance();
                float divisor = parseFactor();
                if (!failed_ && divisor == 0.0f) {
                    failed_ = true;
                    return 0.0f;
                }
                if (!failed_) value /= divisor;
            } else {
                break;
            }
        }
        return value;
    }

    float parseFactor() {
        skipWhitespace();
        if (peek() == '-') {
            advance();
            return -parseFactor();
        }
        if (peek() == '+') {
            advance();
            return parseFactor();
        }
        return parsePrimary();
    }

    float parsePrimary() {
        skipWhitespace();
        if (peek() == '(') {
            advance();
            float value = parseExpression();
            skipWhitespace();
            if (peek() != ')') {
                failed_ = true;
                return 0.0f;
            }
            advance();
            return value;
        }

        size_t start = pos_;
        if (peek() == '.' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            while (pos_ < text_.size() &&
                   (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == '.')) {
                ++pos_;
            }
        }
        if (pos_ == start) {
            failed_ = true; // no digits/decimal point found -- not a valid number here
            return 0.0f;
        }
        char* endPtr = nullptr;
        float value = std::strtof(text_.substr(start, pos_ - start).c_str(), &endPtr);
        if (endPtr == nullptr || *endPtr != '\0') {
            failed_ = true;
            return 0.0f;
        }
        return value;
    }

    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) ++pos_;
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    void advance() { ++pos_; }

    std::string text_;
    size_t pos_ = 0;
    bool failed_ = false;
};

// Real, honest whitespace trim -- used to inspect the first meaningful
// character for the relative-operator convention below.
std::string trimmed(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

} // namespace

bool evaluateMathExpression(const std::string& text, float currentValue, float& outResult) {
    std::string trimmedText = trimmed(text);
    if (trimmedText.empty()) return false;

    char first = trimmedText.front();
    if (first == '+' || first == '*' || first == '/') {
        // Relative-to-current: currentValue <op> (the rest, itself a
        // real, full expression -- "+10" and "+ (5 * 2)" both work).
        Parser parser(trimmedText.substr(1));
        float rhs = parser.parseExpression();
        if (parser.failed() || !parser.atEnd()) return false;

        if (first == '+') {
            outResult = currentValue + rhs;
        } else if (first == '*') {
            outResult = currentValue * rhs;
        } else { // '/'
            if (rhs == 0.0f) return false;
            outResult = currentValue / rhs;
        }
        return true;
    }

    // Absolute: a full, self-contained expression that replaces the
    // value outright -- covers plain numbers ("42"), negative literals
    // ("-5"), and full expressions ("180 - 45").
    Parser parser(trimmedText);
    float value = parser.parseExpression();
    if (parser.failed() || !parser.atEnd()) return false;
    outResult = value;
    return true;
}

} // namespace engine::core
