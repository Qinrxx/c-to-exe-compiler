// Minimal C-subset -> x86_64 AT&T compiler (Win64 ABI / mingw-w64)
// Supports: int vars, + - * /, if/else, while, functions (int params/return), printf.
#include <bits/stdc++.h>
using namespace std;

enum Tok { T_INT, T_IF, T_ELSE, T_WHILE, T_RETURN, T_PRINTF,
           T_IDENT, T_NUM, T_STRING,
           T_LP, T_RP, T_LB, T_RB, T_SEMI, T_COMMA,
           T_ASSIGN, T_PLUS, T_MINUS, T_STAR, T_SLASH,
           T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE, T_EOF };

struct Token { Tok t; string s; long v; int line; };

struct Lexer {
    string src; size_t p=0; int line=1;
    vector<Token> out;
    void run() {
        while (p < src.size()) {
            char c = src[p];
            if (c=='\n') { line++; p++; continue; }
            if (isspace((unsigned char)c)) { p++; continue; }
            if (c=='/' && p+1<src.size() && src[p+1]=='/') { while (p<src.size()&&src[p]!='\n') p++; continue; }
            if (c=='/' && p+1<src.size() && src[p+1]=='*') { p+=2; while(p+1<src.size()&&!(src[p]=='*'&&src[p+1]=='/')){ if(src[p]=='\n')line++; p++; } p+=2; continue; }
            if (isalpha((unsigned char)c) || c=='_') {
                size_t s=p; while (p<src.size()&&(isalnum((unsigned char)src[p])||src[p]=='_')) p++;
                string id = src.substr(s,p-s);
                if (id=="int") out.push_back({T_INT,id,0,line});
                else if (id=="if") out.push_back({T_IF,id,0,line});
                else if (id=="else") out.push_back({T_ELSE,id,0,line});
                else if (id=="while") out.push_back({T_WHILE,id,0,line});
                else if (id=="return") out.push_back({T_RETURN,id,0,line});
                else if (id=="printf") out.push_back({T_PRINTF,id,0,line});
                else out.push_back({T_IDENT,id,0,line});
                continue;
            }
            if (isdigit((unsigned char)c)) {
                long v=0; while (p<src.size()&&isdigit((unsigned char)src[p])) { v=v*10+(src[p]-'0'); p++; }
                out.push_back({T_NUM,"",v,line}); continue;
            }
            if (c=='"') {
                p++; string s;
                while (p<src.size()&&src[p]!='"') {
                    if (src[p]=='\\' && p+1<src.size()) {
                        char n=src[p+1]; p+=2;
                        if (n=='n') s+='\n';
                        else if (n=='t') s+='\t';
                        else if (n=='\\') s+='\\';
                        else if (n=='"') s+='"';
                        else if (n=='0') s+='\0';
                        else s+=n;
                    } else { s+=src[p++]; }
                }
                p++;
                out.push_back({T_STRING,s,0,line}); continue;
            }
            auto two=[&](const char* m, Tok t){ if (p+1<src.size()&&src[p]==m[0]&&src[p+1]==m[1]){out.push_back({t,"",0,line});p+=2;return true;} return false;};
            if (two("==",T_EQ)) continue;
            if (two("!=",T_NE)) continue;
            if (two("<=",T_LE)) continue;
            if (two(">=",T_GE)) continue;
            switch (c) {
                case '(': out.push_back({T_LP,"",0,line}); p++; break;
                case ')': out.push_back({T_RP,"",0,line}); p++; break;
                case '{': out.push_back({T_LB,"",0,line}); p++; break;
                case '}': out.push_back({T_RB,"",0,line}); p++; break;
                case ';': out.push_back({T_SEMI,"",0,line}); p++; break;
                case ',': out.push_back({T_COMMA,"",0,line}); p++; break;
                case '=': out.push_back({T_ASSIGN,"",0,line}); p++; break;
                case '+': out.push_back({T_PLUS,"",0,line}); p++; break;
                case '-': out.push_back({T_MINUS,"",0,line}); p++; break;
                case '*': out.push_back({T_STAR,"",0,line}); p++; break;
                case '/': out.push_back({T_SLASH,"",0,line}); p++; break;
                case '<': out.push_back({T_LT,"",0,line}); p++; break;
                case '>': out.push_back({T_GT,"",0,line}); p++; break;
                default: fprintf(stderr,"lex error '%c' line %d\n",c,line); exit(1);
            }
        }
        out.push_back({T_EOF,"",0,line});
    }
};

