/* Validate explicitly. XP and newer Windows handle invalid UTF-8 differently
   in MultiByteToWideChar, which would make the Windows-1252 fallback vary. */
static size_t utf8_sequence(const unsigned char* p, size_t remaining) {
    unsigned char lead=*p;
    size_t width;
    if (lead<0x80) return 1;
    if (lead>=0xc2 && lead<=0xdf) width=2;
    else if (lead>=0xe0 && lead<=0xef) width=3;
    else if (lead>=0xf0 && lead<=0xf4) width=4;
    else return 0;
    if (remaining<width) return 0;
    for (size_t i=1;i<width;++i) if ((p[i]&0xc0)!=0x80) return 0;
    if ((lead==0xe0 && p[1]<0xa0) || (lead==0xed && p[1]>=0xa0) ||
        (lead==0xf0 && p[1]<0x90) || (lead==0xf4 && p[1]>=0x90)) return 0;
    return width;
}

static unsigned int utf16_unit(const unsigned char* p, int bigEndian) {
    return bigEndian ? ((unsigned int)p[0]<<8)|p[1] : ((unsigned int)p[1]<<8)|p[0];
}

/* Without a BOM, infer byte order from the zero byte in ASCII text and verify
   that the resulting code units have valid surrogate pairs. */
static int guess_utf16(const unsigned char* text, size_t size) {
    size_t little=0, big=0;
    int bigEndian;
    if (size<2 || size%2) return 0;
    for (size_t i=0;i<size;i+=2) {
        unsigned char a=text[i],b=text[i+1];
        if (!b && ((a>=32 && a<=126) || a=='\n' || a=='\r' || a=='\t')) ++little;
        if (!a && ((b>=32 && b<=126) || b=='\n' || b=='\r' || b=='\t')) ++big;
    }
    if (!(little>3*big || big>3*little)) return 0;
    bigEndian=big>little;
    for (size_t i=0;i<size;i+=2) {
        unsigned int unit=utf16_unit(text+i,bigEndian);
        if (unit>=0xd800 && unit<=0xdbff) {
            if (size-i<4) return 0;
            unit=utf16_unit(text+i+2,bigEndian);
            if (unit<0xdc00 || unit>0xdfff) return 0;
            i+=2;
        } else if (unit>=0xdc00 && unit<=0xdfff) return 0;
    }
    return bigEndian ? 2 : 1;
}

static char* utf16_to_utf8(const unsigned char* text, size_t size, int bigEndian) {
    wchar_t* wide=(wchar_t*)malloc((size/2+2)*sizeof(*wide));
    size_t count=0;
    char* utf8;
    if (!wide) return NULL;
    for (size_t i=0;i+1<size;i+=2) {
        unsigned int unit=utf16_unit(text+i,bigEndian);
        if (unit>=0xd800 && unit<=0xdbff) {
            unsigned int low=size-i>=4 ? utf16_unit(text+i+2,bigEndian) : 0;
            if (low>=0xdc00 && low<=0xdfff) {
                wide[count++]=(wchar_t)unit;
                wide[count++]=(wchar_t)low;
                i+=2;
                continue;
            }
            unit=0xfffd;
        } else if (!unit || (unit>=0xdc00 && unit<=0xdfff)) unit=0xfffd;
        wide[count++]=(wchar_t)unit;
    }
    if (size%2) wide[count++]=0xfffd;
    wide[count]=0;
    utf8=wide_to_utf8_dup(wide);
    free(wide);
    return utf8;
}

/* Takes ownership of the NUL-terminated file buffer. */
static char* decode_markdown_text(char* buffer, size_t size) {
    const unsigned char* text=(const unsigned char*)buffer;
    size_t start=0, firstInvalid, firstNull=size;
    int utf16=0;
    char* result=NULL;
    if (size>=3 && text[0]==0xef && text[1]==0xbb && text[2]==0xbf) start=3;
    else if (size>=2 && ((text[0]==0xff && text[1]==0xfe) || (text[0]==0xfe && text[1]==0xff))) {
        utf16=text[0]==0xfe ? 2 : 1;
        start=2;
    } else utf16=guess_utf16(text,size);
    if (utf16) result=utf16_to_utf8(text+start,size-start,utf16==2);
    else {
        for (firstInvalid=start;firstInvalid<size;) {
            size_t width=utf8_sequence(text+firstInvalid,size-firstInvalid);
            if (!width) break;
            if (!text[firstInvalid] && firstNull==size) firstNull=firstInvalid;
            firstInvalid+=width;
        }
        if (firstInvalid==size && firstNull==size) {
            if (start) memmove(buffer,buffer+start,size-start+1);
            return buffer;
        }
        if (start || firstInvalid==size) {
            StrBuf clean;
            if (sb_init(&clean)) {
                size_t firstRepair=firstInvalid<firstNull ? firstInvalid : firstNull;
                sb_append_slice(&clean,buffer+start,firstRepair-start);
                for (size_t i=firstRepair;i<size;) {
                    size_t width=utf8_sequence(text+i,size-i);
                    if (width && text[i]) { sb_append_slice(&clean,buffer+i,width); i+=width; }
                    else { sb_append(&clean,"\xef\xbf\xbd"); ++i; }
                }
                result=clean.data;
            }
        } else {
            wchar_t* wide=(wchar_t*)malloc((size+1)*sizeof(*wide));
            if (wide) {
                int count=MultiByteToWideChar(1252,0,buffer,(int)size,wide,(int)size);
                for (int i=0;i<count;++i) if (!wide[i]) wide[i]=0xfffd;
                wide[count]=0;
                if (count) result=wide_to_utf8_dup(wide);
                free(wide);
            }
        }
    }
    free(buffer);
    return result;
}
