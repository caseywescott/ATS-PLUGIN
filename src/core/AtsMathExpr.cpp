#include "AtsMathExpr.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <optional>
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

class Parser
{
public:
    const char* s = nullptr;
    const char* cur = nullptr;
    const char* end = nullptr;
    EvalCtx* ctx = nullptr;
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

    std::optional<double> parseNumber()
    {
        skipWs();
        if (cur >= end)
            return std::nullopt;
        char* next = nullptr;
        const std::string frag(cur, end);
        double v = std::strtod(frag.c_str(), &next);
        if (next == frag.c_str())
            return std::nullopt;
        cur += (const char*) next - frag.c_str();
        return v;
    }

    std::optional<std::string> parseIdent()
    {
        skipWs();
        if (cur >= end || (!std::isalpha((unsigned char) *cur) && *cur != '_'))
            return std::nullopt;
        const char* a = cur;
        while (cur < end && (std::isalnum((unsigned char) *cur) || *cur == '_'))
            ++cur;
        return std::string(a, cur);
    }

    std::optional<double> parseExpr(); // + -

    std::optional<double> parseAtom()
    {
        skipWs();
        if (cur >= end)
        {
            err = "Unexpected end of expression";
            return std::nullopt;
        }

        if (*cur == '(')
        {
            ++cur;
            auto v = parseExpr();
            if (!v)
                return std::nullopt;
            if (!eat(')'))
            {
                err = "Expected ')'";
                return std::nullopt;
            }
            return v;
        }

        if (std::isdigit((unsigned char) *cur) || (*cur == '.' && cur + 1 < end && std::isdigit((unsigned char) cur[1])))
            return parseNumber();

        auto idOpt = parseIdent();
        if (!idOpt)
        {
            err = "Expected number, identifier, or '('";
            return std::nullopt;
        }
        const std::string id = *idOpt;

        if (eat('('))
        {
            auto x = parseExpr();
            if (!x)
                return std::nullopt;

            if (id == "sin")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after sin";
                    return std::nullopt;
                }
                return std::sin(*x);
            }
            if (id == "cos")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after cos";
                    return std::nullopt;
                }
                return std::cos(*x);
            }
            if (id == "tan")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after tan";
                    return std::nullopt;
                }
                return std::tan(*x);
            }
            if (id == "asin")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after asin";
                    return std::nullopt;
                }
                return std::asin(*x);
            }
            if (id == "acos")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after acos";
                    return std::nullopt;
                }
                return std::acos(*x);
            }
            if (id == "atan")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after atan";
                    return std::nullopt;
                }
                return std::atan(*x);
            }
            if (id == "log")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after log";
                    return std::nullopt;
                }
                return std::log(*x);
            }
            if (id == "log10")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after log10";
                    return std::nullopt;
                }
                return std::log10(*x);
            }
            if (id == "exp")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after exp";
                    return std::nullopt;
                }
                return std::exp(*x);
            }
            if (id == "sqrt")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after sqrt";
                    return std::nullopt;
                }
                return std::sqrt(*x);
            }
            if (id == "abs" || id == "fabs")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after abs";
                    return std::nullopt;
                }
                return std::fabs(*x);
            }

            if (!eat(','))
            {
                err = "Expected ',' or ')' in function call";
                return std::nullopt;
            }
            auto y = parseExpr();
            if (!y)
                return std::nullopt;

            if (id == "pow")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after pow";
                    return std::nullopt;
                }
                return std::pow(*x, *y);
            }
            if (id == "atan2")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after atan2";
                    return std::nullopt;
                }
                return std::atan2(*x, *y);
            }
            if (id == "min")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after min";
                    return std::nullopt;
                }
                return std::min(*x, *y);
            }
            if (id == "max")
            {
                if (!eat(')'))
                {
                    err = "Expected ')' after max";
                    return std::nullopt;
                }
                return std::max(*x, *y);
            }

            if (id == "A" || id == "F" || id == "H")
            {
                int pi = (int) std::lround(*x);
                int ti = (int) std::lround(*y);
                if (!eat(')'))
                {
                    err = "Expected ')' after A/F/H";
                    return std::nullopt;
                }
                if (id == "A")
                    return afn(ctx, pi, ti);
                if (id == "F")
                    return ffn(ctx, pi, ti);
                return hfn(ctx, pi, ti);
            }

            err = "Unknown function: " + id;
            return std::nullopt;
        }

        if (id == "P")
            return (double) ctx->P;
        if (id == "T")
            return (double) ctx->T;
        if (id == "p")
            return (double) ctx->p;
        if (id == "t")
            return (double) ctx->t;
        if (id == "a")
            return ctx->a;
        if (id == "f")
            return ctx->f;
        if (id == "h")
            return ctx->h;
        if (id == "pi")
            return 3.14159265358979323846;
        if (id == "e")
            return 2.71828182845904523536;

        err = "Unknown identifier: " + id;
        return std::nullopt;
    }

    std::optional<double> parsePow()
    {
        auto lhs = parseAtom();
        if (!lhs)
            return std::nullopt;
        for (;;)
        {
            skipWs();
            if (cur >= end || *cur != '^')
                break;
            ++cur;
            auto rhs = parseAtom();
            if (!rhs)
                return std::nullopt;
            lhs = std::pow(*lhs, *rhs);
        }
        return lhs;
    }

    std::optional<double> parseUnary()
    {
        skipWs();
        if (cur < end && *cur == '-')
        {
            ++cur;
            auto v = parseUnary();
            if (!v)
                return std::nullopt;
            return -(*v);
        }
        if (cur < end && *cur == '+')
        {
            ++cur;
            return parseUnary();
        }
        return parsePow();
    }

    std::optional<double> parseMul()
    {
        auto lhs = parseUnary();
        if (!lhs)
            return std::nullopt;
        for (;;)
        {
            skipWs();
            if (cur >= end)
                break;
            const char op = *cur;
            if (op != '*' && op != '/')
                break;
            ++cur;
            auto rhs = parseUnary();
            if (!rhs)
                return std::nullopt;
            if (op == '*')
                lhs = *lhs * *rhs;
            else
            {
                if (*rhs == 0.0)
                {
                    err = "Division by zero";
                    return std::nullopt;
                }
                lhs = *lhs / *rhs;
            }
        }
        return lhs;
    }
};