struct Node {
    string kind;
    long num=0;
    string s;
    vector<unique_ptr<Node>> ch;
};
using NP = unique_ptr<Node>;
static NP mk(string k){ auto n=make_unique<Node>(); n->kind=k; return n; }

struct Parser {
    vector<Token>& toks; size_t p=0;
    Parser(vector<Token>&t):toks(t){}
    Token& peek(int o=0){ return toks[p+o]; }
    bool accept(Tok t){ if(peek().t==t){p++;return true;} return false; }
    Token& expect(Tok t, const char* what){
        if(peek().t!=t){ fprintf(stderr,"parse err line %d: expected %s, got tok %d\n",peek().line,what,(int)peek().t); exit(1);}
        return toks[p++];
    }
    NP parseProgram(){ auto n=mk("PROG"); while(peek().t!=T_EOF) n->ch.push_back(parseFunction()); return n; }
    NP parseFunction(){
        expect(T_INT,"int");
        auto id=expect(T_IDENT,"function name");
        auto fn=mk("FUNC"); fn->s=id.s;
        expect(T_LP,"(");
        auto params=mk("PARAMS");
        if(peek().t!=T_RP){
            do { expect(T_INT,"int"); auto pid=expect(T_IDENT,"param");
                 auto pn=mk("PARAM"); pn->s=pid.s; params->ch.push_back(move(pn));
            } while(accept(T_COMMA));
        }
        expect(T_RP,")");
        fn->ch.push_back(move(params));
        fn->ch.push_back(parseBlock());
        return fn;
    }
    NP parseBlock(){ expect(T_LB,"{"); auto b=mk("BLOCK"); while(peek().t!=T_RB) b->ch.push_back(parseStmt()); expect(T_RB,"}"); return b; }
    NP parseStmt(){
        if(peek().t==T_LB) return parseBlock();
        if(peek().t==T_INT){ p++; auto id=expect(T_IDENT,"var"); auto n=mk("DECL"); n->s=id.s;
            if(accept(T_ASSIGN)) n->ch.push_back(parseExpr());
            expect(T_SEMI,";"); return n; }
        if(peek().t==T_IF){ p++; expect(T_LP,"("); auto c=parseExpr(); expect(T_RP,")");
            auto th=parseStmt(); auto n=mk("IF"); n->ch.push_back(move(c)); n->ch.push_back(move(th));
            if(accept(T_ELSE)) n->ch.push_back(parseStmt()); return n; }
        if(peek().t==T_WHILE){ p++; expect(T_LP,"("); auto c=parseExpr(); expect(T_RP,")");
            auto b=parseStmt(); auto n=mk("WHILE"); n->ch.push_back(move(c)); n->ch.push_back(move(b)); return n; }
        if(peek().t==T_RETURN){ p++; auto e=parseExpr(); expect(T_SEMI,";");
            auto n=mk("RET"); n->ch.push_back(move(e)); return n; }
        if(peek().t==T_PRINTF){ p++; expect(T_LP,"(");
            auto tstr=expect(T_STRING,"string");
            auto n=mk("PRINTF"); n->s=tstr.s;
            while(accept(T_COMMA)) n->ch.push_back(parseExpr());
            expect(T_RP,")"); expect(T_SEMI,";"); return n; }
        if(peek().t==T_IDENT && peek(1).t==T_ASSIGN){
            auto id=toks[p++]; p++; auto e=parseExpr(); expect(T_SEMI,";");
            auto n=mk("ASSIGN"); n->s=id.s; n->ch.push_back(move(e)); return n; }
        auto e=parseExpr(); expect(T_SEMI,";"); auto n=mk("EXPRSTMT"); n->ch.push_back(move(e)); return n;
    }
    NP parseExpr(){ return parseEq(); }
    NP parseEq(){ auto l=parseRel();
        while(peek().t==T_EQ||peek().t==T_NE){ string op=(toks[p++].t==T_EQ)?"EQ":"NE";
            auto r=parseRel(); auto n=mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r)); l=move(n);} return l; }
    NP parseRel(){ auto l=parseAdd();
        while(peek().t==T_LT||peek().t==T_GT||peek().t==T_LE||peek().t==T_GE){
            Tok t=toks[p++].t; string op=t==T_LT?"LT":t==T_GT?"GT":t==T_LE?"LE":"GE";
            auto r=parseAdd(); auto n=mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r)); l=move(n);} return l; }
    NP parseAdd(){ auto l=parseMul();
        while(peek().t==T_PLUS||peek().t==T_MINUS){ string op=(toks[p++].t==T_PLUS)?"ADD":"SUB";
            auto r=parseMul(); auto n=mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r)); l=move(n);} return l; }
    NP parseMul(){ auto l=parseUnary();
        while(peek().t==T_STAR||peek().t==T_SLASH){ string op=(toks[p++].t==T_STAR)?"MUL":"DIV";
            auto r=parseUnary(); auto n=mk(op); n->ch.push_back(move(l)); n->ch.push_back(move(r)); l=move(n);} return l; }
    NP parseUnary(){ if(accept(T_MINUS)){ auto e=parseUnary(); auto n=mk("NEG"); n->ch.push_back(move(e)); return n; } return parsePrimary(); }
    NP parsePrimary(){
        if(peek().t==T_NUM){ auto n=mk("NUM"); n->num=toks[p++].v; return n; }
        if(peek().t==T_LP){ p++; auto e=parseExpr(); expect(T_RP,")"); return e; }
        if(peek().t==T_IDENT){
            auto id=toks[p++];
            if(accept(T_LP)){ auto n=mk("CALL"); n->s=id.s;
                if(peek().t!=T_RP){ n->ch.push_back(parseExpr()); while(accept(T_COMMA)) n->ch.push_back(parseExpr()); }
                expect(T_RP,")"); return n; }
            auto n=mk("VAR"); n->s=id.s; return n;
        }
        fprintf(stderr,"parse err line %d: unexpected token %d\n",peek().line,(int)peek().t); exit(1);
    }
};

