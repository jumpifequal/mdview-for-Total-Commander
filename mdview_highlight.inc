typedef enum { CODE_PLAIN, CODE_C, CODE_JSON, CODE_JSONC, CODE_DIFF } CodeLanguage;

typedef struct {
    int comment; /* 1 = block comment, 2 = continued line comment */
    char quote;
    char rawEnd[19]; /* ')' + up to 16 delimiter characters + '"' */
    int diffHunk;
    long diffOld, diffNew;
} CodeLexer;

static CodeLanguage code_language(const char* name, size_t len) {
    static const char* cNames[]={"c","h","cpp","c++","cc","cxx","hpp","h++","hh","hxx"};
    for (size_t i=0;i<_countof(cNames);++i)
        if (len==strlen(cNames[i]) && !_strnicmp(name,cNames[i],len)) return CODE_C;
    if (len==4 && !_strnicmp(name,"json",4)) return CODE_JSON;
    if (len==5 && !_strnicmp(name,"jsonc",5)) return CODE_JSONC;
    if ((len==4 && !_strnicmp(name,"diff",4)) || (len==5 && !_strnicmp(name,"patch",5))) return CODE_DIFF;
    return CODE_PLAIN;
}

/* Each token is escaped once. Source whitespace retains its own inline boxes
   so the IE7 word-wrap fallback can preserve the original text and indent. */
static void code_text(StrBuf* sb, const char* p, const char* end, int source) {
    if (source) {
        const char* start=p, *lineStart=p;
        while (p<end) {
            if (*p==' ' || *p=='\t') {
                const char* space=p++;
                while (p<end && (*p==' ' || *p=='\t')) ++p;
                if (p-space==1 && *space==' ' && !(source==2 && space==lineStart)) continue;
                sb_append_esc(sb,start,space-start);
                sb_append(sb,"<span class=\"mdv-source-space\" data-mdv-space=\"");
                for (const char* ch=space;ch<p;++ch) sb_append_char(sb,*ch=='\t' ? 't' : 's');
                sb_append(sb,"\">");
                for (const char* ch=space;ch<p;++ch)
                    sb_append(sb,*ch=='\t' ? "<span class=\"mdv-source-tab\">&nbsp;</span>" : "&nbsp;");
                sb_append(sb,"</span>");
                start=p;
            } else ++p;
        }
        sb_append_esc(sb,start,end-start);
    } else sb_append_esc(sb,p,end-p);
}

static void code_token(StrBuf* sb, const char* p, const char* end, const char* cls, int source) {
    if (!cls) { code_text(sb,p,end,source); return; }
    sb_append(sb,"<span class=\"");
    sb_append(sb,cls);
    sb_append(sb,"\">");
    code_text(sb,p,end,source);
    sb_append(sb,"</span>");
}

static int code_identifier(int ch) {
    return ascii_alnum(ch) || ch=='_' || ch>=0x80;
}

static int code_word(const char* p, size_t n, const char* words) {
    for (const char* word=words;*word;) {
        const char* sep=strchr(word,' ');
        size_t len=sep ? (size_t)(sep-word) : strlen(word);
        if (len==n && !memcmp(word,p,n)) return 1;
        if (!sep) break;
        word=sep+1;
    }
    return 0;
}

static const char* code_quoted(const char* p, const char* end, char quote, char* continuation) {
    *continuation=0;
    while (p<end) {
        char ch=*p++;
        if (ch==quote) break;
        if (ch=='\\') {
            if (p<end) ++p;
            else *continuation=quote;
        }
    }
    return p;
}

