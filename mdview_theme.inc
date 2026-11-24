/* Read VS Code-style JSON with comments. Only validated colors enter CSS. */
typedef struct { const char* start; const char* end; } ThemeValue;

static const char* theme_skip(const char* p) {
    for (;;) {
        while (isspace((unsigned char)*p)) ++p;
        if (p[0]=='/' && p[1]=='/') {
            while (*p && *p!='\n') ++p;
        } else if (p[0]=='/' && p[1]=='*') {
            const char* end=strstr(p+2,"*/");
            if (!end) return p+strlen(p);
            p=end+2;
        } else return p;
    }
}

static const char* theme_end(const char* p, int depth) {
    char end;
    if (depth>=32 || !*p) return NULL;
    if (*p=='"') {
        while (*++p && *p!='"') if (*p=='\\' && !*++p) return NULL;
        return *p ? p+1 : NULL;
    }
    if (*p!='{' && *p!='[') {
        const char* start=p;
        while (*p && !strchr(",}]:/ \r\n\t",*p)) ++p;
        return p>start ? p : NULL;
    }
    end=*p=='{' ? '}' : ']';
    for (p=theme_skip(p+1);*p && *p!=end;p=theme_skip(p)) {
        if (*p==',' || *p==':') ++p;
        else if (!(p=theme_end(p,depth+1))) return NULL;
    }
    return *p ? p+1 : NULL;
}

static int theme_is(ThemeValue value, const char* name) {
    return value.start && value.start[0]=='"' &&
           value.end-value.start==(ptrdiff_t)(strlen(name)+2) &&
           memcmp(value.start+1,name,strlen(name))==0;
}

static ThemeValue theme_member(ThemeValue object, const char* key) {
    ThemeValue none={0};
    const char* p=object.start;
    if (!p || *p!='{') return none;
    for (p=theme_skip(p+1);p<object.end && *p== '"';p=theme_skip(p+1)) {
        ThemeValue name={p,theme_end(p,0)}, value;
        if (!name.end) break;
        p=theme_skip(name.end);
        if (*p!=':') break;
        value.start=theme_skip(p+1); value.end=theme_end(value.start,0);
        if (!value.end || value.end>=object.end) break;
        if (theme_is(name,key)) return value;
        p=theme_skip(value.end);
        if (*p!=',') break;
    }
    return none;
}

static int theme_color(ThemeValue value, char color[10]) {
    ptrdiff_t n;
    if (!value.start || *value.start!='"' || value.start[1]!='#') return 0;
    n=value.end-value.start-2;
    if (n!=4 && n!=7 && n!=9) return 0;
    for (ptrdiff_t i=2;i<=n;i++)
        if (!isxdigit((unsigned char)value.start[i])) return 0;
    memcpy(color,value.start+1,n);
    color[n]=0;
    return 1;
}

static ThemeValue theme_token(ThemeValue array, const char* scope) {
    ThemeValue result={0};
    const char* p=array.start;
    if (!p || *p!='[') return result;
    for (p=theme_skip(p+1);p<array.end && *p=='{';p=theme_skip(p+1)) {
        ThemeValue item={p,theme_end(p,0)}, key, color;
        int match;
        if (!item.end) break;
        key=theme_member(item,"scope");
        match=theme_is(key,scope);
        if (key.start && *key.start=='[') {
            const char* q=theme_skip(key.start+1);
            while (q<key.end && *q=='"') {
                ThemeValue part={q,theme_end(q,0)};
                if (!part.end) break;
                if (theme_is(part,scope)) { match=1; break; }
                q=theme_skip(part.end);
                if (*q!=',') break;
                q=theme_skip(q+1);
            }
        }
        color=theme_member(theme_member(item,"settings"),"foreground");
        if (match && color.start) result=color;
        p=theme_skip(item.end);
        if (*p!=',') break;
    }
    return result;
}

static int theme_directory(wchar_t path[MAX_PATH]) {
    wchar_t* sep;
    DWORD n=GetModuleFileNameW(g_hInstance,path,MAX_PATH);
    if (!n || n>=MAX_PATH || !(sep=wcsrchr(path,L'\\'))) return 0;
    return wcscpy_s(sep+1,MAX_PATH-(sep+1-path),L"Themes\\")==0;
}