struct Codegen {
    ostringstream o;
    map<string,int> locals;
    int frameSize=0;
    int labelN=0;
    vector<string> strings;
    string curEpilogue;

    string newLabel(){ return ".L"+to_string(labelN++); }
    int addString(const string& s){ strings.push_back(s); return (int)strings.size()-1; }

    void collectLocals(Node* n, int& count){
        if(!n) return;
        if(n->kind=="DECL" && !locals.count(n->s)){ count++; locals[n->s] = -8*count; }
        for(auto& c: n->ch) collectLocals(c.get(), count);
    }

    void genExpr(Node* n){
        if(n->kind=="NUM"){ o<<"\tmovq $"<<n->num<<", %rax\n"; return; }
        if(n->kind=="VAR"){
            if(!locals.count(n->s)){ fprintf(stderr,"undef var %s\n",n->s.c_str()); exit(1); }
            o<<"\tmovq "<<locals[n->s]<<"(%rbp), %rax\n"; return;
        }
        if(n->kind=="NEG"){ genExpr(n->ch[0].get()); o<<"\tnegq %rax\n"; return; }
        if(n->kind=="CALL"){
            int nargs=(int)n->ch.size();
            if(nargs>4){ fprintf(stderr,"max 4 args\n"); exit(1); }
            for(int i=nargs-1;i>=0;i--){ genExpr(n->ch[i].get()); o<<"\tpushq %rax\n"; }
            const char* regs[4]={"%rcx","%rdx","%r8","%r9"};
            for(int i=0;i<nargs;i++) o<<"\tpopq "<<regs[i]<<"\n";
            o<<"\tcall "<<n->s<<"\n";
            return;
        }
        if(n->ch.size()==2){
            genExpr(n->ch[0].get()); o<<"\tpushq %rax\n";
            genExpr(n->ch[1].get()); o<<"\tmovq %rax, %rcx\n\tpopq %rax\n";
            if(n->kind=="ADD") o<<"\taddq %rcx, %rax\n";
            else if(n->kind=="SUB") o<<"\tsubq %rcx, %rax\n";
            else if(n->kind=="MUL") o<<"\timulq %rcx, %rax\n";
            else if(n->kind=="DIV") o<<"\tcqto\n\tidivq %rcx\n";
            else {
                o<<"\tcmpq %rcx, %rax\n";
                string set = n->kind=="EQ"?"sete":n->kind=="NE"?"setne":n->kind=="LT"?"setl":n->kind=="GT"?"setg":n->kind=="LE"?"setle":"setge";
                o<<"\t"<<set<<" %al\n\tmovzbq %al, %rax\n";
            }
            return;
        }
        fprintf(stderr,"bad expr %s\n",n->kind.c_str()); exit(1);
    }

