/* CommonMark angle links and GFM literal links. The viewer only opens
   explicit web, mail, FTP and XMPP schemes for generated autolinks. */
static int ascii_alnum(int ch) {
    return (ch>='0' && ch<='9') || (ch>='A' && ch<='Z') || (ch>='a' && ch<='z');
}

static int angle_has_scheme(const char* start, const char* end) {
    const char* p=start;
    if (!((*p>='A' && *p<='Z') || (*p>='a' && *p<='z'))) return 0;
    while (++p<end && p-start<=32) {
        if (*p==':') return p-start>=2;
        if (!(ascii_alnum((unsigned char)*p) || *p=='+' || *p=='-' || *p=='.')) break;
    }
    return 0;
}

static int link_prefix(const char* p, const char* end, const char* prefix) {
    size_t n=strlen(prefix);
    return end-p>=(ptrdiff_t)n && _strnicmp(p,prefix,n)==0;
}

static int email_local(int ch, int angle) {
    return ascii_alnum(ch) || (ch && strchr(angle ? ".!#$%&'*+/=?^_`{|}~-" : ".-_+",ch)!=NULL);
}

static const char* email_end(const char* p, const char* end, int angle) {
    const char* q=p;
    int dots=0;
    while (q<end && email_local((unsigned char)*q,angle)) ++q;
    if (q==p || q==end || *q!='@') return NULL;
    ++q;
    for (;;) {
        const char* label=q;
        while (q<end && (ascii_alnum((unsigned char)*q) || *q=='-' || (!angle && *q=='_'))) ++q;
        if (q==label || (angle && (q-label>63 || !ascii_alnum((unsigned char)*label) ||
                                   !ascii_alnum((unsigned char)q[-1])))) return NULL;
        if (end-q<2 || *q!='.' ||
            !(ascii_alnum((unsigned char)q[1]) || (!angle && (q[1]=='_' || q[1]=='-')))) break;
        ++dots;
        ++q;
    }
    if (!angle && (!dots || !ascii_alnum((unsigned char)q[-1]))) return NULL;
    return q;
}

static int web_domain(const char* p, const char* end, int allowShort) {
    int dots=0, underscore=0, previousUnderscore=0;
    for (;;) {
        const char* label=p;
        underscore=0;
        while (p<end && (ascii_alnum((unsigned char)*p) || (unsigned char)*p>=0x80 || *p=='-' || *p=='_')) {
            if (*p=='_') underscore=1;
            ++p;
        }
        if (p==label) return 0;
        if (p==end || *p!='.') break;
        ++dots;
        previousUnderscore=underscore;
        ++p;
    }
    return (dots || allowShort) && !underscore && !previousUnderscore;
}

static const char* trim_web_link(const char* begin, const char* end) {
    int excessClose=0;
    for (const char* p=begin;p<end;++p)
        excessClose+=(*p==')')-(*p=='(');
    while (end>begin) {
        if (strchr("?!.,:*_~'\"",end[-1])) --end;
        else if (end[-1]==')' && excessClose>0) { --excessClose; --end; }
        else if (end[-1]==';') {
            const char* entity=end-1;
            while (entity>begin && ascii_alnum((unsigned char)entity[-1])) --entity;
            if (entity==end-1 || entity==begin || entity[-1]!='&') --end;
            else end=entity-1;
        } else break;
    }
    return end;
}

static size_t append_autolink(StrBuf* sb, const char* text, size_t len, size_t at, int allow) {
    const char *start=text+at, *limit=text+len, *end=NULL, *label=start;
    const char* hrefPrefix="";
    int prev=at ? (unsigned char)text[at-1] : 0;
    int angle=*start=='<';
    if (angle) {
        const char* p=++label;
        while (p<limit && (unsigned char)*p>0x20 && *p!='<' && *p!='>' && *p!=0x7f) ++p;
        if (p==limit || *p!='>' || p==label) return 0;
        end=p;
        if (!(link_prefix(label,end,"https://") || link_prefix(label,end,"http://") ||
              link_prefix(label,end,"ftp://") || link_prefix(label,end,"mailto:") ||
              link_prefix(label,end,"xmpp:"))) {
            if (email_end(label,end,1)!=end) {
                if (!angle_has_scheme(label,end)) return 0;
                sb_append_esc(sb,start,end-start+1);
                return end-start+1;
            }
            hrefPrefix="mailto:";
        }
    } else if (allow) {
        int boundary=!prev || isspace(prev) || strchr("*_~(",prev)!=NULL;
        int scheme=link_prefix(start,limit,"https://") ? 8 : link_prefix(start,limit,"http://") ? 7 :
                   link_prefix(start,limit,"ftp://") ? 6 : 0;
        int www=link_prefix(start,limit,"www.");
        if ((scheme && !ascii_alnum(prev) && prev!='<') || (www && boundary)) {
            const char* host=start+scheme, *stop=host;
            while (stop<limit && (unsigned char)*stop>0x20 && *stop!='<' && *stop!='>') ++stop;
            end=trim_web_link(host,stop);
            if (!web_domain(host,end,scheme!=0)) return 0;
            if (www) hrefPrefix="http://";
        } else if (!email_local(prev,0) && prev!='@') {
            int mail=link_prefix(start,limit,"mailto:");
            int xmpp=link_prefix(start,limit,"xmpp:");
            const char* address=start+(mail?7:xmpp?5:0);
            end=email_end(address,limit,0);
            if (!end) return 0;
            if (xmpp && limit-end>=2 && *end=='/' &&
                (ascii_alnum((unsigned char)end[1]) || end[1]=='@' || end[1]=='.')) {
                do { ++end; } while (end<limit && (ascii_alnum((unsigned char)*end) || *end=='@' || *end=='.'));
            }
            if (!mail && !xmpp) hrefPrefix="mailto:";
        }
    }
    if (!end) return 0;
    if (allow) {
        sb_append(sb,"<a href=\"");
        sb_append(sb,hrefPrefix);
        sb_append_esc(sb,label,end-label);
        sb_append(sb,"\">");
        sb_append_esc(sb,label,end-label);
        sb_append(sb,"</a>");
    } else sb_append_esc(sb,start,end-start+angle);
    return end-start+angle;
}
