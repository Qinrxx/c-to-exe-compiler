// mycc v2 -- a small C compiler targeting Windows x64 (mingw-w64 linker).
// Single-file C++17. Compiles a substantial C subset to .exe via gcc.
//
// Supported:
//   types      : int (64-bit), char, void, pointer-to-T, T[N]
//   storage    : local vars, globals (zero-init or constant-init scalar)
//   literals   : decimal/hex/octal ints, char 'x', string "..."
//   operators  : + - * / % , unary - ~ ! , & | ^ << >> , < <= > >= == != ,
//                && || , = += -= *= /= %= , ++ -- (pre/post) , ?: , sizeof ,
//                & (address-of) , * (dereference) , [] , function call
//   control    : if/else, while, do-while, for, break, continue, return
//   functions  : any number of parameters, int/char/ptr return or void,
//                recursion, external calls (anything not locally defined is
//                treated as an external and resolved by the linker -- this
//                lets you call printf, puts, malloc, etc. directly).
//   misc       : nested scopes, preprocessor lines (#...) are skipped,
//                /* */ and // comments
//
// Diagnostics: errors report file:line:col with a caret under the offending
// token. Multiple errors are accumulated before bailing.
//
// Not supported: float/double, structs/unions, typedef, multi-var decls,
// function prototypes (not needed -- use definitions or call externals),
// array initializer lists, designated inits, VLAs, goto.

#include <bits/stdc++.h>
using namespace std;

// ===========================================================================
// Diagnostics
// ===========================================================================
static string g_file;
static string g_text;
static vector<int> g_lineStart; // index i = offset of line i+1 (1-based lines)
static int g_errors = 0;

static void initSource(const string& name, const string& text) {
    g_file = name;
    g_text = text;
    g_lineStart.clear();
    g_lineStart.push_back(0);
    for (size_t i = 0; i < text.size(); i++)
        if (text[i] == '\n') g_lineStart.push_back((int)i + 1);
}

static string getLine(int line) {
    if (line < 1 || line > (int)g_lineStart.size()) return "";
    int start = g_lineStart[line - 1];
    int end = (line < (int)g_lineStart.size()) ? g_lineStart[line] - 1 : (int)g_text.size();
    string s = g_text.substr(start, max(0, end - start));
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

static void diag(const char* kind, int line, int col, const string& msg) {
    fprintf(stderr, "%s:%d:%d: %s: %s\n",
            g_file.c_str(), line, col, kind, msg.c_str());
    string src = getLine(line);
    if (!src.empty()) {
        fprintf(stderr, "  %s\n  ", src.c_str());
        for (int i = 1; i < col && i - 1 < (int)src.size(); i++)
            fputc(src[i - 1] == '\t' ? '\t' : ' ', stderr);
        fputs("^\n", stderr);
    }
}

static void errorAt(int line, int col, const string& msg) {
    g_errors++;
    diag("error", line, col, msg);
}

[[noreturn]] static void fatalAt(int line, int col, const string& msg) {
    errorAt(line, col, msg);
    exit(1);
}

[[noreturn]] static void fatal(const string& msg) {
    fprintf(stderr, "mycc: error: %s\n", msg.c_str());
    exit(1);
}

// ===========================================================================
// Types
// ===========================================================================
struct Type;
using TP = shared_ptr<Type>;

struct Type {
    enum Kind { VOID, CHAR, INT, PTR, ARRAY } kind;
    TP base;
    int arrayLen = 0;
};

static TP tVoid() { static TP t = make_shared<Type>(Type{Type::VOID, nullptr, 0}); return t; }
static TP tChar() { static TP t = make_shared<Type>(Type{Type::CHAR, nullptr, 0}); return t; }
static TP tInt()  { static TP t = make_shared<Type>(Type{Type::INT,  nullptr, 0}); return t; }
static TP ptrTo(TP base) {
    auto t = make_shared<Type>();
    t->kind = Type::PTR; t->base = base;
    return t;
}
static TP arrayOf(TP base, int n) {
    auto t = make_shared<Type>();
    t->kind = Type::ARRAY; t->base = base; t->arrayLen = n;
    return t;
}
static int sizeOf(TP t) {
    switch (t->kind) {
        case Type::VOID: return 1;
        case Type::CHAR: return 1;
        case Type::INT:  return 8;
        case Type::PTR:  return 8;
        case Type::ARRAY: return sizeOf(t->base) * t->arrayLen;
    }
    return 8;
}
static bool isInteger(TP t) { return t->kind == Type::CHAR || t->kind == Type::INT; }
static bool isPointerOrArray(TP t) { return t->kind == Type::PTR || t->kind == Type::ARRAY; }
static bool isScalar(TP t) { return isInteger(t) || isPointerOrArray(t); }
static TP decayType(TP t) {
    if (t->kind == Type::ARRAY) return ptrTo(t->base);
    return t;
}
static string typeStr(TP t) {
    if (!t) return "?";
    switch (t->kind) {
        case Type::VOID: return "void";
        case Type::CHAR: return "char";
        case Type::INT:  return "int";
        case Type::PTR:  return typeStr(t->base) + "*";
        case Type::ARRAY: return typeStr(t->base) + "[" + to_string(t->arrayLen) + "]";
    }
    return "?";
}

// ===========================================================================
// Tokens
// ===========================================================================
enum Tok {
    T_EOF,
    T_INT, T_CHAR, T_VOID, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR,
    T_RETURN, T_BREAK, T_CONTINUE, T_SIZEOF,
    T_IDENT, T_NUM, T_STRING,
    T_LP, T_RP, T_LB, T_RB, T_LSB, T_RSB, T_SEMI, T_COMMA, T_QMARK, T_COLON,
    T_ASSIGN, T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ, T_PERCEQ,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_INC, T_DEC,
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_ANDAND, T_OROR, T_NOT,
    T_AMP, T_PIPE, T_CARET, T_TILDE, T_SHL, T_SHR,
    T_ELLIPSIS,
};

struct Token {
    Tok t;
    string s;
    long long v = 0;
    int line = 0, col = 0;
};

// ===========================================================================
// Lexer
// ===========================================================================
struct Lexer {
    string src;
    size_t p = 0;
    int line = 1, col = 1;
    vector<Token> out;

    void advance() {
        if (p < src.size()) {
            if (src[p] == '\n') { line++; col = 1; }
            else col++;
            p++;
        }
    }
    char peek(int o = 0) { return p + o < src.size() ? src[p + o] : 0; }
    void push(Tok t, int l, int c, string s = "", long long v = 0) {
        out.push_back({t, s, v, l, c});
    }

    bool starts(const char* m) {
        size_t n = strlen(m);
        if (p + n > src.size()) return false;
        return src.compare(p, n, m) == 0;
    }

    char readEscape(int l, int c) {
        advance(); // past backslash
        if (p >= src.size()) { errorAt(l, c, "unterminated escape"); return 0; }
        char n = src[p]; advance();
        switch (n) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case '0': return '\0';
            case '\\': return '\\';
            case '\'': return '\'';
            case '"': return '"';
            case 'a': return '\a';
            case 'b': return '\b';
            case 'f': return '\f';
            case 'v': return '\v';
            case 'x': {
                int v = 0, cnt = 0;
                while (p < src.size() && isxdigit((unsigned char)src[p]) && cnt < 2) {
                    char d = src[p];
                    int dv = (d >= '0' && d <= '9') ? d - '0'
                           : (d >= 'a' && d <= 'f') ? d - 'a' + 10
                           : d - 'A' + 10;
                    v = v * 16 + dv;
                    advance();
                    cnt++;
                }
                return (char)v;
            }
            default: return n;
        }
    }

    void run() {
        // Skip a leading '#' line unconditionally (preprocessor lines).
        while (p < src.size()) {
            int l = line, c = col;
            char ch = src[p];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { advance(); continue; }
            if (ch == '#') {
                // skip to end of line, honoring line continuations
                while (p < src.size() && src[p] != '\n') {
                    if (src[p] == '\\' && p + 1 < src.size() && src[p + 1] == '\n') {
                        advance(); advance(); continue;
                    }
                    advance();
                }
                continue;
            }
            if (ch == '/' && peek(1) == '/') {
                while (p < src.size() && src[p] != '\n') advance();
                continue;
            }
            if (ch == '/' && peek(1) == '*') {
                advance(); advance();
                while (p + 1 < src.size() && !(src[p] == '*' && src[p + 1] == '/')) advance();
                if (p + 1 < src.size()) { advance(); advance(); }
                continue;
            }
            if (isalpha((unsigned char)ch) || ch == '_') {
                size_t s = p;
                while (p < src.size() && (isalnum((unsigned char)src[p]) || src[p] == '_')) advance();
                string id = src.substr(s, p - s);
                static const unordered_map<string, Tok> kw = {
                    {"int", T_INT}, {"char", T_CHAR}, {"void", T_VOID},
                    {"if", T_IF}, {"else", T_ELSE},
                    {"while", T_WHILE}, {"do", T_DO}, {"for", T_FOR},
                    {"return", T_RETURN}, {"break", T_BREAK}, {"continue", T_CONTINUE},
                    {"sizeof", T_SIZEOF},
                };
                auto it = kw.find(id);
                if (it != kw.end()) push(it->second, l, c, id);
                else push(T_IDENT, l, c, id);
                continue;
            }
            if (isdigit((unsigned char)ch)) {
                long long v = 0;
                if (ch == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                    advance(); advance();
                    while (p < src.size() && isxdigit((unsigned char)src[p])) {
                        char d = src[p];
                        int dv = (d >= '0' && d <= '9') ? d - '0'
                               : (d >= 'a' && d <= 'f') ? d - 'a' + 10
                               : d - 'A' + 10;
                        v = v * 16 + dv;
                        advance();
                    }
                } else if (ch == '0') {
                    advance();
                    while (p < src.size() && src[p] >= '0' && src[p] <= '7') {
                        v = v * 8 + (src[p] - '0');
                        advance();
                    }
                } else {
                    while (p < src.size() && isdigit((unsigned char)src[p])) {
                        v = v * 10 + (src[p] - '0');
                        advance();
                    }
                }
                push(T_NUM, l, c, "", v);
                continue;
            }
            if (ch == '\'') {
                advance();
                long long v = 0;
                if (p < src.size() && src[p] == '\\') {
                    v = (unsigned char)readEscape(l, c);
                } else if (p < src.size()) {
                    v = (unsigned char)src[p];
                    advance();
                }
                if (p < src.size() && src[p] == '\'') advance();
                else errorAt(l, c, "unterminated character literal");
                push(T_NUM, l, c, "", v);
                continue;
            }
            if (ch == '"') {
                advance();
                string s;
                while (p < src.size() && src[p] != '"') {
                    if (src[p] == '\\') s += readEscape(l, c);
                    else { s += src[p]; advance(); }
                }
                if (p < src.size()) advance();
                else errorAt(l, c, "unterminated string literal");
                push(T_STRING, l, c, s);
                continue;
            }
            // Multi-char operators
            if (starts("...")) { push(T_ELLIPSIS, l, c); advance(); advance(); advance(); continue; }
            if (starts("==")) { push(T_EQ, l, c); advance(); advance(); continue; }
            if (starts("!=")) { push(T_NE, l, c); advance(); advance(); continue; }
            if (starts("<=")) { push(T_LE, l, c); advance(); advance(); continue; }
            if (starts(">=")) { push(T_GE, l, c); advance(); advance(); continue; }
            if (starts("&&")) { push(T_ANDAND, l, c); advance(); advance(); continue; }
            if (starts("||")) { push(T_OROR, l, c); advance(); advance(); continue; }
            if (starts("<<")) { push(T_SHL, l, c); advance(); advance(); continue; }
            if (starts(">>")) { push(T_SHR, l, c); advance(); advance(); continue; }
            if (starts("++")) { push(T_INC, l, c); advance(); advance(); continue; }
            if (starts("--")) { push(T_DEC, l, c); advance(); advance(); continue; }
            if (starts("+=")) { push(T_PLUSEQ, l, c); advance(); advance(); continue; }
            if (starts("-=")) { push(T_MINUSEQ, l, c); advance(); advance(); continue; }
            if (starts("*=")) { push(T_STAREQ, l, c); advance(); advance(); continue; }
            if (starts("/=")) { push(T_SLASHEQ, l, c); advance(); advance(); continue; }
            if (starts("%=")) { push(T_PERCEQ, l, c); advance(); advance(); continue; }
            switch (ch) {
                case '(': push(T_LP, l, c); advance(); break;
                case ')': push(T_RP, l, c); advance(); break;
                case '{': push(T_LB, l, c); advance(); break;
                case '}': push(T_RB, l, c); advance(); break;
                case '[': push(T_LSB, l, c); advance(); break;
                case ']': push(T_RSB, l, c); advance(); break;
                case ';': push(T_SEMI, l, c); advance(); break;
                case ',': push(T_COMMA, l, c); advance(); break;
                case '?': push(T_QMARK, l, c); advance(); break;
                case ':': push(T_COLON, l, c); advance(); break;
                case '=': push(T_ASSIGN, l, c); advance(); break;
                case '+': push(T_PLUS, l, c); advance(); break;
                case '-': push(T_MINUS, l, c); advance(); break;
                case '*': push(T_STAR, l, c); advance(); break;
                case '/': push(T_SLASH, l, c); advance(); break;
                case '%': push(T_PERCENT, l, c); advance(); break;
                case '<': push(T_LT, l, c); advance(); break;
                case '>': push(T_GT, l, c); advance(); break;
                case '!': push(T_NOT, l, c); advance(); break;
                case '&': push(T_AMP, l, c); advance(); break;
                case '|': push(T_PIPE, l, c); advance(); break;
                case '^': push(T_CARET, l, c); advance(); break;
                case '~': push(T_TILDE, l, c); advance(); break;
                default:
                    fatalAt(l, c, string("unexpected character '") + ch + "'");
            }
        }
        out.push_back({T_EOF, "", 0, line, col});
    }
};

