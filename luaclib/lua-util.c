#include <lua.h>
#include <lauxlib.h>
#include <luaconf.h>
#include <lobject.h>
#include <lstate.h>
#include <lstring.h>
#include <ltable.h>
#include <lfunc.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <openssl/evp.h>  
#include <openssl/bio.h>  
#include <openssl/buffer.h> 
#include <openssl/rc4.h>  
#include <openssl/md5.h>  
#include <openssl/sha.h> 

#include <unistd.h>
#include <sys/prctl.h> 
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>     
#include <netdb.h>

#include "convert.h"
#include "linenoise.h"
#include "common.h"


#define LOG_ERROR   "\033[40;31m%s\033[0m\r\n"
#define LOG_WARN    "\033[40;33m%s\033[0m\r\n"
#define LOG_INFO    "\033[40;36m%s\033[0m\r\n"
#define LOG_DEBUG   "\033[40;37m%s\033[0m\r\n"

static const char* LOG_COLOR[] = { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG};


#define SMALL_CHUNK 256
#define HEX(v,c) { char tmp = (char) c; if (tmp >= '0' && tmp <= '9') { v = tmp-'0'; } else { v = tmp - 'a' + 10; } }

static int
lhex_encode(lua_State *L) {
    static char hex[] = "0123456789abcdef";
    size_t sz = 0;
    const uint8_t * text = (const uint8_t *)luaL_checklstring(L, 1, &sz);
    char tmp[SMALL_CHUNK];
    char *buffer = tmp;
    if (sz > SMALL_CHUNK/2) {
        buffer = lua_newuserdata(L, sz * 2);
    }
    int i;
    for (i=0;i<sz;i++) {
        buffer[i*2] = hex[text[i] >> 4];
        buffer[i*2+1] = hex[text[i] & 0xf];
    }
    lua_pushlstring(L, buffer, sz * 2);
    return 1;
}

static int
lhex_decode(lua_State *L) {
    size_t sz = 0;
    const char * text = luaL_checklstring(L, 1, &sz);
    if (sz & 1) {
        return luaL_error(L, "Invalid hex text size %d", (int)sz);
    }
    char tmp[SMALL_CHUNK];
    char *buffer = tmp;
    if (sz > SMALL_CHUNK*2) {
        buffer = lua_newuserdata(L, sz / 2);
    }
    int i;
    for (i=0;i<sz;i+=2) {
        uint8_t hi,low;
        HEX(hi, text[i]);
        HEX(low, text[i+1]);
        if (hi > 16 || low > 16) {
            return luaL_error(L, "Invalid hex text", text);
        }
        buffer[i/2] = hi<<4 | low;
    }
    lua_pushlstring(L, buffer, i/2);
    return 1;
}

int
lbase64_encode(lua_State* L) {
    size_t size;
    const char* source = lua_tolstring(L,1,&size);

    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); 
    
    BIO* bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64,bio);
    BIO_write(b64,source,size);
    BIO_flush(b64);

    BUF_MEM* buf = NULL;
    BIO_get_mem_ptr(b64,&buf);

    char* result = malloc(buf->length + 1);
    memcpy(result,buf->data,buf->length);
    result[buf->length] = 0;

    lua_pushlstring(L,result,buf->length + 1);
    BIO_free_all(b64); 
    free(result);
    return 1;
}

int
lbase64_decode(lua_State* L) {
    size_t size;
    const char* source = lua_tolstring(L,1,&size);

    char * buffer = (char *)malloc(size);  
    memset(buffer, 0, size);  
  
    BIO* b64 = BIO_new(BIO_f_base64());  
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  
    
    BIO* bio = BIO_new_mem_buf((void*)source, size);  
    bio = BIO_push(b64, bio);  
    int rsize = BIO_read(bio, buffer, size);  
    buffer[rsize] = '\0';

    lua_pushlstring(L,buffer,size);

    BIO_free_all(bio);
    free(buffer);
    return 1;
}

int
lmd5(lua_State* L) {
    size_t size;
    const unsigned char* source = (const unsigned char*)lua_tolstring(L,1,&size);
    unsigned char md5[16] = {0};
    MD5(source,size,md5); 
    lua_pushlstring(L,(const char*)md5,16);
    return 1;
}