static const char* code_number(const char* p, const char* end, int c) {
    if (c) {
        int hex=end-p>1 && p[0]=='0' && (p[1]=='x' || p[1]=='X');
        for (++p;p<end;++p) {
            if (code_identifier((unsigned char)*p) || *p=='.' ||
                (*p=='\'' && p+1<end && ascii_alnum((unsigned char)p[1]))) continue;
            if ((*p=='+' || *p=='-') && strchr(hex?"pP":"eE",p[-1])) continue;
            break;
        }
        return p;
    }
    if (*p=='-') ++p;
    if (*p=='0') ++p;
    else while (p<end && *p>='0' && *p<='9') ++p;
    if (end-p>1 && *p=='.' && p[1]>='0' && p[1]<='9') {
        ++p;
        while (p<end && *p>='0' && *p<='9') ++p;
    }
    if (p<end && (*p=='e' || *p=='E')) {
        const char* exponent=p+1, *digits;
        if (exponent<end && (*exponent=='+' || *exponent=='-')) ++exponent;
        digits=exponent;
        while (exponent<end && *exponent>='0' && *exponent<='9') ++exponent;
        if (exponent>digits) p=exponent;
    }
    return p;
}

static long diff_range_count(const char** p) {
    char* end;
    long n=1;
    strtol(*p,&end,10);
    if (end==*p) return -1;
    if (*end==',') {
        const char* count=end+1;
        n=strtol(count,&end,10);
        if (end==count || n<0) return -1;
    }
    *p=end;
    return n;
}

static const char* diff_class(const char* line, CodeLexer* state) {
    if (!strncmp(line,"diff ",5)) {
        state->diffHunk=0;
        return "sh-diff-meta";
    }
    if (!strncmp(line,"@@",2)) {
        const char* p=line+2;
        state->diffHunk=1;
        state->diffOld=state->diffNew=-1;
        if (!strncmp(p," -",2)) {
            p+=2;
            state->diffOld=diff_range_count(&p);
            if (!strncmp(p," +",2)) { p+=2; state->diffNew=diff_range_count(&p); }
        }
        return "sh-diff-hunk";
    }
    if (!state->diffHunk &&
        (!strncmp(line,"--- ",4) || !strncmp(line,"+++ ",4) || !strncmp(line,"index ",6) ||
         !strncmp(line,"new file ",9) || !strncmp(line,"deleted file ",13) ||
         !strncmp(line,"rename ",7) || !strncmp(line,"copy ",5) ||
         !strncmp(line,"similarity ",11) || !strncmp(line,"Binary files ",13) ||
         !strncmp(line,"old mode ",9) || !strncmp(line,"new mode ",9))) return "sh-diff-meta";
    if (!strncmp(line,"\\ No newline",12)) return "sh-diff-meta";
    if (state->diffHunk) {
        if ((line[0]=='-' || line[0]==' ') && state->diffOld>0) --state->diffOld;
        if ((line[0]=='+' || line[0]==' ') && state->diffNew>0) --state->diffNew;
        if (!state->diffOld && !state->diffNew) state->diffHunk=0;
    }
    return line[0]=='+' ? "sh-diff-add" : line[0]=='-' ? "sh-diff-del" : NULL;
}