// ===========================================================================
// AST
// ===========================================================================
struct Node {
    string kind;
    long long num = 0;
    string s;
    int line = 0, col = 0;
    TP type;       // for expressions (result type) and DECL/PARAM/GLOBAL (declared type)
    vector<unique_ptr<Node>> ch;
    bool variadic = false; // for FUNC
};
using NP = unique_ptr<Node>;
static NP mk(const string& k, int line = 0, int col = 0) {
    auto n = make_unique<Node>();
    n->kind = k; n->line = line; n->col = col;
    return n;
}

// ===========================================================================
// Parser
// ===========================================================================
struct Parser {
    vector<Token>& toks;
    size_t p = 0;

    Parser(vector<Token>& t) : toks(t) {}
    Token& cur() { return toks[p]; }
    Token& peek(int o = 1) { return toks[p + o]; }
    bool is(Tok t) { return toks[p].t == t; }
    bool accept(Tok t) { if (is(t)) { p++; return true; } return false; }
    Token& eat(Tok t, const char* what) {
        if (!is(t)) fatalAt(cur().line, cur().col, string("expected ") + what);
        return toks[p++];
    }

    bool isTypeStart() {
        return is(T_INT) || is(T_CHAR) || is(T_VOID);
    }

    TP parseTypeBase() {
        Token& t = toks[p++];
        TP base;
        switch (t.t) {
            case T_INT:  base = tInt();  break;
            case T_CHAR: base = tChar(); break;
            case T_VOID: base = tVoid(); break;
            default: fatalAt(t.line, t.col, "expected type");
        }
        while (accept(T_STAR)) base = ptrTo(base);
        return base;
    }

    NP parseProgram() {
        auto n = mk("PROG");
        while (!is(T_EOF)) {
            n->ch.push_back(parseTopLevel());
        }
        return n;
    }