static char* theme_read(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    FILE* fp;
    long size;
    char* text=NULL;
    size_t n=wcslen(name);
    if (n<6 || _wcsicmp(name+n-5,L".json")!=0 || wcspbrk(name,L"/\\:*?\"<>|") ||
        !theme_directory(path) || wcscat_s(path,MAX_PATH,name)!=0) return NULL;
    fp=_wfopen(path,L"rb");
    if (!fp) return NULL;
    if (fseek(fp,0,SEEK_END)==0 && (size=ftell(fp))>0 && size<=256*1024 && fseek(fp,0,SEEK_SET)==0) {
        text=(char*)malloc(size+1);
        if (text && fread(text,1,size,fp)==(size_t)size) text[size]=0;
        else { free(text);text=NULL; }
    }
    fclose(fp);
    return text;
}

static ThemeValue theme_root(const char* text) {
    ThemeValue root={0};
    if (text) {
        if (strncmp(text,"\xef\xbb\xbf",3)==0) text+=3;
        root.start=theme_skip(text);
        root.end=theme_end(root.start,0);
        if (!root.end || *theme_skip(root.end)) root.start=NULL;
    }
    return root;
}

static int theme_type(ThemeValue root) {
    ThemeValue type=theme_member(root,"type");
    if (theme_is(type,"light") || theme_is(type,"hcLight")) return 0;
    if (theme_is(type,"dark") || theme_is(type,"hc")) return 1;
    return -1;
}

static void theme_rule(StrBuf* sb, const char* mode, const char* selector, const char* prop, const char* color) {
    sb_append(sb,mode); sb_append(sb,selector); sb_append_char(sb,'{');
    sb_append(sb,prop); sb_append_char(sb,':'); sb_append(sb,color); sb_append(sb,"}");
}