int
lsha1(lua_State* L) {
    size_t size;
    const unsigned char* source = (const unsigned char*)lua_tolstring(L,1,&size);
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(source,size,digest);
    lua_pushlstring(L,(const char*)digest,SHA_DIGEST_LENGTH);
    return 1;
}

int
lrc4(lua_State* L) {
    size_t source_size;
    size_t key_size;
    const char* source = lua_tolstring(L,1,&source_size);
    const char* key = lua_tolstring(L,2,&key_size);

    RC4_KEY rc4_key;
    RC4_set_key(&rc4_key,key_size,(unsigned char*)key);

    char out[512] = {0};
    char* result = out;
    if (source_size > 512)
        result = malloc(source_size);

    RC4(&rc4_key,source_size,(unsigned char*)source,(unsigned char*)result);
    lua_pushlstring(L,result,source_size);
    if (result != out)
        free(result);

    return 1;
}


int
lload_script(lua_State* L) {
    size_t size;
    const char* buffer = lua_tolstring(L,1,&size);
    const char* name = lua_tostring(L,2);
    luaL_checktype(L,3,LUA_TTABLE);
    int status = luaL_loadbuffer(L,buffer,size,name);
    if (status != LUA_OK)
        luaL_error(L,"%s",lua_tostring(L,-1));

    lua_pushvalue(L, 3);
    lua_setupvalue(L, -2, 1);

    const Proto* f = getproto(L->top - 1);

    lua_newtable(L);
    int i;
    for (i=0; i<f->sizelocvars; i++) {
        lua_pushstring(L,getstr(f->locvars[i].varname));
        lua_pushinteger(L,f->lineinfo[f->locvars[i].startpc+1]);
        lua_settable(L,-3);
    }

    return 2;
}

static int
lthread_name(lua_State* L) {
    const char* name = lua_tostring(L,1);
    prctl(PR_SET_NAME, name); 
    return 0;
}

int
lthread_id(lua_State* L) {
    lua_pushinteger(L,syscall(SYS_gettid));
    return 1;
}

int
ltime(lua_State* L) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now = (tv.tv_sec + tv.tv_usec * 1e-6) * 100;
    lua_pushinteger(L,now);
    return 1;
}

int
lprint(lua_State* L) {
    int log_lv = lua_tointeger(L,1);
    const char* log = lua_tostring(L,2);
    printf(LOG_COLOR[log_lv],log);
    return 0;
}

int
ltostring(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            luaL_error(L,lua_tostring(L, -1));
        }
        return 1;
    }

   if (lua_isinteger(L, 1)) {
        LUAI_UACINT integer = (LUAI_UACINT)lua_tointeger(L, 1);
        int32_t i32 = (int32_t)integer;
        int64_t i64 = (int64_t)integer;
        char buff[64] = {0};
        size_t len;
        if ((LUAI_UACINT)i32 == integer) {
            char* end = i32toa_fast(i32, buff);
            *end = 0;
            len = end - buff;
            lua_pushlstring(L,buff,len);
        } else if ((LUAI_UACINT)i64 == integer) {
            char* end = i64toa_fast(i64, buff);
            *end = 0;
            len = end - buff;
            lua_pushlstring(L,buff,len);
        } else {
            lua_pushfstring(L, "%I", integer);
        }
    }  else {
        LUAI_UACNUMBER number = (LUAI_UACNUMBER)lua_tonumber(L, 1);
        double d = (double)number;
        if (!isnan(d) && !isinf(d) && (LUAI_UACNUMBER)d == number) {
            char buff[64] = {0};
            dtoa_fast(d, buff);
            lua_pushstring(L,buff);
        } else {
            lua_pushfstring(L, "%f", number);
        }
    }
    return 1;
}

int
ltype(lua_State* L) {
    int t = lua_type(L, 1);
    luaL_argcheck(L, t != LUA_TNONE, 1, "value expected");
    lua_pushinteger(L, t);
    return 1;
}

static lua_State* gL = NULL;

