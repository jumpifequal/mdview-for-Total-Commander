/* A bounded TeX subset rendered with selectable HTML text. */
typedef struct {
    const char* p;
    const char* end;
    StrBuf tag;
    int display;
    int depth;
} MathParser;

static int math_expr(MathParser*, StrBuf*);

static void math_space(MathParser* m) {
    while (m->p < m->end && isspace((unsigned char)*m->p)) ++m->p;
}

static int math_atom(MathParser* m, StrBuf* sb) {
    static const struct { const char* name; const char* html; } symbols[] = {
        {"alpha","&alpha;"},{"beta","&beta;"},{"gamma","&gamma;"},{"delta","&delta;"},
        {"epsilon","&epsilon;"},{"theta","&theta;"},{"lambda","&lambda;"},{"mu","&mu;"},
        {"pi","&pi;"},{"rho","&rho;"},{"sigma","&sigma;"},{"phi","&phi;"},{"omega","&omega;"},
        {"Gamma","&Gamma;"},{"Delta","&Delta;"},{"Theta","&Theta;"},{"Lambda","&Lambda;"},
        {"Pi","&Pi;"},{"Sigma","&Sigma;"},{"Phi","&Phi;"},{"Omega","&Omega;"},
        {"infty","&infin;"},{"times","&times;"},{"cdot","&middot;"},{"pm","&plusmn;"},
        {"le","&le;"},{"leq","&le;"},{"ge","&ge;"},{"geq","&ge;"},{"neq","&ne;"},
        {"approx","&asymp;"},{"to","&rarr;"},{"rightarrow","&rarr;"},{"partial","&part;"},
        {"nabla","&nabla;"},{"forall","&forall;"},{"exists","&exist;"},{"in","&isin;"},
        {"sin","sin"},{"cos","cos"},{"tan","tan"},{"log","log"},{"ln","ln"},{"exp","exp"},
        {"quad","&emsp;"},{"qquad","&emsp;&emsp;"}
    };
    const char* start;
    char cmd[24];
    size_t n;
    int result;
    math_space(m);
    if (m->p == m->end || m->depth >= 24) return 0;
    if (*m->p == '{') {
        ++m->p;
        ++m->depth;
        result = math_expr(m, sb);
        --m->depth;
        if (!result || m->p == m->end || *m->p != '}') return 0;
        ++m->p;
        return 1;
    }
    if (*m->p != '\\') {
        char ch = *m->p++;
        if (ch == '}' || ch == '^' || ch == '_') return 0;
        start = m->p - 1;
        while (m->p < m->end && ((unsigned char)*m->p & 0xc0) == 0x80) ++m->p;
        if (isalpha((unsigned char)ch)) sb_append(sb,"<i>");
        if (strchr("=+-*/<>", ch)) sb_append(sb,"<span class=\"mdv-math-bin\">");
        sb_append_esc(sb, start, m->p - start);
        if (strchr("=+-*/<>", ch)) sb_append(sb,"</span>");
        if (isalpha((unsigned char)ch)) sb_append(sb,"</i>");
        return 1;
    }
    start = ++m->p;
    while (m->p < m->end && isalpha((unsigned char)*m->p)) ++m->p;
    n = m->p - start;
    if (!n && m->p < m->end) {
        char ch = *m->p++;
        if (strchr(",;: ", ch)) return sb_append(sb, "&thinsp;");
        if (ch == '!') return 1;
        if (strchr("{}_$%#&|", ch)) {
            sb_append_esc(sb, &ch, 1);
            return 1;
        }
    }
    if (!n || n >= sizeof(cmd)) return 0;
    memcpy(cmd,start,n);
    cmd[n] = 0;
    for (size_t i = 0; i < _countof(symbols); ++i)
        if (strcmp(cmd,symbols[i].name) == 0) return sb_append(sb,symbols[i].html);
    if (strcmp(cmd,"sum") == 0 || strcmp(cmd,"prod") == 0 || strcmp(cmd,"int") == 0) {
        sb_append(sb,"<span class=\"mdv-math-big\">");
        sb_append(sb, cmd[0]=='s' ? "&sum;" : cmd[0]=='p' ? "&prod;" : "&int;");
        return sb_append(sb,"</span>");
    }
    if (strcmp(cmd,"left") == 0 || strcmp(cmd,"right") == 0) {
        math_space(m);
        if (m->p < m->end && *m->p == '.') { ++m->p; return 1; }
        if (m->p == m->end) return 0;
        if (*m->p=='\\') {
            if (m->end-m->p<2 || !strchr("{}|",m->p[1])) return 0;
        } else if (!strchr("()[]|",*m->p)) return 0;
        return math_atom(m,sb);
    }
    if (strcmp(cmd,"text") == 0 || strcmp(cmd,"mathrm") == 0 || strcmp(cmd,"tag") == 0) {
        int tag = strcmp(cmd,"tag") == 0;
        math_space(m);
        if (m->p == m->end || *m->p != '{' || (tag && (!m->display || m->depth || m->tag.len))) return 0;
        start = ++m->p;
        while (m->p < m->end && *m->p != '}' && *m->p != '{' && *m->p != '\\') ++m->p;
        if (m->p == m->end || *m->p != '}') return 0;
        sb_append_esc(tag ? &m->tag : sb,start,m->p++ - start);
        return 1;
    }
    ++m->depth;
    if (strcmp(cmd,"frac") == 0) {
        sb_append(sb,"<span class=\"mdv-math-frac\"><span>");
        result = math_atom(m,sb);
        sb_append(sb,"</span><span>");
        result = result && math_atom(m,sb);
        sb_append(sb,"</span></span>");
    } else if (strcmp(cmd,"sqrt") == 0) {
        sb_append(sb,"<span class=\"mdv-math-root\"><span class=\"mdv-math-radical\">&radic;</span><span class=\"mdv-math-radicand\">");
        result = math_atom(m,sb);
        sb_append(sb,"</span></span>");
    } else result = 0;
    --m->depth;
    return result;
}