static void append_theme_css(StrBuf* sb, int dark) {
    static const struct { const char* key; const char* fallback; const char* selector; const char* prop; } rules[]={
        {"editor.background","", "", "background-color"},
        {"editor.background","", " #mdv-raw-pane", "background-color"},
        {"editor.foreground","foreground", "", "color"},
        {"editor.foreground","foreground", " #mdv-raw-pane", "color"},
        {"mdview.headingForeground","editor.foreground"," h1","color"},
        {"mdview.headingForeground","editor.foreground"," h2","color"},
        {"mdview.headingForeground","editor.foreground"," h3","color"},
        {"mdview.headingForeground","editor.foreground"," h4","color"},
        {"mdview.headingForeground","editor.foreground"," h5","color"},
        {"mdview.headingForeground","editor.foreground"," h6","color"},
        {"textLink.foreground","", " a","color"},
        {"textLink.foreground","", " summary:hover","color"},
        {"mdview.codeBackground","menu.background"," code","background-color"},
        {"mdview.codeBackground","menu.background"," pre","background-color"},
        {"mdview.codeBackground","menu.background"," th","background-color"},
        {"mdview.codeBackground","menu.background"," blockquote","background-color"},
        {"mdview.codeForeground","editor.foreground"," code","color"},
        {"editor.foreground","foreground"," pre code","color"},
        {"mdview.border","titleBar.border"," pre","border-color"},
        {"mdview.border","titleBar.border"," h1","border-bottom-color"},
        {"mdview.border","titleBar.border"," h2","border-bottom-color"},
        {"mdview.border","titleBar.border"," td","border-color"},
        {"mdview.border","titleBar.border"," th","border-color"},
        {"mdview.border","titleBar.border"," hr","border-top-color"},
        {"mdview.border","titleBar.border"," #mdv-render-pane","border-right-color"},
        {"textLink.foreground",""," blockquote","border-left-color"},
        {"mdview.mutedForeground","statusBar.foreground"," blockquote","color"},
        {"mdview.mutedForeground","statusBar.foreground"," .ln-nums","color"},
        {"mdview.markBackground",""," .mdv-highlight","background-color"},
        {"mdview.markForeground",""," .mdv-highlight","color"},
        {"mdview.markBackground",""," mark","background-color"},
        {"mdview.markForeground",""," mark","color"},
        {"mdview.findBackground","editor.findMatchHighlightBackground"," .hl","background-color"},
        {"mdview.findForeground",""," .hl","color"},
        {"mdview.findActiveBackground","editor.findMatchBackground"," .hl.hl-a","background-color"},
        {"mdview.findActiveForeground",""," .hl.hl-a","color"},
        {"mdview.mermaidBackground","editor.background"," .mdv-mermaid-view","background-color"},
        {"mdview.border","titleBar.border"," .mdv-mermaid-view","border-color"},
        {"mdview.mermaidBackground","editor.background"," .mdv-mm-label","fill"},
        {"mdview.mermaidBackground","editor.background"," .mdv-mm-marker.hollow","fill"},
        {"mdview.mermaidNodeBackground","menu.background"," .mdv-mm-node","fill"},
        {"mdview.mermaidEdge","textLink.foreground"," .mdv-mm-node","stroke"},
        {"mdview.mermaidEdge","textLink.foreground"," .mdv-mm-edge","stroke"},
        {"mdview.mermaidEdge","textLink.foreground"," .mdv-mm-life","stroke"},
        {"mdview.mermaidEdge","textLink.foreground"," .mdv-mm-marker","stroke"},
        {"mdview.mermaidEdge","textLink.foreground"," .mdv-mm-marker","fill"},
        {"editor.foreground","foreground"," .mdv-mm-text","fill"},
        {"mdview.markBackground",""," .mdv-mm-note","fill"},
        {"mdview.markForeground",""," .mdv-mm-note-text","fill"},
        {"menu.background",""," tr:nth-child(even)","background-color"},
        {"menu.background",""," #mdv-toc","background-color"},
        {"menu.foreground","editor.foreground"," #mdv-toc","color"},
        {"menu.background",""," #mdv-help","background-color"},
        {"menu.foreground","editor.foreground"," #mdv-help","color"},
        {"menu.background",""," #mdv-fb","background-color"},
        {"menu.foreground","editor.foreground"," #mdv-fb","color"},
        {"mdview.codeBackground","editor.background"," #mdv-fi","background-color"},
        {"editor.foreground","foreground"," #mdv-fi","color"},
        {"menu.background",""," .fb","background-color"},
        {"menu.foreground","editor.foreground"," .fb","color"},
        {"menu.selectionBackground",""," .fb:hover","background-color"},
        {"menu.background",""," #mdv-char","background-color"},
        {"menu.background",""," #mdv-raw-char","background-color"},
        {"menu.background",""," #mdv-status","background-color"},
        {"statusBar.foreground","editor.foreground"," #mdv-char","color"},
        {"statusBar.foreground","editor.foreground"," #mdv-raw-char","color"},
        {"statusBar.foreground","editor.foreground"," #mdv-status","color"}
    };
    static const struct { const char* scope; const char* selector; } syntax[]={
        {"keyword"," .sh-kw"},{"string"," .sh-str"},{"constant.numeric"," .sh-num"},
        {"comment"," .sh-cm"},{"entity.name.function"," .sh-fn"},{"keyword.operator"," .sh-op"},
        {"entity.name.type"," .sh-type"},{"entity.name.tag"," .sh-tag"},
        {"entity.other.attribute-name"," .sh-attr"},{"string"," .sh-val"},
        {"meta.preprocessor"," .sh-pp"},{"markup.heading"," .sh-md-heading"},
        {"markup.inserted"," .sh-diff-add"},{"markup.deleted"," .sh-diff-del"},
        {"comment"," .sh-diff-meta"},{"markup.changed"," .sh-diff-hunk"}
    };
    const char* mode=dark ? "body.mdv-dark.dark" : "body.mdv-light";
    char* text=theme_read(g_settings.themeFile[dark]);
    ThemeValue root=theme_root(text), colors=theme_member(root,"colors"), tokens=theme_member(root,"tokenColors");
    char color[10];
    if (theme_type(root)==dark) {
        for (size_t i=0;i<_countof(rules);++i) {
            if (theme_color(theme_member(colors,rules[i].key),color) ||
                theme_color(theme_member(colors,rules[i].fallback),color)) {
                /* Older MSHTML does not understand CSS hex alpha. */
                if (strlen(color)==9) {
                    unsigned int red,green,blue,alpha;
                    char rgba[40];
                    sscanf_s(color+1,"%2x%2x%2x%2x",&red,&green,&blue,&alpha);
                    sprintf_s(rgba,sizeof(rgba),"rgba(%u,%u,%u,%u.%03u)",red,green,blue,alpha/255,(alpha%255)*1000/255);
                    theme_rule(sb,mode,rules[i].selector,rules[i].prop,rgba);
                } else theme_rule(sb,mode,rules[i].selector,rules[i].prop,color);
                if (i==0 && strlen(color)!=9)
                    theme_rule(sb,dark?"html.mdv-dark":"html.mdv-light","","background-color",color);
            }
        }
        for (size_t i=0;i<_countof(syntax);++i)
            if (theme_color(theme_token(tokens,syntax[i].scope),color) && strlen(color)!=9)
                theme_rule(sb,mode,syntax[i].selector,"color",color);
    }
    free(text);
}