void completion(const char* str,linenoiseCompletions* lc) {
    lua_pushvalue(gL,2);
    lua_pushstring(gL,str);
    int r = lua_pcall(gL,1,1,0);
    if (r != LUA_OK)  {
        fprintf(stderr,"%s\n",lua_tostring(gL,-1));
        lua_pop(gL,1);
        return;
    }

    if (!lua_isnil(gL,-1)) {
        lua_pushnil(gL);
        while (lua_next(gL, -2) != 0) {
            const char* result = lua_tostring(gL,-1);
            linenoiseAddCompletion(lc,result);
            lua_pop(gL, 1);
        }
    }
    lua_pop(gL,1);
}

static int
lreadline(lua_State* L) {
    linenoiseSetCompletionCallback(completion);
    const char* prompt = lua_tostring(L,1);
    gL = L;
    char* line = linenoise(prompt);
    if (line) {
        linenoiseHistoryAdd(line);
        gL = NULL;
        lua_pushstring(L,line);
        free(line);
        return 1;
    }
    gL = NULL;
    return 0;
}

static int
lgetaddrinfo(lua_State* L) {
    const char* nodename = lua_tostring(L,1);
    const char* servname = lua_tostring(L,2);
    
    struct addrinfo hints;
    struct addrinfo* ai_list = NULL;
    struct addrinfo* ai_ptr = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(nodename, servname, &hints, &ai_list);
    if (status != 0) {
        free(ai_list);
        lua_pushboolean(L,0);
        lua_pushstring(L,gai_strerror(status));
        return 2;
    }

    lua_newtable(L);
    int index = 1;
    
    for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next ) {
        lua_newtable(L);

        lua_pushinteger(L,ai_ptr->ai_family);
        lua_setfield(L,-2,"ai_family");

        lua_pushinteger(L,ai_ptr->ai_socktype);
        lua_setfield(L,-2,"ai_socktype");

        lua_pushinteger(L,ai_ptr->ai_protocol);
        lua_setfield(L,-2,"ai_protocol");

        struct sockaddr* addr = ai_ptr->ai_addr;
        void* sin_addr;
        void* sin_port;
        if (ai_ptr->ai_family == AF_INET) {
            sin_port = (void*)&((struct sockaddr_in*)addr)->sin_port;
            sin_addr = (void*)&((struct sockaddr_in*)addr)->sin_addr;
        } else {
            sin_port = (void*)&((struct sockaddr_in6*)addr)->sin6_port;
            sin_addr = (void*)&((struct sockaddr_in6*)addr)->sin6_addr;
        }

        lua_pushinteger(L,ntohs((uint16_t)(uintptr_t)sin_port));
        lua_setfield(L,-2,"port");

        char host_buffer[128] = {0};

        if (inet_ntop(ai_ptr->ai_family, sin_addr, host_buffer, sizeof(host_buffer))) {
            lua_pushstring(L,host_buffer);
            lua_setfield(L,-2,"ip");
        }

        lua_seti(L,-2,index++);
    }
    return 1;
}

static int
labort(lua_State* L) {
    abort();
    return 0;
}

static int
lclone_string(lua_State* L) {
    void* data = lua_touserdata(L, 1);
    size_t size = lua_tointeger(L, 2);
    lua_pushlstring(L,data,size);
    return 1;
}

struct packet {
    uint8_t rseed;
    uint16_t rorder;
    uint8_t wseed;
    uint16_t worder;
};

static int
lpacket_unpack(lua_State* L) {
    struct packet* packet = lua_touserdata(L, 1);
    uint8_t* data = lua_touserdata(L, 2);
    size_t size = lua_tointeger(L, 3);

    ushort id = data[0] | data[1] << 8;

    lua_pushinteger(L, id);
    lua_pushlightuserdata(L, &data[2]);
    lua_pushinteger(L, size - 2);
    return 3;
}