static void highlight_code_line(StrBuf* sb, const char* line, const char* end, CodeLanguage lang,
                                CodeLexer* state, int source, char nextNonspace) {
    static const char cKeywords[]=
        "alignas alignof asm auto break case catch class concept const consteval constexpr constinit const_cast "
        "continue co_await co_return co_yield default delete do dynamic_cast else enum explicit export extern "
        "false final for friend goto if inline mutable namespace new noexcept nullptr operator override private "
        "protected public register reinterpret_cast requires return sizeof static static_assert static_cast "
        "struct switch template this thread_local throw true try typedef typeid typename union using virtual "
        "volatile while _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local";
    static const char cTypes[]="bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t _Bool _Complex _Imaginary";
    const char* p=line;
    const char* first=p;
    int c=lang==CODE_C, header=0;
    if (lang==CODE_DIFF) { code_token(sb,p,end,diff_class(line,state),source?2:0); return; }
    if (lang==CODE_PLAIN) { code_text(sb,p,end,source?2:0); return; }
    while (first<end && (*first==' ' || *first=='\t')) ++first;
    while (p<end) {
        const char* start=p;
        const char* cls=NULL;
        if (state->comment==2 || ((c || lang==CODE_JSONC) && end-p>=2 && p[0]=='/' && p[1]=='/')) {
            state->comment=c && end[-1]=='\\' ? 2 : 0;
            p=end;
            cls="sh-cm";
        } else if (state->comment==1 || ((c || lang==CODE_JSONC) && end-p>=2 && p[0]=='/' && p[1]=='*')) {
            if (!state->comment) p+=2;
            while (end-p>=2 && !(p[0]=='*' && p[1]=='/')) ++p;
            if (end-p>=2) { p+=2; state->comment=0; }
            else { p=end; state->comment=1; }
            cls="sh-cm";
        } else if (*state->rawEnd) {
            size_t n=strlen(state->rawEnd);
            while (end-p>=(ptrdiff_t)n && memcmp(p,state->rawEnd,n)) ++p;
            if (end-p>=(ptrdiff_t)n) { p+=n; state->rawEnd[0]=0; }
            else p=end;
            cls="sh-str";
        } else if (state->quote || *p=='"' || (c && *p=='\'')) {
            char quote=state->quote ? state->quote : *p++;
            const char* next;
            p=code_quoted(p,end,quote,&state->quote);
            if (!c) state->quote=0;
            for (next=p;next<end && (*next==' ' || *next=='\t');++next) {}
            cls=!c && p>start+1 && p[-1]=='"' && (next<end ? *next : nextNonspace)==':' ? "sh-attr" : "sh-str";
        } else if (c && p==first && *p=='#') {
            ++p;
            while (p<end && (*p==' ' || *p=='\t')) ++p;
            const char* directive=p;
            while (p<end && code_identifier((unsigned char)*p)) ++p;
            header=code_word(directive,p-directive,"include include_next import");
            cls="sh-pp";
        } else if (header && *p=='<') {
            while (++p<end && *p!='>') {}
            if (p<end) ++p;
            header=0;
            cls="sh-str";
        } else if ((*p>='0' && *p<='9') ||
                   (end-p>1 && p[1]>='0' && p[1]<='9' && *p==(c?'.':'-'))) {
            p=code_number(p,end,c);
            cls="sh-num";
        } else if (code_identifier((unsigned char)*p)) {
            const char* next;
            while (p<end && code_identifier((unsigned char)*p)) ++p;
            if (c && p<end && *p=='"' && code_word(start,p-start,"R u8R uR UR LR")) {
                const char* delimiter=p+1, *open=delimiter;
                while (open<end && open-delimiter<=16 && !strchr("()\\ \t",*open)) ++open;
                if (open<end && *open=='(' && open-delimiter<=16) {
                    size_t n=open-delimiter;
                    state->rawEnd[0]=')';
                    memcpy(state->rawEnd+1,delimiter,n);
                    state->rawEnd[n+1]='"'; state->rawEnd[n+2]=0;
                    p=open+1;
                    while (end-p>=(ptrdiff_t)(n+2) && memcmp(p,state->rawEnd,n+2)) ++p;
                    if (end-p>=(ptrdiff_t)(n+2)) { p+=n+2; state->rawEnd[0]=0; }
                    else p=end;
                    cls="sh-str";
                }
            } else if (c && p<end && (*p=='"' || *p=='\'') && code_word(start,p-start,"u8 u U L")) {
                char quote=*p++;
                p=code_quoted(p,end,quote,&state->quote);
                cls="sh-str";
            } else if (code_word(start,p-start,c?cKeywords:"true false null")) cls="sh-kw";
            else if (c && code_word(start,p-start,cTypes)) cls="sh-type";
            else if (c) {
                for (next=p;next<end && (*next==' ' || *next=='\t');++next) {}
                if (next<end && *next=='(') cls="sh-fn";
            }
        } else if (*p==' ' || *p=='\t') {
            do { ++p; } while (p<end && (*p==' ' || *p=='\t'));
        } else {
            if (!c && strchr("{}[],:",*p)) cls="sh-op";
            ++p;
        }
        code_token(sb,start,p,cls,source && start==line ? 2 : source);
    }
    if (line==end && state->comment==2) state->comment=0;
}

static char code_next_nonspace(const Lines* lines, int from, int limit) {
    for (int i=from;i<limit;++i) {
        const char* p=lines->lines[i];
        while (*p==' ' || *p=='\t') ++p;
        if (*p) return *p;
    }
    return 0;
}
