#pragma once

#include <string>

namespace engine::core {

// Kronos ("Developer Velocity Sprint" -- "Live Math Evaluation"): a
// real, small recursive-descent arithmetic expression evaluator --
// +, -, *, /, unary +/-, parentheses, decimal literals. Pure and
// headlessly testable, no ImGui/UI dependency at all (studio::
// InspectorPanel's math-expression popup is the one real caller, but
// this function knows nothing about it).
//
// `currentValue` is only consulted when `text` (after trimming
// whitespace) begins with '+', '*', or '/' with no left-hand operand --
// e.g. "+10" evaluates as currentValue + 10, "*2" as currentValue * 2,
// "/3" as currentValue / 3, matching a spreadsheet-style relative-edit
// convention. A leading '-' is deliberately NOT treated as "subtract
// from current" -- it's the start of an ordinary negative-number
// literal ("-5" means "set to -5", not "subtract 5"), and every other
// expression (including a full one like "180 - 45") is evaluated as a
// complete, self-contained expression that replaces the value outright.
//
// Returns false (leaving outResult untouched) for a malformed
// expression (unbalanced parens, a stray operator, division by zero,
// trailing garbage after a valid expression) -- a real, honest failure,
// not a best-effort partial parse.
[[nodiscard]] bool evaluateMathExpression(const std::string& text, float currentValue, float& outResult);

} // namespace engine::core