    NP parseTopLevel() {
        int ln = cur().line, cl = cur().col;
        TP base = parseTypeBase();
        Token& id = eat(T_IDENT, "identifier");
        if (accept(T_LP)) {
            // function definition
            auto fn = mk("FUNC", id.line, id.col);
            fn->s = id.s;
            fn->type = base; // return type
            auto params = mk("PARAMS");
            bool variadic = false;
            if (!is(T_RP)) {
                do {
                    if (accept(T_ELLIPSIS)) { variadic = true; break; }
                    if (is(T_VOID) && peek().t == T_RP) { p++; break; } // f(void)
                    int pln = cur().line, pcl = cur().col;
                    TP pt = parseTypeBase();
                    Token& pid = eat(T_IDENT, "parameter name");
                    if (accept(T_LSB)) {
                        if (!is(T_RSB)) eat(T_NUM, "array size");
                        eat(T_RSB, "]");
                        pt = ptrTo(pt);
                    }
                    auto pn = mk("PARAM", pln, pcl);
                    pn->s = pid.s; pn->type = pt;
                    params->ch.push_back(move(pn));
                } while (accept(T_COMMA));
            }
            eat(T_RP, ")");
            fn->ch.push_back(move(params));
            fn->variadic = variadic;
            if (accept(T_SEMI)) {
                fn->kind = "PROTO";
                return fn;
            }
            fn->ch.push_back(parseBlock());
            return fn;
        }
        // global variable
        TP vt = base;
        if (accept(T_LSB)) {
            Token& sz = eat(T_NUM, "array size");
            eat(T_RSB, "]");
            vt = arrayOf(base, (int)sz.v);
        }
        auto g = mk("GLOBAL", id.line, id.col);
        g->s = id.s; g->type = vt;
        if (accept(T_ASSIGN)) {
            // constant initializer only
            int il = cur().line, ic = cur().col;
            bool neg = false;
            if (accept(T_MINUS)) neg = true;
            if (is(T_NUM)) {
                g->num = (neg ? -1 : 1) * toks[p++].v;
                auto init = mk("INIT_NUM"); init->num = g->num;
                g->ch.push_back(move(init));
            } else if (is(T_STRING)) {
                auto init = mk("INIT_STR"); init->s = toks[p++].s;
                g->ch.push_back(move(init));
            } else {
                errorAt(il, ic, "global initializer must be a constant");
            }
        }
        eat(T_SEMI, ";");
        return g;
    }

    NP parseBlock() {
        eat(T_LB, "{");
        auto b = mk("BLOCK");
        while (!is(T_RB) && !is(T_EOF)) b->ch.push_back(parseStmt());
        eat(T_RB, "}");
        return b;
    }

    NP parseStmt() {
        int ln = cur().line, cl = cur().col;
        if (is(T_LB)) return parseBlock();
        if (accept(T_SEMI)) return mk("EMPTY", ln, cl);
        if (isTypeStart()) return parseLocalDecl(true);
        if (is(T_IF)) {
            p++; eat(T_LP, "("); auto c = parseExpr(); eat(T_RP, ")");
            auto th = parseStmt();
            auto n = mk("IF", ln, cl);
            n->ch.push_back(move(c));
            n->ch.push_back(move(th));
            if (accept(T_ELSE)) n->ch.push_back(parseStmt());
            return n;
        }
        if (is(T_WHILE)) {
            p++; eat(T_LP, "("); auto c = parseExpr(); eat(T_RP, ")");
            auto body = parseStmt();
            auto n = mk("WHILE", ln, cl);
            n->ch.push_back(move(c));
            n->ch.push_back(move(body));
            return n;
        }
        if (is(T_DO)) {
            p++;
            auto body = parseStmt();
            eat(T_WHILE, "while");
            eat(T_LP, "("); auto c = parseExpr(); eat(T_RP, ")");
            eat(T_SEMI, ";");
            auto n = mk("DOWHILE", ln, cl);
            n->ch.push_back(move(body));
            n->ch.push_back(move(c));
            return n;
        }
        if (is(T_FOR)) {
            p++; eat(T_LP, "(");
            auto n = mk("FOR", ln, cl);
            // init
            if (accept(T_SEMI)) n->ch.push_back(mk("EMPTY"));
            else if (isTypeStart()) n->ch.push_back(parseLocalDecl(true));
            else {
                auto e = parseExpr(); eat(T_SEMI, ";");
                auto es = mk("EXPRSTMT"); es->ch.push_back(move(e));
                n->ch.push_back(move(es));
            }
            // cond
            if (is(T_SEMI)) n->ch.push_back(mk("EMPTY"));
            else n->ch.push_back(parseExpr());
            eat(T_SEMI, ";");
            // step
            if (is(T_RP)) n->ch.push_back(mk("EMPTY"));
            else n->ch.push_back(parseExpr());
            eat(T_RP, ")");
            n->ch.push_back(parseStmt());
            return n;
        }
        if (is(T_RETURN)) {
            p++;
            auto n = mk("RET", ln, cl);
            if (!is(T_SEMI)) n->ch.push_back(parseExpr());
            eat(T_SEMI, ";");
            return n;
        }
        if (is(T_BREAK))    { p++; eat(T_SEMI, ";"); return mk("BREAK", ln, cl); }
        if (is(T_CONTINUE)) { p++; eat(T_SEMI, ";"); return mk("CONTINUE", ln, cl); }
        auto e = parseExpr(); eat(T_SEMI, ";");
        auto es = mk("EXPRSTMT", ln, cl); es->ch.push_back(move(e));
        return es;
    }

    NP parseLocalDecl(bool requireSemi) {
        int ln = cur().line, cl = cur().col;
        TP base = parseTypeBase();
        Token& id = eat(T_IDENT, "variable name");
        TP vt = base;
        if (accept(T_LSB)) {
            Token& sz = eat(T_NUM, "array size");
            eat(T_RSB, "]");
            vt = arrayOf(base, (int)sz.v);
        }
        auto d = mk("DECL", ln, cl);
        d->s = id.s; d->type = vt;
        if (accept(T_ASSIGN)) d->ch.push_back(parseExpr());
        if (requireSemi) eat(T_SEMI, ";");
        return d;
    }

    // Expression grammar (precedence, low -> high):
    //   assign     = += -= *= /= %=       (right assoc)
    //   ternary    ?:
    //   lor        ||
    //   land       &&
    //   bor        |
    //   bxor       ^
    //   band       &
    //   equality   == !=
    //   rel        < > <= >=
    //   shift      << >>
    //   additive   + -
    //   mult       * / %
    //   unary      & * + - ! ~ ++ -- sizeof
    //   postfix    () [] ++ --
    //   primary
    NP parseExpr() { return parseAssign(); }

    NP parseAssign() {
        auto l = parseTernary();
        int ln = cur().line, cl = cur().col;
        if (accept(T_ASSIGN)) {
            auto r = parseAssign();
            auto n = mk("ASSIGN", ln, cl);
            n->ch.push_back(move(l));
            n->ch.push_back(move(r));
            return n;
        }
        static const unordered_map<int, string> compound = {
            {T_PLUSEQ, "ADD"}, {T_MINUSEQ, "SUB"},
            {T_STAREQ, "MUL"}, {T_SLASHEQ, "DIV"},
            {T_PERCEQ, "MOD"},
        };
        auto it = compound.find(cur().t);
        if (it != compound.end()) {
            p++;
            auto r = parseAssign();
            auto n = mk("COMPASSIGN", ln, cl);
            n->s = it->second;
            n->ch.push_back(move(l));
            n->ch.push_back(move(r));
            return n;
        }
        return l;
    }

    NP parseTernary() {
        auto c = parseLor();
        if (accept(T_QMARK)) {
            auto t = parseExpr();
            eat(T_COLON, ":");
            auto e = parseTernary();
            auto n = mk("TERN");
            n->ch.push_back(move(c));
            n->ch.push_back(move(t));
            n->ch.push_back(move(e));
            return n;
        }
        return c;
    }

