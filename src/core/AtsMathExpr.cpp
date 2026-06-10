#include "AtsMathExpr.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace ats
{
namespace
{

static void trimInPlace(std::string& s)
{
    while (!s.empty() && std::isspace((unsigned char) s.front()))
        s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char) s.back()))
        s.pop_back();
}

struct EvalCtx
{
    ATS_SOUND* sound = nullptr;
    int P = 0;
    int T = 0;
    int p = 0;
    int t = 0;
    double a = 0.0;
    double f = 0.0;
    double h = 0.0;
    bool hasPhase = false;
};

static bool readCell(const ATS_SOUND* s, int pi, int ti, double& oa, double& of, double& oh, bool& hasPh)
{
    if (s == nullptr || s->amp == nullptr || s->frq == nullptr)
        return false;
    if (pi < 0 || ti < 0 || pi >= s->partials || ti >= s->frames)
        return false;
    hasPh = (s->pha != nullptr);
    oa = s->amp[pi][ti];
    of = s->frq[pi][ti];
    oh = hasPh ? s->pha[pi][ti] : 0.0;
    return true;
}

static double afn(const EvalCtx* ctx, int pi, int ti)
{
    double oa {}, of {}, oh {};
    bool hp {};
    if (!readCell(ctx->sound, pi, ti, oa, of, oh, hp))
        return 0.0;
    return oa;
}

static double ffn(const EvalCtx* ctx, int pi, int ti)
{
    double oa {}, of {}, oh {};
    bool hp {};
    if (!readCell(ctx->sound, pi, ti, oa, of, oh, hp))
        return 0.0;
    return of;
}

static double hfn(const EvalCtx* ctx, int pi, int ti)
{
    double oa {}, of {}, oh {};
    bool hp {};
    if (!readCell(ctx->sound, pi, ti, oa, of, oh, hp))
        return 0.0;
    return hp ? oh : 0.0;
}

enum class ExprOp
{
    Constant,
    P,
    T,
    PartialIndex,
    FrameIndex,
    Amplitude,
    Frequency,
    Phase,
    Negate,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Log,
    Log10,
    Exp,
    Sqrt,
    Abs,
    Atan2,
    Min,
    Max,
    ReadAmplitude,
    ReadFrequency,
    ReadPhase,
};

struct ExprInstruction
{
    ExprOp op = ExprOp::Constant;
    double value = 0.0;
};

struct ExprProgram
{
    std::vector<ExprInstruction> instructions;