static int
lpacket_pack(lua_State* L) {
    struct packet* packet = lua_touserdata(L, 1);
    int id = lua_tointeger(L, 2);
    size_t size;
    const char* data = NULL;
    switch(lua_type(L,3)) {
        case LUA_TSTRING: {
            data = lua_tolstring(L, 3, &size);
            break;
        }
        case LUA_TLIGHTUSERDATA:{
            data = lua_touserdata(L, 3);
            size = lua_tointeger(L, 4);
            break;
        }
        default:
            luaL_error(L,"unkown type:%s",lua_typename(L,lua_type(L,3)));
    }

    size_t total = size + sizeof(short) * 4;

    uint8_t* mb = malloc(total);
    memset(mb,0,total);
    memcpy(mb,&total,2);

    memcpy(mb+4,&packet->worder,2);
    memcpy(mb+6,&id,2);
    memcpy(mb+8,data,size);

    uint16_t sum = checksum((uint16_t*)(mb + 4),total - 4);
    memcpy(mb + 2,&sum,2);

    packet->worder++;

    int i;
    for (i = 2; i < total; ++i) {
        uint8_t tmp = mb[i];
        mb[i] = mb[i] ^ packet->wseed;
        packet->wseed += tmp;
    }
    lua_pushlightuserdata(L, mb);
    lua_pushinteger(L, total);
    return 2;
}