    void genStmt(Node* n){
        if(n->kind=="BLOCK"){ for(auto& c: n->ch) genStmt(c.get()); return; }
        if(n->kind=="DECL"){
            if(!n->ch.empty()){ genExpr(n->ch[0].get()); o<<"\tmovq %rax, "<<locals[n->s]<<"(%rbp)\n"; }
            return;
        }
        if(n->kind=="ASSIGN"){
            if(!locals.count(n->s)){ fprintf(stderr,"undef var %s\n",n->s.c_str()); exit(1); }
            genExpr(n->ch[0].get()); o<<"\tmovq %rax, "<<locals[n->s]<<"(%rbp)\n"; return;
        }
        if(n->kind=="IF"){
            string Lelse=newLabel(), Lend=newLabel();
            genExpr(n->ch[0].get());
            o<<"\ttestq %rax, %rax\n\tjz "<<Lelse<<"\n";
            genStmt(n->ch[1].get());
            o<<"\tjmp "<<Lend<<"\n"<<Lelse<<":\n";
            if(n->ch.size()==3) genStmt(n->ch[2].get());
            o<<Lend<<":\n"; return;
        }
        if(n->kind=="WHILE"){
            string Lb=newLabel(), Le=newLabel();
            o<<Lb<<":\n"; genExpr(n->ch[0].get());
            o<<"\ttestq %rax, %rax\n\tjz "<<Le<<"\n";
            genStmt(n->ch[1].get());
            o<<"\tjmp "<<Lb<<"\n"<<Le<<":\n"; return;
        }
        if(n->kind=="RET"){ genExpr(n->ch[0].get()); o<<"\tjmp "<<curEpilogue<<"\n"; return; }
        if(n->kind=="PRINTF"){
            int sid=addString(n->s);
            int nargs=(int)n->ch.size();
            if(nargs>3){ fprintf(stderr,"printf max 3 extra args\n"); exit(1); }
            for(int i=nargs-1;i>=0;i--){ genExpr(n->ch[i].get()); o<<"\tpushq %rax\n"; }
            const char* regs[3]={"%rdx","%r8","%r9"};
            for(int i=0;i<nargs;i++) o<<"\tpopq "<<regs[i]<<"\n";
            o<<"\tleaq .LC"<<sid<<"(%rip), %rcx\n\tcall printf\n";
            return;
        }
        if(n->kind=="EXPRSTMT"){ genExpr(n->ch[0].get()); return; }
        fprintf(stderr,"bad stmt %s\n",n->kind.c_str()); exit(1);
    }