static int math_expr(MathParser* m, StrBuf* sb) {
    math_space(m);
    while (m->p < m->end && *m->p != '}') {
        int sum = m->display && m->end-m->p >= 4 &&
                  (strncmp(m->p,"\\sum",4) == 0 || (m->end-m->p >= 5 && strncmp(m->p,"\\prod",5) == 0));
        StrBuf limits={0}, scripts[2]={{0},{0}};
        int good;
        if (sum && !sb_init(&limits)) return 0;
        good=math_atom(m,sum ? &limits : sb);
        math_space(m);
        while (good && m->p < m->end && (*m->p == '_' || *m->p == '^')) {
            int pos=*m->p++ == '^' ? 0 : 1;
            if (scripts[pos].data || !sb_init(&scripts[pos])) { good=0; break; }
            ++m->depth;
            good=math_atom(m,&scripts[pos]);
            --m->depth;
            math_space(m);
        }
        if (good && (scripts[0].data || scripts[1].data || sum)) {
            if (sum) sb_append(sb,"<span class=\"mdv-math-limits\">");
            else if (!scripts[1].data) sb_append(sb,"<span class=\"mdv-math-scripts mdv-math-raised\">");
            else if (!scripts[0].data) sb_append(sb,"<span class=\"mdv-math-scripts mdv-math-lowered\">");
            else sb_append(sb,"<span class=\"mdv-math-scripts\">");
            if (scripts[0].data) {
                sb_append(sb,"<span class=\"mdv-math-up\">");
                sb_append(sb,scripts[0].data);
                sb_append(sb,"</span>");
            }
            if (sum) {
                sb_append(sb,"<span>");
                sb_append(sb,limits.data);
                sb_append(sb,"</span>");
            }
            if (scripts[1].data) {
                sb_append(sb,"<span class=\"mdv-math-down\">");
                sb_append(sb,scripts[1].data);
                sb_append(sb,"</span>");
            }
            sb_append(sb,"</span>");
        }
        free(limits.data);
        free(scripts[0].data);
        free(scripts[1].data);
        if (!good) return 0;
    }
    return 1;
}

static int append_math(StrBuf* sb, const char* src, size_t len, int display) {
    MathParser m;
    StrBuf html;
    int ok;
    if (!len || len > 8192 || !sb_init(&html)) return 0;
    m.p=src; m.end=src+len; m.display=display; m.depth=0;
    if (!sb_init(&m.tag)) { free(html.data); return 0; }
    ok=math_expr(&m,&html) && m.p == m.end && html.len;
    if (ok) {
        sb_append(sb,"<span class=\"mdv-math\">");
        sb_append(sb,html.data);
        sb_append(sb,"</span>");
        if (m.tag.len) {
            sb_append(sb,"<span class=\"mdv-math-tag\">(");
            sb_append(sb,m.tag.data);
            sb_append(sb,")</span>");
        }
    }
    free(m.tag.data);
    free(html.data);
    return ok;
}