    NP parseLor() {
        auto l = parseLand();
        while (accept(T_OROR)) {
            auto r = parseLand();
            auto n = mk("LOR"); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseLand() {
        auto l = parseBor();
        while (accept(T_ANDAND)) {
            auto r = parseBor();
            auto n = mk("LAND"); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseBor() {
        auto l = parseBxor();
        while (accept(T_PIPE)) {
            auto r = parseBxor();
            auto n = mk("BOR"); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseBxor() {
        auto l = parseBand();
        while (accept(T_CARET)) {
            auto r = parseBand();
            auto n = mk("BXOR"); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseBand() {
        auto l = parseEq();
        while (accept(T_AMP)) {
            auto r = parseEq();
            auto n = mk("BAND"); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseEq() {
        auto l = parseRel();
        while (is(T_EQ) || is(T_NE)) {
            string op = is(T_EQ) ? "EQ" : "NE"; p++;
            auto r = parseRel();
            auto n = mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseRel() {
        auto l = parseShift();
        while (is(T_LT) || is(T_GT) || is(T_LE) || is(T_GE)) {
            string op = is(T_LT) ? "LT" : is(T_GT) ? "GT" : is(T_LE) ? "LE" : "GE"; p++;
            auto r = parseShift();
            auto n = mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseShift() {
        auto l = parseAdd();
        while (is(T_SHL) || is(T_SHR)) {
            string op = is(T_SHL) ? "SHL" : "SHR"; p++;
            auto r = parseAdd();
            auto n = mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseAdd() {
        auto l = parseMul();
        while (is(T_PLUS) || is(T_MINUS)) {
            string op = is(T_PLUS) ? "ADD" : "SUB"; p++;
            auto r = parseMul();
            auto n = mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseMul() {
        auto l = parseUnary();
        while (is(T_STAR) || is(T_SLASH) || is(T_PERCENT)) {
            string op = is(T_STAR) ? "MUL" : is(T_SLASH) ? "DIV" : "MOD"; p++;
            auto r = parseUnary();
            auto n = mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    NP parseUnary() {
        int ln = cur().line, cl = cur().col;
        if (accept(T_PLUS))  return parseUnary();
        if (accept(T_MINUS)) { auto e = parseUnary(); auto n = mk("NEG", ln, cl);   n->ch.push_back(move(e)); return n; }
        if (accept(T_NOT))   { auto e = parseUnary(); auto n = mk("LNOT", ln, cl);  n->ch.push_back(move(e)); return n; }
        if (accept(T_TILDE)) { auto e = parseUnary(); auto n = mk("BNOT", ln, cl);  n->ch.push_back(move(e)); return n; }
        if (accept(T_AMP))   { auto e = parseUnary(); auto n = mk("ADDR", ln, cl);  n->ch.push_back(move(e)); return n; }
        if (accept(T_STAR))  { auto e = parseUnary(); auto n = mk("DEREF", ln, cl); n->ch.push_back(move(e)); return n; }
        if (accept(T_INC))   { auto e = parseUnary(); auto n = mk("PREINC", ln, cl);n->ch.push_back(move(e)); return n; }
        if (accept(T_DEC))   { auto e = parseUnary(); auto n = mk("PREDEC", ln, cl);n->ch.push_back(move(e)); return n; }
        if (accept(T_SIZEOF)) {
            auto n = mk("SIZEOF", ln, cl);
            if (accept(T_LP)) {
                if (isTypeStart()) {
                    TP t = parseTypeBase();
                    if (accept(T_LSB)) {
                        Token& sz = eat(T_NUM, "array size");
                        eat(T_RSB, "]");
                        t = arrayOf(t, (int)sz.v);
                    }
                    eat(T_RP, ")");
                    n->type = t;
                } else {
                    auto e = parseExpr();
                    eat(T_RP, ")");
                    n->ch.push_back(move(e));
                }
            } else {
                auto e = parseUnary();
                n->ch.push_back(move(e));
            }
            return n;
        }
        return parsePostfix();
    }
    NP parsePostfix() {
        auto e = parsePrimary();
        while (true) {
            int ln = cur().line, cl = cur().col;
            if (accept(T_LSB)) {
                auto idx = parseExpr();
                eat(T_RSB, "]");
                auto n = mk("INDEX", ln, cl);
                n->ch.push_back(move(e));
                n->ch.push_back(move(idx));
                e = move(n);
            } else if (accept(T_INC)) {
                auto n = mk("POSTINC", ln, cl); n->ch.push_back(move(e)); e = move(n);
            } else if (accept(T_DEC)) {
                auto n = mk("POSTDEC", ln, cl); n->ch.push_back(move(e)); e = move(n);
            } else break;
        }
        return e;
    }
    NP parsePrimary() {
        int ln = cur().line, cl = cur().col;
        if (is(T_NUM))    { auto n = mk("NUM",  ln, cl); n->num = toks[p++].v; return n; }
        if (is(T_STRING)) { auto n = mk("STR",  ln, cl); n->s   = toks[p++].s; return n; }
        if (accept(T_LP)) { auto e = parseExpr(); eat(T_RP, ")"); return e; }
        if (is(T_IDENT)) {
            auto id = toks[p++];
            if (accept(T_LP)) {
                auto n = mk("CALL", id.line, id.col); n->s = id.s;
                if (!is(T_RP)) {
                    n->ch.push_back(parseAssign());
                    while (accept(T_COMMA)) n->ch.push_back(parseAssign());
                }
                eat(T_RP, ")");
                return n;
            }
            auto n = mk("VAR", id.line, id.col); n->s = id.s;
            return n;
        }
        fatalAt(ln, cl, "unexpected token in expression");
    }
};

// ===========================================================================
// Codegen (x86_64, AT&T, Win64 ABI)
// ===========================================================================
struct Var {
    TP type;
    int offset;     // rbp-relative, for locals
    bool isGlobal;  // true: reference via rip
    string gname;   // for globals, the asm label
};

struct StringLit { string content; int id; };

struct Codegen {
    ostringstream o;
    unordered_map<string, Var> globals;
    unordered_map<string, TP> funcReturns;  // known local functions -> return type
    vector<unordered_map<string, Var>> scopes;
    vector<StringLit> strings;
    unordered_map<string, int> stringPool;
    int labelN = 0;
    int stringN = 0;

    // per-function state
    string curFunc;
    TP curRet;
    int curLocalBytes = 0;   // running, used during layout
    int maxLocalBytes = 0;
    int spillSlots = 0;      // number of 8-byte spill slots needed
    int maxCallArgs = 0;     // maximum argc of any nested call
    int curSpillUsed = 0;
    int frameSize = 0;
    int callAreaBytes = 0;
    int spillBase = 0;       // offset (negative from rbp) of spill slot 0
    string epilogueLabel;
    vector<pair<string, string>> loopStack; // (break, continue)
    bool debug = false;
    int lastLine = 0;

    string newLabel() { return ".L" + to_string(labelN++); }
    int addString(const string& s) {
        auto it = stringPool.find(s);
        if (it != stringPool.end()) return it->second;
        int id = stringN++;
        strings.push_back({s, id});
        stringPool[s] = id;
        return id;
    }
    void enterScope() { scopes.push_back({}); }
    void exitScope() { scopes.pop_back(); }
    Var* lookup(const string& name) {
        for (int i = (int)scopes.size() - 1; i >= 0; i--) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) return &it->second;
        }
        auto g = globals.find(name);
        if (g != globals.end()) return &g->second;
        return nullptr;
    }

    void emitLoc(int line) {
        if (debug && line > 0 && line != lastLine) {
            o << "\t.loc 1 " << line << " 0\n";
            lastLine = line;
        }
    }

    // ------------------------------------------------------------------
    // Layout pass: assign stack offsets to each DECL/PARAM node (stored in
    // node->num), and compute maxLocalBytes, spillSlots, maxCallArgs.
    // ------------------------------------------------------------------
    int layoutCur = 0;
    int layoutMax = 0;

    void layoutBlock(Node* n) {
        int save = layoutCur;
        for (auto& c : n->ch) layoutNode(c.get());
        layoutCur = save;
    }

    void layoutNode(Node* n) {
        if (!n) return;
        if (n->kind == "BLOCK") { layoutBlock(n); return; }
        if (n->kind == "DECL") {
            int sz = sizeOf(n->type);
            if (sz < 8) sz = 8;
            sz = (sz + 7) & ~7;
            layoutCur += sz;
            if (layoutCur > layoutMax) layoutMax = layoutCur;
            n->num = -layoutCur;
            if (!n->ch.empty()) layoutNode(n->ch[0].get());
            return;
        }
        if (n->kind == "FOR") {
            int save = layoutCur;
            for (auto& c : n->ch) layoutNode(c.get());
            if (layoutCur > layoutMax) layoutMax = layoutCur;
            layoutCur = save;
            return;
        }
        if (n->kind == "CALL") {
            int nargs = (int)n->ch.size();
            if (nargs > maxCallArgs) maxCallArgs = nargs;
            int depth = exprSpillNeed(n);
            if (depth > spillSlots) spillSlots = depth;
            for (auto& c : n->ch) layoutNode(c.get());
            return;
        }
        // expression statement: compute its spill need
        if (n->kind == "EXPRSTMT" || n->kind == "IF" || n->kind == "WHILE" ||
            n->kind == "DOWHILE" || n->kind == "RET") {
            for (auto& c : n->ch) {
                int d = exprSpillNeed(c.get());
                if (d > spillSlots) spillSlots = d;
                layoutNode(c.get());
            }
            return;
        }
        for (auto& c : n->ch) layoutNode(c.get());
    }

    int exprSpillNeed(Node* n) {
        if (!n) return 0;
        const string& k = n->kind;
        if (k == "NUM" || k == "STR" || k == "VAR" || k == "SIZEOF") return 0;
        if (k == "NEG" || k == "LNOT" || k == "BNOT" || k == "ADDR" || k == "DEREF" ||
            k == "PREINC" || k == "PREDEC" || k == "POSTINC" || k == "POSTDEC")
            return exprSpillNeed(n->ch[0].get());
        if (k == "ADD" || k == "SUB" || k == "MUL" || k == "DIV" || k == "MOD" ||
            k == "BAND" || k == "BOR" || k == "BXOR" || k == "SHL" || k == "SHR" ||
            k == "EQ" || k == "NE" || k == "LT" || k == "GT" || k == "LE" || k == "GE" ||
            k == "INDEX" || k == "ASSIGN" || k == "COMPASSIGN" || k == "LAND" || k == "LOR") {
            int l = exprSpillNeed(n->ch[0].get());
            int r = exprSpillNeed(n->ch[1].get());
            return max(l, 1 + r);
        }
        if (k == "TERN") {
            return max({exprSpillNeed(n->ch[0].get()),
                        exprSpillNeed(n->ch[1].get()),
                        exprSpillNeed(n->ch[2].get())});
        }
        if (k == "CALL") {
            int peak = (int)n->ch.size();
            for (int i = 0; i < (int)n->ch.size(); i++) {
                int need = exprSpillNeed(n->ch[i].get());
                int p = i + need;
                if (p > peak) peak = p;
            }
            return peak;
        }
        if (k == "EXPRSTMT") return exprSpillNeed(n->ch[0].get());
        return 0;
    }

    // ------------------------------------------------------------------
    // Codegen
    // ------------------------------------------------------------------
    string spillAddr(int idx) {
        int off = spillBase - idx * 8;
        return to_string(off) + "(%rbp)";
    }

    void pushRax() {
        o << "\tmovq %rax, " << spillAddr(curSpillUsed) << "\n";
        curSpillUsed++;
    }
    void popRcx() {
        curSpillUsed--;
        o << "\tmovq " << spillAddr(curSpillUsed) << ", %rcx\n";
    }

    // Load the value at (%rax) according to type t, leaving result in %rax.
    void loadFrom(TP t) {
        if (t->kind == Type::ARRAY) return; // array-as-lvalue loads address (no-op)
        if (t->kind == Type::CHAR)  { o << "\tmovsbq (%rax), %rax\n"; return; }
        o << "\tmovq (%rax), %rax\n";
    }
    // Store %rax into address in %rcx according to type t.
    void storeTo(TP t) {
        if (t->kind == Type::CHAR) { o << "\tmovb %al, (%rcx)\n"; return; }
        o << "\tmovq %rax, (%rcx)\n";
    }

    // Compute the address of an lvalue expression into %rax.
    // Returns the (undecayed) type of the lvalue.
    TP genAddr(Node* n) {
        if (n->kind == "VAR") {
            Var* v = lookup(n->s);
            if (!v) { errorAt(n->line, n->col, "undefined variable '" + n->s + "'"); return tInt(); }
            if (v->isGlobal) {
                o << "\tleaq " << v->gname << "(%rip), %rax\n";
            } else {
                o << "\tleaq " << v->offset << "(%rbp), %rax\n";
            }
            return v->type;
        }
        if (n->kind == "DEREF") {
            TP pt = genExpr(n->ch[0].get()); // value = address
            if (!isPointerOrArray(pt)) {
                errorAt(n->line, n->col, "dereferencing non-pointer type " + typeStr(pt));
                return tInt();
            }
            return pt->base;
        }
        if (n->kind == "INDEX") {
            // a[i] => *(a + i)
            TP lt = genExpr(n->ch[0].get()); // address or pointer value
            pushRax();
            TP rt = genExpr(n->ch[1].get());
            popRcx();
            // rcx = base, rax = index
            TP base;
            if (lt->kind == Type::PTR || lt->kind == Type::ARRAY) base = lt->base;
            else { errorAt(n->line, n->col, "subscript on non-pointer type " + typeStr(lt)); return tInt(); }
            int sz = sizeOf(base);
            if (sz != 1) o << "\timulq $" << sz << ", %rax\n";
            o << "\taddq %rcx, %rax\n";
            return base;
        }
        errorAt(n->line, n->col, "not an lvalue");
        return tInt();
    }

    // Determine the (undecayed) type of an expression without emitting code.
    // Used by sizeof so that sizeof(arr) gives the array size, not pointer size.
    TP typeOfExpr(Node* n) {
        if (!n) return tInt();
        const string& k = n->kind;
        if (k == "NUM") return tInt();
        if (k == "STR") return ptrTo(tChar());
        if (k == "VAR") {
            Var* v = lookup(n->s);
            if (!v) return tInt();
            return v->type;
        }
        if (k == "DEREF") {
            TP pt = typeOfExpr(n->ch[0].get());
            if (isPointerOrArray(pt)) return pt->base;
            return tInt();
        }
        if (k == "INDEX") {
            TP at = typeOfExpr(n->ch[0].get());
            if (isPointerOrArray(at)) return at->base;
            return tInt();
        }
        if (k == "ADDR") return ptrTo(typeOfExpr(n->ch[0].get()));
        if (k == "ASSIGN" || k == "COMPASSIGN") return typeOfExpr(n->ch[0].get());
        if (k == "CALL") {
            auto it = funcReturns.find(n->s);
            if (it != funcReturns.end()) return it->second;
            return tInt();
        }
        if (k == "ADD" || k == "SUB") {
            TP a = typeOfExpr(n->ch[0].get());
            TP b = typeOfExpr(n->ch[1].get());
            if (isPointerOrArray(a)) return decayType(a);
            if (isPointerOrArray(b)) return decayType(b);
            return tInt();
        }
        if (k == "TERN") return typeOfExpr(n->ch[1].get());
        return tInt();
    }

    // Emit code that leaves the *value* of the expression in %rax.
    // Returns the (decayed) type.
    TP genExpr(Node* n) {
        if (!n) return tInt();
        const string& k = n->kind;

        if (k == "NUM") {
            o << "\tmovq $" << n->num << ", %rax\n";
            return tInt();
        }
        if (k == "STR") {
            int id = addString(n->s);
            o << "\tleaq .LCS" << id << "(%rip), %rax\n";
            return ptrTo(tChar());
        }
        if (k == "VAR") {
            Var* v = lookup(n->s);
            if (!v) {
                errorAt(n->line, n->col, "undefined variable '" + n->s + "'");
                o << "\tmovq $0, %rax\n";
                return tInt();
            }
            // arrays: load address, don't deref
            if (v->type->kind == Type::ARRAY) {
                if (v->isGlobal) o << "\tleaq " << v->gname << "(%rip), %rax\n";
                else o << "\tleaq " << v->offset << "(%rbp), %rax\n";
                return ptrTo(v->type->base);
            }
            if (v->isGlobal) {
                if (v->type->kind == Type::CHAR)
                    o << "\tmovsbq " << v->gname << "(%rip), %rax\n";
                else
                    o << "\tmovq " << v->gname << "(%rip), %rax\n";
            } else {
                if (v->type->kind == Type::CHAR)
                    o << "\tmovsbq " << v->offset << "(%rbp), %rax\n";
                else
                    o << "\tmovq " << v->offset << "(%rbp), %rax\n";
            }
            return v->type;
        }
        if (k == "SIZEOF") {
            long long sz;
            if (n->type) sz = sizeOf(n->type);
            else sz = sizeOf(typeOfExpr(n->ch[0].get()));
            o << "\tmovq $" << sz << ", %rax\n";
            return tInt();
        }
        if (k == "ADDR") {
            TP t = genAddr(n->ch[0].get());
            return ptrTo(t);
        }
        if (k == "DEREF") {
            TP pt = genExpr(n->ch[0].get());
            if (!isPointerOrArray(pt)) {
                errorAt(n->line, n->col, "cannot dereference " + typeStr(pt));
                return tInt();
            }
            TP bt = pt->base;
            if (bt->kind == Type::ARRAY) return ptrTo(bt->base); // dereferencing ptr to array => ptr to element
            loadFrom(bt);
            return bt;
        }
        if (k == "INDEX") {
            TP bt = genAddr(n); // address of a[i]
            if (bt->kind == Type::ARRAY) return ptrTo(bt->base);
            loadFrom(bt);
            return bt;
        }
        if (k == "NEG") {
            genExpr(n->ch[0].get());
            o << "\tnegq %rax\n";
            return tInt();
        }
        if (k == "BNOT") {
            genExpr(n->ch[0].get());
            o << "\tnotq %rax\n";
            return tInt();
        }
        if (k == "LNOT") {
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tsete %al\n\tmovzbq %al, %rax\n";
            return tInt();
        }
        if (k == "PREINC" || k == "PREDEC" || k == "POSTINC" || k == "POSTDEC") {
            bool pre  = (k == "PREINC" || k == "PREDEC");
            bool inc  = (k == "PREINC" || k == "POSTINC");
            TP t = genAddr(n->ch[0].get());
            o << "\tmovq %rax, %rcx\n"; // rcx = address
            // load old value
            if (t->kind == Type::CHAR) o << "\tmovsbq (%rcx), %rax\n";
            else o << "\tmovq (%rcx), %rax\n";
            // determine delta: pointer arith scales
            int delta = 1;
            if (t->kind == Type::PTR) delta = sizeOf(t->base);
            if (pre) {
                if (inc) o << "\taddq $" << delta << ", %rax\n";
                else o << "\tsubq $" << delta << ", %rax\n";
                if (t->kind == Type::CHAR) o << "\tmovb %al, (%rcx)\n";
                else o << "\tmovq %rax, (%rcx)\n";
            } else {
                // save old value, modify in memory
                o << "\tmovq %rax, %rdx\n"; // rdx = old
                if (inc) o << "\taddq $" << delta << ", %rdx\n";
                else o << "\tsubq $" << delta << ", %rdx\n";
                if (t->kind == Type::CHAR) o << "\tmovb %dl, (%rcx)\n";
                else o << "\tmovq %rdx, (%rcx)\n";
                // rax still has old value
            }
            return t;
        }
        if (k == "ASSIGN") {
            TP lt = genAddr(n->ch[0].get());
            pushRax();
            TP rt = genExpr(n->ch[1].get());
            popRcx();
            storeTo(lt);
            return lt;
        }
        if (k == "COMPASSIGN") {
            // a op= b  ==>  tmp = &a; *tmp = *tmp op b
            TP lt = genAddr(n->ch[0].get());
            pushRax();                       // save &a
            // load current value
            o << "\tmovq " << spillAddr(curSpillUsed - 1) << ", %rcx\n";
            if (lt->kind == Type::CHAR) o << "\tmovsbq (%rcx), %rax\n";
            else o << "\tmovq (%rcx), %rax\n";
            pushRax();                       // save old value
            TP rt = genExpr(n->ch[1].get()); // rhs -> rax
            popRcx();                        // rcx = old value
            // now perform op: rax = rcx op rax   (old op rhs)
            o << "\txchgq %rax, %rcx\n";     // rax = old, rcx = rhs
            const string& op = n->s;
            if (op == "ADD") {
                if (lt->kind == Type::PTR) o << "\timulq $" << sizeOf(lt->base) << ", %rcx\n";
                o << "\taddq %rcx, %rax\n";
            } else if (op == "SUB") {
                if (lt->kind == Type::PTR) o << "\timulq $" << sizeOf(lt->base) << ", %rcx\n";
                o << "\tsubq %rcx, %rax\n";
            } else if (op == "MUL") {
                o << "\timulq %rcx, %rax\n";
            } else if (op == "DIV") {
                o << "\tcqto\n\tidivq %rcx\n";
            } else if (op == "MOD") {
                o << "\tcqto\n\tidivq %rcx\n\tmovq %rdx, %rax\n";
            }
            // now rax has new value, recover &a
            popRcx();                        // rcx = &a
            storeTo(lt);
            return lt;
        }
        if (k == "LAND") {
            string Lfalse = newLabel();
            string Lend = newLabel();
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tjz " << Lfalse << "\n";
            genExpr(n->ch[1].get());
            o << "\ttestq %rax, %rax\n\tjz " << Lfalse << "\n";
            o << "\tmovq $1, %rax\n\tjmp " << Lend << "\n";
            o << Lfalse << ":\n\tmovq $0, %rax\n" << Lend << ":\n";
            return tInt();
        }
        if (k == "LOR") {
            string Ltrue = newLabel();
            string Lend = newLabel();
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tjnz " << Ltrue << "\n";
            genExpr(n->ch[1].get());
            o << "\ttestq %rax, %rax\n\tjnz " << Ltrue << "\n";
            o << "\tmovq $0, %rax\n\tjmp " << Lend << "\n";
            o << Ltrue << ":\n\tmovq $1, %rax\n" << Lend << ":\n";
            return tInt();
        }
        if (k == "TERN") {
            string Lelse = newLabel(), Lend = newLabel();
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tjz " << Lelse << "\n";
            TP t1 = genExpr(n->ch[1].get());
            o << "\tjmp " << Lend << "\n" << Lelse << ":\n";
            TP t2 = genExpr(n->ch[2].get());
            o << Lend << ":\n";
            return t1; // assume matching types
        }
        if (k == "CALL") {
            return genCall(n);
        }
        // Binary ops
        if (k == "ADD" || k == "SUB" || k == "MUL" || k == "DIV" || k == "MOD" ||
            k == "BAND" || k == "BOR" || k == "BXOR" || k == "SHL" || k == "SHR" ||
            k == "EQ" || k == "NE" || k == "LT" || k == "GT" || k == "LE" || k == "GE") {
            TP lt = genExpr(n->ch[0].get());
            pushRax();
            TP rt = genExpr(n->ch[1].get());
            popRcx();
            // rcx = left, rax = right
            // Pointer arithmetic scaling for ADD/SUB
            TP resultType = tInt();
            if (k == "ADD") {
                if (isPointerOrArray(lt) && isInteger(rt)) {
                    int sz = sizeOf(lt->base);
                    if (sz != 1) o << "\timulq $" << sz << ", %rax\n";
                    o << "\taddq %rcx, %rax\n";
                    resultType = decayType(lt);
                } else if (isInteger(lt) && isPointerOrArray(rt)) {
                    int sz = sizeOf(rt->base);
                    if (sz != 1) o << "\timulq $" << sz << ", %rcx\n";
                    o << "\taddq %rcx, %rax\n";
                    resultType = decayType(rt);
                } else {
                    o << "\taddq %rcx, %rax\n";
                }
                return resultType;
            }
            if (k == "SUB") {
                if (isPointerOrArray(lt) && isInteger(rt)) {
                    int sz = sizeOf(lt->base);
                    if (sz != 1) o << "\timulq $" << sz << ", %rax\n";
                    o << "\tmovq %rcx, %rdx\n\tsubq %rax, %rdx\n\tmovq %rdx, %rax\n";
                    resultType = decayType(lt);
                } else if (isPointerOrArray(lt) && isPointerOrArray(rt)) {
                    int sz = sizeOf(lt->base);
                    o << "\tmovq %rcx, %rdx\n\tsubq %rax, %rdx\n\tmovq %rdx, %rax\n";
                    if (sz != 1) o << "\tmovq $" << sz << ", %rcx\n\tcqto\n\tidivq %rcx\n";
                } else {
                    o << "\tmovq %rcx, %rdx\n\tsubq %rax, %rdx\n\tmovq %rdx, %rax\n";
                }
                return resultType;
            }
            // For the rest, operands are integers; rcx = left, rax = right
            if (k == "MUL") { o << "\timulq %rcx, %rax\n"; return tInt(); }
            if (k == "DIV") {
                // quotient = left / right => rax = rcx / rax
                o << "\tmovq %rax, %r8\n\tmovq %rcx, %rax\n\tcqto\n\tidivq %r8\n";
                return tInt();
            }
            if (k == "MOD") {
                o << "\tmovq %rax, %r8\n\tmovq %rcx, %rax\n\tcqto\n\tidivq %r8\n\tmovq %rdx, %rax\n";
                return tInt();
            }
            if (k == "BAND") { o << "\tandq %rcx, %rax\n"; return tInt(); }
            if (k == "BOR")  { o << "\torq %rcx, %rax\n"; return tInt(); }
            if (k == "BXOR") { o << "\txorq %rcx, %rax\n"; return tInt(); }
            if (k == "SHL") {
                // left << right -> rcx << rax; shift uses cl
                o << "\tmovq %rax, %r8\n\tmovq %rcx, %rax\n\tmovq %r8, %rcx\n\tshlq %cl, %rax\n";
                return tInt();
            }
            if (k == "SHR") {
                o << "\tmovq %rax, %r8\n\tmovq %rcx, %rax\n\tmovq %r8, %rcx\n\tsarq %cl, %rax\n";
                return tInt();
            }
            // Comparisons: rcx op rax
            o << "\tcmpq %rax, %rcx\n";
            string set = k == "EQ" ? "sete"
                       : k == "NE" ? "setne"
                       : k == "LT" ? "setl"
                       : k == "GT" ? "setg"
                       : k == "LE" ? "setle" : "setge";
            o << "\t" << set << " %al\n\tmovzbq %al, %rax\n";
            return tInt();
        }
        errorAt(n->line, n->col, "bad expression kind: " + k);
        return tInt();
    }

    // Like genExpr but for the SIZEOF case where we want the type only.
    // We still emit code but callers swap to a throwaway stream.
    TP genExprTypeOnly(Node* n) {
        return genExpr(n);
    }

    TP genCall(Node* n) {
        int nargs = (int)n->ch.size();
        // Evaluate args left-to-right, stashing each in a fresh spill slot.
        // After all args are evaluated, copy spill slots out to the call area
        // so nested calls can't clobber our partially-built call frame.
        int startSpill = curSpillUsed;
        for (int i = 0; i < nargs; i++) {
            genExpr(n->ch[i].get());
            pushRax();
        }
        const char* regs4[4] = {"%rcx", "%rdx", "%r8", "%r9"};
        for (int i = 0; i < nargs; i++) {
            o << "\tmovq " << spillAddr(startSpill + i) << ", %rax\n";
            o << "\tmovq %rax, " << (i * 8) << "(%rsp)\n";
        }
        for (int i = 0; i < min(4, nargs); i++) {
            o << "\tmovq " << (i * 8) << "(%rsp), " << regs4[i] << "\n";
        }
        curSpillUsed = startSpill;
        o << "\tcall " << n->s << "\n";
        auto it = funcReturns.find(n->s);
        if (it != funcReturns.end()) return it->second;
        return tInt(); // default for externals
    }

    void genStmt(Node* n) {
        if (!n) return;
        if (n->kind != "BLOCK") emitLoc(n->line);
        const string& k = n->kind;
        if (k == "BLOCK") {
            enterScope();
            for (auto& c : n->ch) genStmt(c.get());
            exitScope();
            return;
        }
        if (k == "EMPTY") return;
        if (k == "DECL") {
            Var v; v.type = n->type; v.offset = (int)n->num; v.isGlobal = false;
            scopes.back()[n->s] = v;
            if (!n->ch.empty()) {
                curSpillUsed = 0;
                TP rt = genExpr(n->ch[0].get());
                o << "\tleaq " << v.offset << "(%rbp), %rcx\n";
                storeTo(v.type);
            }
            return;
        }
        if (k == "EXPRSTMT") {
            curSpillUsed = 0;
            genExpr(n->ch[0].get());
            return;
        }
        if (k == "IF") {
            string Lelse = newLabel(), Lend = newLabel();
            curSpillUsed = 0;
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tjz " << Lelse << "\n";
            genStmt(n->ch[1].get());
            o << "\tjmp " << Lend << "\n" << Lelse << ":\n";
            if (n->ch.size() == 3) genStmt(n->ch[2].get());
            o << Lend << ":\n";
            return;
        }
        if (k == "WHILE") {
            string Ltop = newLabel(), Lend = newLabel();
            loopStack.push_back({Lend, Ltop});
            o << Ltop << ":\n";
            curSpillUsed = 0;
            genExpr(n->ch[0].get());
            o << "\ttestq %rax, %rax\n\tjz " << Lend << "\n";
            genStmt(n->ch[1].get());
            o << "\tjmp " << Ltop << "\n" << Lend << ":\n";
            loopStack.pop_back();
            return;
        }
        if (k == "DOWHILE") {
            string Ltop = newLabel(), Lcont = newLabel(), Lend = newLabel();
            loopStack.push_back({Lend, Lcont});
            o << Ltop << ":\n";
            genStmt(n->ch[0].get());
            o << Lcont << ":\n";
            curSpillUsed = 0;
            genExpr(n->ch[1].get());
            o << "\ttestq %rax, %rax\n\tjnz " << Ltop << "\n" << Lend << ":\n";
            loopStack.pop_back();
            return;
        }
        if (k == "FOR") {
            string Ltop = newLabel(), Lcont = newLabel(), Lend = newLabel();
            enterScope();
            genStmt(n->ch[0].get()); // init
            o << Ltop << ":\n";
            if (n->ch[1]->kind != "EMPTY") {
                curSpillUsed = 0;
                genExpr(n->ch[1].get());
                o << "\ttestq %rax, %rax\n\tjz " << Lend << "\n";
            }
            loopStack.push_back({Lend, Lcont});
            genStmt(n->ch[3].get());
            loopStack.pop_back();
            o << Lcont << ":\n";
            if (n->ch[2]->kind != "EMPTY") {
                curSpillUsed = 0;
                genExpr(n->ch[2].get());
            }
            o << "\tjmp " << Ltop << "\n" << Lend << ":\n";
            exitScope();
            return;
        }
        if (k == "BREAK") {
            if (loopStack.empty()) { errorAt(n->line, n->col, "break outside loop"); return; }
            o << "\tjmp " << loopStack.back().first << "\n";
            return;
        }
        if (k == "CONTINUE") {
            if (loopStack.empty()) { errorAt(n->line, n->col, "continue outside loop"); return; }
            o << "\tjmp " << loopStack.back().second << "\n";
            return;
        }
        if (k == "RET") {
            if (!n->ch.empty()) {
                curSpillUsed = 0;
                genExpr(n->ch[0].get());
            }
            o << "\tjmp " << epilogueLabel << "\n";
            return;
        }
        errorAt(n->line, n->col, "unhandled statement kind: " + k);
    }

    void genFunction(Node* fn) {
        curFunc = fn->s;
        curRet = fn->type;

        // Layout pass
        layoutCur = 0;
        layoutMax = 0;
        spillSlots = 0;
        maxCallArgs = 0;

        Node* params = fn->ch[0].get();
        int np = (int)params->ch.size();
        // Assign param offsets as if they were locals (we spill them here).
        for (int i = 0; i < np; i++) {
            layoutCur += 8;
            params->ch[i]->num = -layoutCur;
        }
        if (layoutCur > layoutMax) layoutMax = layoutCur;

        Node* body = fn->ch[1].get();
        layoutNode(body);

        maxLocalBytes = layoutMax;

        // Compute frame
        int locals = (maxLocalBytes + 7) & ~7;
        int spillBytes = spillSlots * 8;
        int callStackArgs = max(0, maxCallArgs - 4);
        callAreaBytes = 32 + callStackArgs * 8;
        if (maxCallArgs == 0) callAreaBytes = 0;
        callAreaBytes = (callAreaBytes + 15) & ~15;
        int total = locals + spillBytes + callAreaBytes;
        // Must keep rsp 16-aligned; after pushq rbp rsp is 16-aligned, so total % 16 must be 0.
        total = (total + 15) & ~15;
        frameSize = total;
        // spillBase = rbp - locals -> first spill slot at rbp - locals - 8
        spillBase = -(locals + 8);

        // Emit
        o << "\t.text\n\t.globl " << fn->s << "\n" << fn->s << ":\n";
        o << "\tpushq %rbp\n\tmovq %rsp, %rbp\n";
        if (frameSize > 0) o << "\tsubq $" << frameSize << ", %rsp\n";

        // Spill params to their slots
        const char* regs4[4] = {"%rcx", "%rdx", "%r8", "%r9"};
        scopes.clear();
        enterScope();
        for (int i = 0; i < np; i++) {
            Node* pn = params->ch[i].get();
            int off = (int)pn->num;
            if (i < 4) {
                if (pn->type->kind == Type::CHAR) {
                    o << "\tmovq " << regs4[i] << ", %rax\n";
                    o << "\tmovb %al, " << off << "(%rbp)\n";
                } else {
                    o << "\tmovq " << regs4[i] << ", " << off << "(%rbp)\n";
                }
            } else {
                int srcOff = 16 + 8 * i;
                o << "\tmovq " << srcOff << "(%rbp), %rax\n";
                if (pn->type->kind == Type::CHAR)
                    o << "\tmovb %al, " << off << "(%rbp)\n";
                else
                    o << "\tmovq %rax, " << off << "(%rbp)\n";
            }
            Var v; v.type = pn->type; v.offset = off; v.isGlobal = false;
            scopes.back()[pn->s] = v;
        }

        epilogueLabel = newLabel();
        genStmt(body);
        exitScope();

        // Default zero return
        o << "\tmovq $0, %rax\n" << epilogueLabel << ":\n";
        o << "\tmovq %rbp, %rsp\n\tpopq %rbp\n\tret\n";
    }

    void genProgram(Node* prog) {
        // First pass: collect function return types and declare globals.
        for (auto& top : prog->ch) {
            if (top->kind == "FUNC" || top->kind == "PROTO") {
                funcReturns[top->s] = top->type;
            } else if (top->kind == "GLOBAL") {
                Var v;
                v.type = top->type;
                v.offset = 0;
                v.isGlobal = true;
                v.gname = top->s;
                globals[top->s] = v;
            }
        }
        // Emit globals
        bool anyData = false;
        for (auto& top : prog->ch) {
            if (top->kind != "GLOBAL") continue;
            if (!top->ch.empty()) {
                // initialized
                if (!anyData) { o << "\t.data\n"; anyData = true; }
                o << "\t.globl " << top->s << "\n" << top->s << ":\n";
                Node* init = top->ch[0].get();
                if (init->kind == "INIT_NUM") {
                    if (top->type->kind == Type::CHAR) o << "\t.byte " << init->num << "\n";
                    else o << "\t.quad " << init->num << "\n";
                } else if (init->kind == "INIT_STR") {
                    // string: store as the raw bytes (for char[])
                    o << "\t.ascii \"";
                    for (unsigned char c : init->s) {
                        if (c == '\\') o << "\\\\";
                        else if (c == '"') o << "\\\"";
                        else if (c >= 32 && c < 127) o << (char)c;
                        else { char buf[8]; snprintf(buf, sizeof(buf), "\\%03o", c); o << buf; }
                    }
                    o << "\\0\"\n";
                }
            }
        }
        // Emit uninitialized globals in .bss
        bool anyBss = false;
        for (auto& top : prog->ch) {
            if (top->kind != "GLOBAL") continue;
            if (!top->ch.empty()) continue;
            if (!anyBss) { o << "\t.bss\n"; anyBss = true; }
            o << "\t.globl " << top->s << "\n" << top->s << ":\n";
            o << "\t.zero " << sizeOf(top->type) << "\n";
        }
        // Emit functions
        for (auto& top : prog->ch) {
            if (top->kind == "FUNC") genFunction(top.get());
        }
        // Emit string literals
        if (!strings.empty()) o << "\t.section .rdata,\"dr\"\n";
        for (auto& sl : strings) {
            o << ".LCS" << sl.id << ":\n\t.ascii \"";
            for (unsigned char c : sl.content) {
                if (c == '\\') o << "\\\\";
                else if (c == '"') o << "\\\"";
                else if (c >= 32 && c < 127) o << (char)c;
                else { char buf[8]; snprintf(buf, sizeof(buf), "\\%03o", c); o << buf; }
            }
            o << "\\0\"\n";
        }
    }
};

// ===========================================================================
// AST dumper
// ===========================================================================
static void dumpAst(Node* n, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", stderr);
    fprintf(stderr, "%s", n->kind.c_str());
    if (!n->s.empty()) fprintf(stderr, " '%s'", n->s.c_str());
    if (n->kind == "NUM") fprintf(stderr, " %lld", n->num);
    if (n->type) fprintf(stderr, " : %s", typeStr(n->type).c_str());
    if (n->line) fprintf(stderr, "  @%d:%d", n->line, n->col);
    fputc('\n', stderr);
    for (auto& c : n->ch) dumpAst(c.get(), depth + 1);
}

static const char* tokName(Tok t) {
    switch (t) {
        case T_EOF: return "EOF";
        case T_INT: return "INT"; case T_CHAR: return "CHAR"; case T_VOID: return "VOID";
        case T_IF: return "IF"; case T_ELSE: return "ELSE";
        case T_WHILE: return "WHILE"; case T_DO: return "DO"; case T_FOR: return "FOR";
        case T_RETURN: return "RETURN"; case T_BREAK: return "BREAK"; case T_CONTINUE: return "CONTINUE";
        case T_SIZEOF: return "SIZEOF";
        case T_IDENT: return "IDENT"; case T_NUM: return "NUM"; case T_STRING: return "STRING";
        case T_LP: return "("; case T_RP: return ")"; case T_LB: return "{"; case T_RB: return "}";
        case T_LSB: return "["; case T_RSB: return "]"; case T_SEMI: return ";"; case T_COMMA: return ",";
        case T_QMARK: return "?"; case T_COLON: return ":";
        case T_ASSIGN: return "="; case T_PLUSEQ: return "+="; case T_MINUSEQ: return "-=";
        case T_STAREQ: return "*="; case T_SLASHEQ: return "/="; case T_PERCEQ: return "%=";
        case T_PLUS: return "+"; case T_MINUS: return "-"; case T_STAR: return "*"; case T_SLASH: return "/";
        case T_PERCENT: return "%";
        case T_INC: return "++"; case T_DEC: return "--";
        case T_EQ: return "=="; case T_NE: return "!=";
        case T_LT: return "<"; case T_GT: return ">"; case T_LE: return "<="; case T_GE: return ">=";
        case T_ANDAND: return "&&"; case T_OROR: return "||"; case T_NOT: return "!";
        case T_AMP: return "&"; case T_PIPE: return "|"; case T_CARET: return "^"; case T_TILDE: return "~";
        case T_SHL: return "<<"; case T_SHR: return ">>";
        case T_ELLIPSIS: return "...";
    }
    return "?";
}

static string stripExt(const string& p) {
    size_t slash = p.find_last_of("/\\");
    size_t dot = p.find_last_of('.');
    if (dot == string::npos || (slash != string::npos && dot < slash)) return p;
    return p.substr(0, dot);
}

// ===========================================================================
// Driver
// ===========================================================================
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
            "mycc v2 - a small C compiler\n"
            "usage: %s input.c [options]\n"
            "  default: produces <input>.exe (requires gcc on PATH)\n"
            "  -o X       : output name (X.exe, or X.s with -S)\n"
            "  -S         : stop after codegen, write <input>.s\n"
            "  -g         : emit debug info so gdb can step by source line\n"
            "  --tokens   : dump token stream\n"
            "  --ast      : dump parsed AST\n"
            "  --keep-asm : keep intermediate .s alongside .exe\n"
            "  -v         : verbose\n",
            argv[0]);
        return 1;
    }
    string in, out;
    bool asmOnly = false, keepAsm = false, dumpToks = false, dumpTree = false, verbose = false, dbg = false;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-S") asmOnly = true;
        else if (a == "-g") dbg = true;
        else if (a == "--keep-asm") keepAsm = true;
        else if (a == "--tokens") dumpToks = true;
        else if (a == "--ast") dumpTree = true;
        else if (a == "-v") verbose = true;
        else if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a[0] == '-') fatal(string("unknown flag ") + a);
        else in = a;
    }
    if (in.empty()) fatal("no input file");
    ifstream f(in);
    if (!f) fatal("can't open " + in);
    stringstream ss; ss << f.rdbuf();
    initSource(in, ss.str());

    if (verbose) fprintf(stderr, "[mycc] lexing %s\n", in.c_str());
    Lexer L; L.src = g_text; L.run();
    if (dumpToks) {
        fprintf(stderr, "--- tokens ---\n");
        for (auto& t : L.out) {
            fprintf(stderr, "  %4d:%-3d %-10s", t.line, t.col, tokName(t.t));
            if (t.t == T_IDENT) fprintf(stderr, " %s", t.s.c_str());
            else if (t.t == T_NUM) fprintf(stderr, " %lld", t.v);
            else if (t.t == T_STRING) fprintf(stderr, " \"%s\"", t.s.c_str());
            fputc('\n', stderr);
        }
    }

    if (verbose) fprintf(stderr, "[mycc] parsing\n");
    Parser P(L.out);
    auto prog = P.parseProgram();
    if (dumpTree) { fprintf(stderr, "--- ast ---\n"); dumpAst(prog.get(), 0); }

    if (g_errors > 0) { fprintf(stderr, "mycc: %d error(s); aborting\n", g_errors); return 1; }

    if (verbose) fprintf(stderr, "[mycc] codegen\n");
    Codegen C;
    C.debug = dbg;
    if (dbg) C.o << "\t.file 1 \"" << in << "\"\n";
    C.genProgram(prog.get());

    if (g_errors > 0) { fprintf(stderr, "mycc: %d error(s); aborting\n", g_errors); return 1; }

    string base = out.empty() ? stripExt(in) : stripExt(out);
    string asmPath = base + ".s";
    { ofstream of(asmPath); of << C.o.str(); }
    if (asmOnly) {
        fprintf(stderr, "wrote %s\n", asmPath.c_str());
        return 0;
    }
    string exePath = base + ".exe";
    string cmd = string("gcc ") + (dbg ? "-g " : "") + "-static \"" + asmPath + "\" -o \"" + exePath + "\"";
    if (verbose) fprintf(stderr, "[mycc] %s\n", cmd.c_str());
    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "gcc failed (exit %d). Is MinGW gcc on PATH?\n", rc);
        return 1;
    }
    if (!keepAsm) remove(asmPath.c_str());
    fprintf(stderr, "wrote %s\n", exePath.c_str());
    return 0;
}