    void genFunction(Node* fn){
        locals.clear();
        Node* params=fn->ch[0].get();
        Node* body=fn->ch[1].get();
        int count=0;
        for(auto& pn: params->ch){ count++; locals[pn->s] = -8*count; }
        collectLocals(body,count);
        int ls=8*count;
        frameSize = ((ls + 32) + 15) & ~15;
        o<<"\t.text\n\t.globl "<<fn->s<<"\n"<<fn->s<<":\n";
        o<<"\tpushq %rbp\n\tmovq %rsp, %rbp\n\tsubq $"<<frameSize<<", %rsp\n";
        const char* pregs[4]={"%rcx","%rdx","%r8","%r9"};
        int np=(int)params->ch.size();
        if(np>4){ fprintf(stderr,"max 4 params\n"); exit(1); }
        for(int i=0;i<np;i++) o<<"\tmovq "<<pregs[i]<<", "<<(-8*(i+1))<<"(%rbp)\n";
        curEpilogue=newLabel();
        genStmt(body);
        o<<"\tmovq $0, %rax\n"<<curEpilogue<<":\n";
        o<<"\tmovq %rbp, %rsp\n\tpopq %rbp\n\tret\n";
    }

    void genProgram(Node* prog){
        for(auto& f: prog->ch) genFunction(f.get());
        o<<"\t.section .rdata,\"dr\"\n";
        for(size_t i=0;i<strings.size();i++){
            o<<".LC"<<i<<":\n\t.ascii \"";
            for(unsigned char c: strings[i]){
                if(c=='\\') o<<"\\\\";
                else if(c=='"') o<<"\\\"";
                else if(c>=32 && c<127) o<<(char)c;
                else { char buf[8]; snprintf(buf,sizeof(buf),"\\%03o",c); o<<buf; }
            }
            o<<"\\0\"\n";
        }
    }
};

static string stripExt(const string& p){
    size_t slash=p.find_last_of("/\\");
    size_t dot=p.find_last_of('.');
    if(dot==string::npos || (slash!=string::npos && dot<slash)) return p;
    return p.substr(0,dot);
}

int main(int argc, char**argv){
    if(argc<2){
        fprintf(stderr,
            "mycc - mini C compiler\n"
            "usage: %s input.c [-o output] [-S] [--keep-asm]\n"
            "  default: produces <input>.exe (requires gcc on PATH)\n"
            "  -S     : stop after assembly, write <input>.s\n"
            "  -o X   : output name (X.exe, or X.s with -S)\n"
            "  --keep-asm : keep intermediate .s when producing .exe\n",
            argv[0]);
        return 1;
    }
    string in, out; bool asmOnly=false, keepAsm=false;
    for(int i=1;i<argc;i++){
        string a=argv[i];
        if(a=="-S") asmOnly=true;
        else if(a=="--keep-asm") keepAsm=true;
        else if(a=="-o" && i+1<argc) out=argv[++i];
        else if(a[0]=='-'){ fprintf(stderr,"unknown flag %s\n",a.c_str()); return 1; }
        else in=a;
    }
    if(in.empty()){ fprintf(stderr,"no input file\n"); return 1; }
    ifstream f(in); if(!f){ fprintf(stderr,"can't open %s\n",in.c_str()); return 1; }
    stringstream ss; ss<<f.rdbuf();
    Lexer L; L.src=ss.str(); L.run();
    Parser P(L.out);
    auto prog=P.parseProgram();
    Codegen C; C.genProgram(prog.get());

    string base = out.empty() ? stripExt(in) : stripExt(out);
    string asmPath = base + ".s";
    { ofstream of(asmPath); of<<C.o.str(); }
    if(asmOnly){
        fprintf(stderr,"wrote %s\n", asmPath.c_str());
        return 0;
    }
    string exePath = base + ".exe";
    string cmd = "gcc -static \"" + asmPath + "\" -o \"" + exePath + "\"";
    int rc = system(cmd.c_str());
    if(rc!=0){
        fprintf(stderr,"gcc failed (exit %d). Is MinGW gcc on PATH?\n", rc);
        return 1;
    }
    if(!keepAsm) remove(asmPath.c_str());
    fprintf(stderr,"wrote %s\n", exePath.c_str());
    return 0;
}