    bool evaluate(EvalCtx& ctx, std::vector<double>& stack, double& result, std::string& err) const
    {
        stack.clear();
        for (const auto& instruction : instructions)
        {
            const auto push = [&](double value) { stack.push_back(value); };
            const auto pop = [&]() {
                const double value = stack.back();
                stack.pop_back();
                return value;
            };

            switch (instruction.op)
            {
                case ExprOp::Constant:     push(instruction.value); break;
                case ExprOp::P:            push((double) ctx.P); break;
                case ExprOp::T:            push((double) ctx.T); break;
                case ExprOp::PartialIndex: push((double) ctx.p); break;
                case ExprOp::FrameIndex:   push((double) ctx.t); break;
                case ExprOp::Amplitude:    push(ctx.a); break;
                case ExprOp::Frequency:    push(ctx.f); break;
                case ExprOp::Phase:        push(ctx.h); break;

                case ExprOp::Negate: stack.back() = -stack.back(); break;
                case ExprOp::Sin:    stack.back() = std::sin(stack.back()); break;
                case ExprOp::Cos:    stack.back() = std::cos(stack.back()); break;
                case ExprOp::Tan:    stack.back() = std::tan(stack.back()); break;
                case ExprOp::Asin:   stack.back() = std::asin(stack.back()); break;
                case ExprOp::Acos:   stack.back() = std::acos(stack.back()); break;
                case ExprOp::Atan:   stack.back() = std::atan(stack.back()); break;
                case ExprOp::Log:    stack.back() = std::log(stack.back()); break;
                case ExprOp::Log10:  stack.back() = std::log10(stack.back()); break;
                case ExprOp::Exp:    stack.back() = std::exp(stack.back()); break;
                case ExprOp::Sqrt:   stack.back() = std::sqrt(stack.back()); break;
                case ExprOp::Abs:    stack.back() = std::fabs(stack.back()); break;

                case ExprOp::Add:
                case ExprOp::Subtract:
                case ExprOp::Multiply:
                case ExprOp::Divide:
                case ExprOp::Power:
                case ExprOp::Atan2:
                case ExprOp::Min:
                case ExprOp::Max:
                case ExprOp::ReadAmplitude:
                case ExprOp::ReadFrequency:
                case ExprOp::ReadPhase:
                {
                    const double y = pop();
                    const double x = pop();
                    switch (instruction.op)
                    {
                        case ExprOp::Add:      push(x + y); break;
                        case ExprOp::Subtract: push(x - y); break;
                        case ExprOp::Multiply: push(x * y); break;
                        case ExprOp::Divide:
                            if (y == 0.0)
                            {
                                err = "Division by zero";
                                return false;
                            }
                            push(x / y);
                            break;
                        case ExprOp::Power: push(std::pow(x, y)); break;
                        case ExprOp::Atan2: push(std::atan2(x, y)); break;
                        case ExprOp::Min:   push(std::min(x, y)); break;
                        case ExprOp::Max:   push(std::max(x, y)); break;
                        case ExprOp::ReadAmplitude:
                            push(afn(&ctx, (int) std::lround(x), (int) std::lround(y)));
                            break;
                        case ExprOp::ReadFrequency:
                            push(ffn(&ctx, (int) std::lround(x), (int) std::lround(y)));
                            break;
                        case ExprOp::ReadPhase:
                            push(hfn(&ctx, (int) std::lround(x), (int) std::lround(y)));
                            break;
                        default: break;
                    }
                    break;
                }
            }
        }

        if (stack.size() != 1)
        {
            err = "Internal expression evaluation error";
            return false;
        }
        result = stack.back();
        return true;
    }
};

class Compiler
{
public:
    const char* s = nullptr;
    const char* cur = nullptr;
    const char* end = nullptr;
    ExprProgram* program = nullptr;
    std::string err;

    void skipWs()
    {
        while (cur < end && std::isspace((unsigned char) *cur))
            ++cur;
    }

    bool fail(const char* msg)
    {
        if (err.empty())
            err = msg;
        return false;
    }

    bool eat(char c)
    {
        skipWs();
        if (cur < end && *cur == c)
        {
            ++cur;
            return true;
        }
        return false;
    }

    bool parseNumber()
    {
        skipWs();
        if (cur >= end)
            return false;
        char* next = nullptr;
        const std::string frag(cur, end);
        const double v = std::strtod(frag.c_str(), &next);
        if (next == frag.c_str())
            return false;
        cur += (const char*) next - frag.c_str();
        emit(ExprOp::Constant, v);
        return true;
    }

    bool parseIdent(std::string& ident)
    {
        skipWs();
        if (cur >= end || (!std::isalpha((unsigned char) *cur) && *cur != '_'))
            return false;
        const char* a = cur;
        while (cur < end && (std::isalnum((unsigned char) *cur) || *cur == '_'))
            ++cur;
        ident.assign(a, cur);
        return true;
    }

    void emit(ExprOp op, double value = 0.0)
    {
        program->instructions.push_back({ op, value });
    }

    bool parseExpr(); // + -