static int
lpacket_new(lua_State* L) {
    struct packet* packet = lua_newuserdata(L, sizeof(*packet));
    memset(packet,0,sizeof(*packet));

    if (luaL_newmetatable(L, "meta_packte")) {
        const luaL_Reg meta_packte[] = {
            { "pack", lpacket_pack },
            { "unpack", lpacket_unpack },
            { NULL, NULL },
        };
        luaL_newlib(L,meta_packte);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

static int
lrpc_pack(lua_State* L) {
    size_t size;
    void* data = NULL;
    int need_free = 0;
    switch(lua_type(L,1)) {
        case LUA_TSTRING: {
            data = (void*)lua_tolstring(L, 1, &size);
            break;
        }
        case LUA_TLIGHTUSERDATA:{
            need_free = 1;
            data = lua_touserdata(L, 1);
            size = lua_tointeger(L, 2);
            break;
        }
        default:
            luaL_error(L,"unkown type:%s",lua_typename(L,lua_type(L,1)));
    }

    int total = size + 4;
    char* buffer = malloc(total);
    memcpy(buffer,&total,4);
    memcpy(buffer+4,data,size);
    if (need_free)
        free(data);

    lua_pushlightuserdata(L,buffer);
    lua_pushinteger(L,total);
    return 2;
}

static int
lauthcode(lua_State* L) {
    size_t source_size;
    const char* source = luaL_checklstring(L, 1, &source_size);
    
    size_t key_size;
    const char* key = luaL_checklstring(L, 2, &key_size);

    int encode = luaL_optinteger(L ,3, 1);

    RC4_KEY rc4_key;
    RC4_set_key(&rc4_key,key_size,(unsigned char*)key);

    if (encode) {
        unsigned char source_md5[16] = {0};
        MD5((const unsigned char*)source,source_size,source_md5);

        size_t length = source_size + 16;
        char* block = malloc(length * 2);
        memcpy(block,source_md5,16);
        memcpy(block+16,source,source_size);

        RC4(&rc4_key,length,(unsigned char*)block,(unsigned char*)block + length);

        lua_pushlstring(L, block + length,length);
        free(block);
        return 1;
    }
    char* block = malloc(source_size);
    RC4(&rc4_key,source_size,(unsigned char*)source,(unsigned char*)block);
    unsigned char omd5[16] = {0};
    memcpy(omd5,block,16);

    unsigned char cmd5[16] = {0};
    MD5((const unsigned char*)block+16,source_size-16,cmd5);

    if (memcmp(omd5,cmd5,16) != 0) {
        free(block);
        luaL_error(L,"authcode decode error");
    }

    lua_pushlstring(L, block+16,source_size-16);
    free(block);
    return 1;
}

static inline void 
swap(lua_State* L, int from, int to) {
    lua_geti(L, 1, from);
    lua_geti(L, 1, to);
    lua_seti(L, 1, from);
    lua_seti(L, 1, to);
}

static inline int 
sort_comp(lua_State *L, int a, int b) {
    if ( lua_isnil(L, 2) )
        return lua_compare(L, a, b, LUA_OPLT);
    else {
        int res;
        lua_pushvalue(L, 3);
        lua_pushvalue(L, a - 1);
        lua_pushvalue(L, b - 2);
        lua_call(L, 2, 1);
        res = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return res;
    }
}

static int 
partition(lua_State *L, int lo, int up) {
    lua_geti(L, 1, lo - 1);

    int i = lo;
    int j = up;

    while ( i != j ) {
        while ( i < j ) {
            lua_geti(L, 1, j);
            if ( sort_comp(L, -1, -2) ) {
                lua_pop(L, 1);
                break;
            }
            j--;
            lua_pop(L, 1);
        }
    
        while ( i < j ) {
            lua_geti(L, 1, i);
            if ( !sort_comp(L, -1, -2) ) {
                lua_pop(L, 1);
                break;
            }
            i++;
            lua_pop(L, 1);
        }

        if (i < j)
            swap(L, i, j);
    }

    lua_geti(L, 1, i);
    if ( sort_comp(L, -1, -2) ) {
        swap(L, i, lo - 1);
    }
    lua_pop(L, 1);
    // printf("j:%d,lo:%d,up:%d\n",i,lo,up);
    return i;
}


static int topK(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t narr = lua_rawlen(L, 1);
    if ( narr < 2 )
        return 0;

    size_t K = luaL_checkinteger(L, 2);

    luaL_checktype(L, 3, LUA_TFUNCTION);
    if ( K >= narr ) {
        return 0;
    }

    K++;

    int low = 2;
    int high = narr;

    int j = partition(L, low, high);
    while (j != K) {
        if (K > j)
            low = j + 1;
        else 
            high = j - 1;
        j = partition(L, low, high);
    }
    return 0;
}

extern TValue *index2addr (lua_State *L, int idx);

static inline int
need_sizeof(lua_State* L) {
    lua_pushvalue(L, 1);
    lua_gettable(L, 2);
    if (!lua_isnil(L, -1)) {
       lua_pop(L, 1);
       return -1;
    }
    lua_pop(L, 1);

    lua_pushvalue(L, 1);
    lua_pushboolean(L, 1);
    lua_settable(L, 2);
    return 0;
}

static int
size_of(lua_State* L) {
    TValue* value = index2addr(L, 1);
    int type = lua_type(L,1);

    if (lua_isnoneornil(L, 2))
        lua_newtable(L);

    switch(type) {
        case LUA_TNIL: {
            lua_pushinteger(L, sizeof(TValue));
            break;
        }
        case LUA_TNUMBER: {
            lua_pushinteger(L, sizeof(TValue));
            break;
        }
        case LUA_TBOOLEAN: {
            lua_pushinteger(L, sizeof(TValue));
            break;
        }
        case LUA_TSTRING: {
            GCObject* o = gcvalue(value);
            switch (o->tt) {
                case LUA_TSHRSTR: {
                    if (need_sizeof(L) == 0) {
                        lua_pushinteger(L, sizelstring(gco2ts(o)->shrlen));
                    } else {
                        lua_pushinteger(L, 0);
                    }
                    break;
                }
                case LUA_TLNGSTR: {
                    lua_pushinteger(L, sizelstring(gco2ts(o)->u.lnglen));
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case LUA_TUSERDATA: {
            GCObject* o = gcvalue(value);
            if (need_sizeof(L) == 0) {
                lua_pushinteger(L, sizeudata(gco2u(o)));
            } else {
                lua_pushinteger(L, 0);
            }
            break;
        }
        case LUA_TFUNCTION: {
            GCObject* o = gcvalue(value);
            if (need_sizeof(L) < 0) {
                lua_pushinteger(L, 0);
            } else {
                   switch (o->tt) {
                    case LUA_TLCL: {
                        LClosure *cl = gco2lcl(o);
                        size_t size = sizeLclosure(cl->nupvalues);
                        
                        // int i;
                        // for (i=1;i<= cl->nupvalues;i++) {
                        //     lua_pushcfunction(L, size_of);
                        //     lua_getupvalue(L, 1, i);
                        //     lua_pushvalue(L, 2);
                        //     if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                        //         luaL_error(L,lua_tostring(L, -1));
                        //     }
                        //     size += lua_tointeger(L, -1);
                        //     lua_pop(L, 1);
                        // }
                        lua_pushinteger(L, size);
                        break;
                    }
                    case LUA_TCCL: {
                        CClosure *cl = gco2ccl(o);
                        size_t size = sizeLclosure(cl->nupvalues);
                        
                        // int i;
                        // for (i=1;i<= cl->nupvalues;i++) {
                        //     lua_pushcfunction(L, size_of);
                        //     lua_getupvalue(L, 1, i);
                        //     lua_pushvalue(L, 2);
                        //     if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                        //         luaL_error(L,lua_tostring(L, -1));
                        //     }
                        //     size += lua_tointeger(L, -1);
                        //     lua_pop(L, 1);
                        // }
                        lua_pushinteger(L, size);
                        break;
                    }
                    default:
                        break;
                }
            }
         
            break;
        }
        case LUA_TPROTO: {
            GCObject* o = gcvalue(value);
            if (need_sizeof(L) < 0) {
                lua_pushinteger(L, 0);
            } else {
               Proto *f = gco2p(o);
                size_t size = sizeof(Proto) + sizeof(Instruction) * f->sizecode +
                             sizeof(Proto *) * f->sizep +
                             sizeof(TValue) * f->sizek +
                             sizeof(int) * f->sizelineinfo +
                             sizeof(LocVar) * f->sizelocvars +
                             sizeof(Upvaldesc) * f->sizeupvalues;

                lua_pushinteger(L ,size); 
            }
            
            break;
        }

        case LUA_TTABLE: {
            GCObject* o = gcvalue(value);
            if (need_sizeof(L) < 0) {
                lua_pushinteger(L, 0);
            } else {
                Table *h = gco2t(o);
                size_t size = sizeof(Table) + sizeof(TValue) * h->sizearray + sizeof(Node) * cast(size_t, allocsizenode(h));

                lua_pushnil(L);
                while (lua_next(L, 1) != 0) {

                    int vtype = lua_type(L, -1);
                    if (vtype == LUA_TTABLE || vtype == LUA_TSTRING || vtype == LUA_TUSERDATA || vtype == LUA_TFUNCTION || vtype == LUA_TPROTO) {
                        lua_pushcfunction(L, size_of);
                        lua_pushvalue(L, -2);
                        lua_pushvalue(L, 2);
                        if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                            luaL_error(L,lua_tostring(L, -1));
                        }
                        size += lua_tointeger(L, -1);
                        lua_pop(L, 1); 
                    }
                    
                    int ktype = lua_type(L, -2);
                    if (ktype == LUA_TTABLE || ktype == LUA_TSTRING || ktype == LUA_TUSERDATA || ktype == LUA_TFUNCTION || ktype == LUA_TPROTO) {
                        lua_pushcfunction(L, size_of);
                        lua_pushvalue(L, -3);
                        lua_pushvalue(L, 2);
                         if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                            luaL_error(L,lua_tostring(L, -1));
                        }
                        size += lua_tointeger(L, -1);
                        lua_pop(L, 1);
                    }
                    
                    lua_pop(L, 1);
                }

                lua_pushinteger(L ,size);
            }
           
            break;
        }
        default:
            luaL_error(L, "unsupport type %s to sizeof", lua_typename(L, type));
    }
    return 1;
}

extern int lprofiler_start(lua_State* L);
extern int lprofiler_stack_start(lua_State *L);

int
luaopen_util_core(lua_State* L){
    luaL_Reg l[] = {
        { "hex_encode", lhex_encode },
        { "hex_decode", lhex_decode },
        { "base64_encode", lbase64_encode },
        { "base64_decode", lbase64_decode },
        { "md5", lmd5 },
        { "sha1", lsha1 },
        { "rc4", lrc4 },
        { "authcode", lauthcode },
        { "load_script", lload_script },
        { "thread_name", lthread_name },
        { "thread_id", lthread_id },
        { "time", ltime },
        { "print", lprint },
        { "type", ltype },
        { "readline", lreadline },
        { "getaddrinfo", lgetaddrinfo },
        { "abort", labort },
        { "clone_string", lclone_string },
        { "packet_new", lpacket_new },
        { "rpc_pack", lrpc_pack },
        { "topK", topK },
        { "size_of", size_of },
        { "profiler_start", lprofiler_start },
        { "profiler_stack_start", lprofiler_stack_start },
        { NULL, NULL },
    };
    luaL_newlib(L,l);

    lua_getglobal(L, "tostring");
    lua_pushcclosure(L, ltostring, 1);
    lua_setfield(L, -2, "tostring");

    return 1;
}