std::optional<double> Parser::parseExpr()
{
    auto lhs = parseMul();
    if (!lhs)
        return std::nullopt;
    for (;;)
    {
        skipWs();
        if (cur >= end)
            break;
        const char op = *cur;
        if (op != '+' && op != '-')
            break;
        ++cur;
        auto rhs = parseMul();
        if (!rhs)
            return std::nullopt;
        if (op == '+')
            lhs = *lhs + *rhs;
        else
            lhs = *lhs - *rhs;
    }
    return lhs;
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

                Parser ps;
                ps.ctx = &ctx;
                ps.s = pass.expr.c_str();
                ps.cur = ps.s;
                ps.end = ps.s + pass.expr.size();

                auto val = ps.parseExpr();
                ps.skipWs();
                if (!val)
                {
                    errorMessage = "Expression error (partial " + std::to_string(p) + ", frame " + std::to_string(t)
                                   + "): " + (ps.err.empty() ? "invalid parse" : ps.err);
                    return false;
                }
                if (ps.cur != ps.end)
                {
                    errorMessage = "Trailing junk in expression at partial " + std::to_string(p) + ", frame "
                                   + std::to_string(t);
                    return false;
                }
                if (!std::isfinite(*val))
                {
                    errorMessage = "Non-finite value from expression.";
                    return false;
                }

                if (pass.target == 'a')
                    sound->amp[p][t] = std::max(0.0, *val);
                else if (pass.target == 'f')
                    sound->frq[p][t] = std::max(1e-6, *val);
                else if (pass.target == 'h')
                {
                    if (sound->pha != nullptr)
                        sound->pha[p][t] = *val;
                }
            }
        }
    }

    return true;
}

} // namespace ats