    bool parseAtom()
    {
        skipWs();
        if (cur >= end)
        {
            err = "Unexpected end of expression";
            return false;
        }

        if (*cur == '(')
        {
            ++cur;
            if (!parseExpr())
                return false;
            if (!eat(')'))
            {
                err = "Expected ')'";
                return false;
            }
            return true;
        }

        if (std::isdigit((unsigned char) *cur) || (*cur == '.' && cur + 1 < end && std::isdigit((unsigned char) cur[1])))
            return parseNumber();

        std::string id;
        if (!parseIdent(id))
        {
            err = "Expected number, identifier, or '('";
            return false;
        }

        if (eat('('))
        {
            if (!parseExpr())
                return false;

            ExprOp unaryOp {};
            bool isUnary = true;
            if (id == "sin") unaryOp = ExprOp::Sin;
            else if (id == "cos") unaryOp = ExprOp::Cos;
            else if (id == "tan") unaryOp = ExprOp::Tan;
            else if (id == "asin") unaryOp = ExprOp::Asin;
            else if (id == "acos") unaryOp = ExprOp::Acos;
            else if (id == "atan") unaryOp = ExprOp::Atan;
            else if (id == "log") unaryOp = ExprOp::Log;
            else if (id == "log10") unaryOp = ExprOp::Log10;
            else if (id == "exp") unaryOp = ExprOp::Exp;
            else if (id == "sqrt") unaryOp = ExprOp::Sqrt;
            else if (id == "abs" || id == "fabs") unaryOp = ExprOp::Abs;
            else isUnary = false;

            if (isUnary)
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after " + std::string(id == "fabs" ? "abs" : id);
                    return false;
                }
                emit(unaryOp);
                return true;
            }

            if (!eat(','))
            {
                err = "Expected ',' or ')' in function call";
                return false;
            }
            if (!parseExpr())
                return false;

            ExprOp binaryOp {};
            bool isBinary = true;
            if (id == "pow") binaryOp = ExprOp::Power;
            else if (id == "atan2") binaryOp = ExprOp::Atan2;
            else if (id == "min") binaryOp = ExprOp::Min;
            else if (id == "max") binaryOp = ExprOp::Max;
            else if (id == "A") binaryOp = ExprOp::ReadAmplitude;
            else if (id == "F") binaryOp = ExprOp::ReadFrequency;
            else if (id == "H") binaryOp = ExprOp::ReadPhase;
            else isBinary = false;

            if (isBinary)
            {
                if (!eat(')'))
                {
                    err = (id == "A" || id == "F" || id == "H")
                              ? "Expected ')' after A/F/H"
                              : "Expected ')' after " + id;
                    return false;
                }
                emit(binaryOp);
                return true;
            }

            err = "Unknown function: " + id;
            return false;
        }

        if (id == "P") emit(ExprOp::P);
        else if (id == "T") emit(ExprOp::T);
        else if (id == "p") emit(ExprOp::PartialIndex);
        else if (id == "t") emit(ExprOp::FrameIndex);
        else if (id == "a") emit(ExprOp::Amplitude);
        else if (id == "f") emit(ExprOp::Frequency);
        else if (id == "h") emit(ExprOp::Phase);
        else if (id == "pi") emit(ExprOp::Constant, 3.14159265358979323846);
        else if (id == "e") emit(ExprOp::Constant, 2.71828182845904523536);
        else
        {
            err = "Unknown identifier: " + id;
            return false;
        }

        return true;
    }

    bool parsePow()
    {
        if (!parseAtom())
            return false;
        for (;;)
        {
            skipWs();
            if (cur >= end || *cur != '^')
                break;
            ++cur;
            if (!parseAtom())
                return false;
            emit(ExprOp::Power);
        }
        return true;
    }

    bool parseUnary()
    {
        skipWs();
        if (cur < end && *cur == '-')
        {
            ++cur;
            if (!parseUnary())
                return false;
            emit(ExprOp::Negate);
            return true;
        }
        if (cur < end && *cur == '+')
        {
            ++cur;
            return parseUnary();
        }
        return parsePow();
    }

    bool parseMul()
    {
        if (!parseUnary())
            return false;
        for (;;)
        {
            skipWs();
            if (cur >= end)
                break;
            const char op = *cur;
            if (op != '*' && op != '/')
                break;
            ++cur;
            if (!parseUnary())
                return false;
            emit(op == '*' ? ExprOp::Multiply : ExprOp::Divide);
        }
        return true;
    }
};

bool Compiler::parseExpr()
{
    if (!parseMul())
        return false;
    for (;;)
    {
        skipWs();
        if (cur >= end)
            break;
        const char op = *cur;
        if (op != '+' && op != '-')
            break;
        ++cur;
        if (!parseMul())
            return false;
        emit(op == '+' ? ExprOp::Add : ExprOp::Subtract);
    }
    return true;
}

static bool parseAssignmentLine(const std::string& line, char& target, std::string& rhs)
{
    std::string s = line;
    trimInPlace(s);
    if (s.empty() || s[0] == '#')
        return false;
    if (s.size() < 3)
        return false;
    target = (char) std::tolower((unsigned char) s[0]);
    if (target != 'a' && target != 'f' && target != 'h')
        return false;
    if (s[1] != '=')
        return false;
    rhs = s.substr(2);
    trimInPlace(rhs);
    return !rhs.empty();
}

} // namespace

bool applyMathExprScript(ATS_SOUND* sound,
                         const std::string& script,
                         std::string& errorMessage,
                         const std::function<bool()>& shouldCancel)
{
    errorMessage.clear();
    if (sound == nullptr || sound->amp == nullptr || sound->frq == nullptr)
    {
        errorMessage = "No valid partial data in model.";
        return false;
    }

    const int P = sound->partials;
    const int T = sound->frames;
    if (P <= 0 || T <= 0)
    {
        errorMessage = "Empty model.";
        return false;
    }

    struct Pass
    {
        char target = 0;
        std::string expr;
    };
    std::vector<Pass> passes;

    std::string acc;
    for (size_t i = 0; i <= script.size(); ++i)
    {
        const char c = (i < script.size() ? script[i] : '\n');
        if (c == '\r')
            continue;
        if (c == '\n' || i == script.size())
        {
            trimInPlace(acc);
            if (!acc.empty() && acc[0] != '#')
            {
                char tgt = 0;
                std::string rhs;
                if (!parseAssignmentLine(acc, tgt, rhs))
                {
                    errorMessage = "Each non-empty line must look like: a = <expr>  or  f = <expr>  or  h = <expr>\nOffending line: "
                                   + acc;
                    return false;
                }
                if (tgt == 'h' && sound->pha == nullptr)
                {
                    errorMessage = "Model has no phase data; cannot use h = ...";
                    return false;
                }
                passes.push_back({ tgt, rhs });
            }
            acc.clear();
        }
        else
            acc.push_back(c);
    }

    EvalCtx ctx {};
    ctx.sound = sound;
    ctx.P = P;
    ctx.T = T;

    for (const auto& pass : passes)
    {
        ExprProgram program;
        Compiler compiler;
        compiler.program = &program;
        compiler.s = pass.expr.c_str();
        compiler.cur = compiler.s;
        compiler.end = compiler.s + pass.expr.size();
        if (!compiler.parseExpr())
        {
            errorMessage = "Expression error (partial 0, frame 0): "
                           + (compiler.err.empty() ? "invalid parse" : compiler.err);
            return false;
        }
        compiler.skipWs();
        if (compiler.cur != compiler.end)
        {
            errorMessage = "Trailing junk in expression at partial 0, frame 0";
            return false;
        }

        std::vector<double> evalStack;
        evalStack.reserve(program.instructions.size());
        for (int p = 0; p < P; ++p)
        {
            for (int t = 0; t < T; ++t)
            {
                if (shouldCancel && shouldCancel())
                {
                    errorMessage = "Cancelled.";
                    return false;
                }

                ctx.p = p;
                ctx.t = t;
                bool hp = false;
                if (!readCell(sound, p, t, ctx.a, ctx.f, ctx.h, hp))
                {
                    errorMessage = "Internal read error.";
                    return false;
                }
                ctx.hasPhase = hp;

                double val = 0.0;
                std::string evalError;
                if (!program.evaluate(ctx, evalStack, val, evalError))
                {
                    errorMessage = "Expression error (partial " + std::to_string(p) + ", frame " + std::to_string(t)
                                   + "): " + (evalError.empty() ? "invalid evaluation" : evalError);
                    return false;
                }
                if (!std::isfinite(val))
                {
                    errorMessage = "Non-finite value from expression.";
                    return false;
                }

                if (pass.target == 'a')
                    sound->amp[p][t] = std::max(0.0, val);
                else if (pass.target == 'f')
                    sound->frq[p][t] = std::max(1e-6, val);
                else if (pass.target == 'h')
                {
                    if (sound->pha != nullptr)
                        sound->pha[p][t] = val;
                }
            }
        }
    }

    return true;
}

} // namespace ats
