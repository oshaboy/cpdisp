#include <unicode/ucnv.h>
#include <unicode/uchar.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <uchar.h>
#ifdef ENABLE_ICONV
#include <iconv.h>
#include <errno.h>
#endif
#ifdef ENABLE_LIBICONV
#include <errno.h>
void * libiconv_open (const char* tocode, const char* fromcode);
size_t libiconv (void * cd,  char* * inbuf, size_t *inbytesleft, char* * outbuf, size_t *outbytesleft);
int libiconv_close (void * cd);
#endif
#ifdef ENABLE_GCONV
#include <gconv.h>
#include <dlfcn.h>
#endif
#include "mapconv/mapping_file_parser.h"

static const char helptext[] = "\n\
Generate nice looking charts of character encodings within the terminal.\n\
\n\
Usage:\n\
    -h --help : print this help.\n\
    -w --wide: print 2 byte table.\n\
    -d [filename] : load custom icu data file.\n\
    -i : require user input between pages (only if -w is enabled).\n\
    -r --range [from]:[to] : display only pages associated with this range of bytes.\n\
    -n --no-format : no format.\n\
    -N --raw : no format and print control character raw.\n\
    -x [byte]:[byte]:[byte]... : prefix in hex.\n\
    -c : print hex code and name of control characters and whitespace characters.\n"
#ifdef ENABLE_ICONV
"    --iconv : use iconv backend.\n"
#endif 
#ifdef ENABLE_LIBICONV
"    --libiconv : use libiconv backend.\n"
#endif
"    --locale : use locale instead.\n\
\n\
Legend:\n\
    Blue: Control Character\n\
    Red: Invalid Character\n\
    Green : Prefix of incomplete character\n\
    Purple/Dark Magenta: Private Use Character\n\
    Dark Yellow: Something I didn't expect\n\
\n";
#include <threads.h>
thread_local static UErrorCode err=U_ZERO_ERROR , idc=U_ZERO_ERROR;

static const int attribute_red_background = 41;
static const int attribute_green_background=42;
static const int attribute_yellow_background=43;
static const int attribute_blue_background=44;
static const int attribute_magenta_background=45;
static const int attribute_light_gray_background=47;
static const int attribute_default_background=49;
static const int attribute_bright_blue_background = 104;


static UBool u_isundefined(UChar32 c) {return !u_isdefined(c);}
const UChar * find_predicate_in_string(const UChar * str, UBool (*predicate)(UChar32), size_t length ){
    if (length==0) return NULL;
    UChar32 c;
    int i=0;
    do {
        int ibak=i;
        U16_NEXT(str, i, length, c);
        if (predicate(c)) return &str[ibak];
    } while(i<length);
    return NULL;
}

static UBool u_isPUA(UChar32 c){
    return u_charType(c)==U_PRIVATE_USE_CHAR;
}

static void attrPrint(int attribute, char * str){
    printf("\e[%dm\xe2\x80\xad%s\xe2\x80\xac ", attribute,str);
}
static void attrPrintSpace(int attribute){
    attrPrint(attribute, " ");
}

static size_t message_index=0;
static char * messages[256];
static void attrPrintMessage(int attribute, const char * message){
    char message_index_string[3] =
        {'A'+(message_index/16),'A'+(message_index%16),'\0'};
    attrPrint(attribute, message_index_string);
    const size_t formatted_message_size=17+strlen(message);
    char * message_formatted=malloc(formatted_message_size);
    snprintf(message_formatted,formatted_message_size,"\e[%dm%s: %s\e[49m\n",
        attribute, 
        message_index_string,
        message
    );
    messages[message_index++]=message_formatted;
}

static void printAllMessages(){
    for (size_t i=0; i<message_index; i++){
        printf("%s",messages[i]);
        free(messages[i]);
    }
    message_index=0;
}
static void attrPrintRaw(int attribute, unsigned char raw){
    char buf[3];
    sprintf(buf,"%02hhx",raw);
    attrPrint(attribute, buf);
}
static void attrPrintCodepointAsHex(int attribute, const UChar32 codepoint){
    if (codepoint < 0x100){
        attrPrintRaw(attribute, (unsigned char)codepoint);
    } else {
        size_t name_length = u_charName(
            codepoint,
            U_UNICODE_CHAR_NAME,
            NULL,
            0,
            &idc
        );
        idc=U_ZERO_ERROR;
        char codepoint_str[10+name_length];
        char name_buffer[name_length+1];
        const char * format_str;

        u_charName(
            codepoint,
            U_UNICODE_CHAR_NAME,
            name_buffer,
            name_length+1,
            &idc
        );
        if (codepoint < 0x10000) 
            format_str="U+%04X %s";
        else 
            format_str="U+%06X %s";
        sprintf(codepoint_str,format_str, codepoint,name_buffer);
        attrPrintMessage(attribute, codepoint_str);
    }
    

}


typedef struct {
    size_t capacity;
    size_t index;
    char buf[];
} inbuf_type;
typedef enum {
    ICU=0,
    #ifdef ENABLE_ICONV
    ICONV,
    #endif
    #ifdef ENABLE_GCONV
    GCONV,
    #endif
    #ifdef ENABLE_LIBICONV
    LIBICONV,
    #endif
    LOCALE,
    MAPPING_FILE,
    BACKEND_END
} Backend;
#ifdef ENABLE_GCONV
typedef struct {
    void * shared_object;
    __gconv_fct gconv;
    __gconv_end_fct gconv_end;
    struct __gconv_step step;
} gconv_nonsense;
#endif
typedef struct {
    void * converter;
    uint8_t from_table, to_table;
    Backend backend : 3;
    bool interactive : 1;
    bool no_format_bool : 1;
    bool control_codes_raw : 1;
    bool fail : 1;
    bool wide : 1;
    bool help : 1;
    bool verbose_control_codes_and_whitespace : 1;
} Config;


Config CreateConfig(int argc, char * argv[], inbuf_type * inbuf){
    Config config={
        .no_format_bool=false,
        .interactive=false,
        .wide=false,
        .fail=false,
        .help=false,
        .control_codes_raw=false,
        .verbose_control_codes_and_whitespace=false
    };
    int opt;
    char * dat_filename=NULL;
    int from_table=0, to_table=255;
    static int backend;
    backend=ICU;
    static const char optstring[] = "wNhnd:x:r:i2c";
    static const struct option longopts[] = {
        {"help", 0, NULL, 'h'},
        {"wide", 0, NULL, 'w'},
        {"range", 1, NULL, 'r'},
        {"no-format", 0, NULL, 'n'},
        {"raw", 0, NULL, 'N'},
        #ifdef ENABLE_ICONV
        {"iconv", 0, &backend, ICONV},
        #endif 
        #ifdef ENABLE_LIBICONV
        {"libiconv", 0, &backend, LIBICONV},
        #endif 
        {"mapfile", 0, &backend, MAPPING_FILE},
        //{"gconv", 0, &backend, GCONV},
        {"locale", 0, &backend, LOCALE},
        {"icu", 0, &backend, ICU},
        {0}
    };
    while ((opt=getopt_long(argc, argv, optstring,longopts,NULL))!=-1){
        switch (opt){
            case 'r':{
            if (isdigit(*optarg))
                from_table=atoi(optarg);
            const char * to_table_str=strchr(optarg, ':');
            if (to_table_str == NULL)
                to_table=from_table;
            else if (isdigit(*(to_table_str+1)))
                to_table=atoi(to_table_str+1);
            } break;

            case 'i':
            config.interactive=true;
            break;

            case 'w':
            config.wide=true;
            break;

            case 'x':
            {
            const char * cur=optarg-1;
            const char * next;
            while(cur != NULL) {
                unsigned char hex_byte;
                next = strchr(cur+1, ':');
                sscanf(cur+1, "%hhx", &hex_byte);
                if (inbuf->capacity-4 <= inbuf->index){	
                    inbuf->capacity*=4;
                    inbuf=realloc(inbuf, inbuf->capacity*sizeof(char) + 3*sizeof(size_t));
                }
                inbuf->buf[inbuf->index++]=hex_byte;
                cur=next;
            }

            break;
            }

            case 'd':
            dat_filename=optarg;
            break;
            case 'N':
            config.control_codes_raw = true;
            case 'n':
            config.no_format_bool = true;
            break;

            case 'h':
            printf("%s",helptext);
            config.help=true;
            return config;

            case 'c':
            config.verbose_control_codes_and_whitespace=true;
            break;

            case '?':
            default:
            fprintf(stderr,"Unknown Option %c\n",opt);
            config.fail=true;
            return config;

            case '\0':
        }
    }
    config.backend=backend;
    if (argc < optind+1){
        fprintf(stderr,"No codepage given\n");
        config.fail=true;
        return config;
    }
    if (!config.wide){
        to_table=from_table=0;
        config.interactive=false;
    }
    if (
        from_table >= 256 || to_table >= 256 ||
        from_table < 0 || to_table < 0
    ){
        fprintf(stderr,"Table index must be between 0 and 255\n");
        config.fail=true;
        return config;
    } else if (to_table < from_table){
        fprintf(stderr,"Range is the wrong way around\n");
        config.fail=true;
        return config;
    }
    config.from_table=from_table;
    config.to_table=to_table;
    const char * errmsg;
    switch (backend){
        case ICU:
            if (dat_filename) 
                config.converter=ucnv_openPackage(
                    dat_filename,
                    argv[optind],
                    &err
                );
            else
                config.converter=ucnv_open(
                    argv[optind],
                    &err
                );
            if (U_SUCCESS(err)) ucnv_setToUCallBack(
                config.converter,
                UCNV_TO_U_CALLBACK_STOP,
                NULL,
                NULL,
                NULL,
                &idc
            ); else {
                config.fail=true;
                errmsg="No such codepage %s\n";
                goto end;
            }
        break;
        #ifdef ENABLE_ICONV
        case ICONV: {
            /* Silly endianess hack */
            union {
                char is_little_endian:8;
                UChar32 a;
            } e={.a=1};
            config.converter=iconv_open(
                e.is_little_endian?"UTF-16LE":"UTF-16BE",
                argv[optind]
            );
            if (config.converter==(void *)-1){
                config.fail=true;
                errmsg="No such codepage %s\n";
                goto end;
            }
            
        } break;
        #endif 

        case LOCALE:
        if (setlocale(LC_CTYPE,argv[optind])==NULL) {
            errmsg="No such locale %s\n";
            config.fail=true;
            goto end;
        }
        #ifdef ENABLE_GCONV
        case GCONV:{
            void * shared_object=dlopen(argv[optind],RTLD_NOW);
            if (!shared_object) {
                errmsg="dlopen failed %s\n";
                config.fail=true;
                goto end;
            }
            __gconv_init_fct gconv_init=dlsym(shared_object,"gconv_init");
            __gconv_fct gconv_gconv=dlsym(shared_object,"gconv");
            __gconv_end_fct gconv_end=dlsym(shared_object,"gconv_end");
            if (!gconv_gconv || !gconv_init || !gconv_end) {
                errmsg="Shared object isn't a gconv library %s\n";
                dlclose(shared_object);
                config.fail=1;
                goto end;
            }
            gconv_nonsense * gconv = malloc(sizeof(gconv_nonsense));
            *gconv=(gconv_nonsense){
                .shared_object=shared_object,
                .gconv=gconv_gconv,
                .gconv_end=gconv_end
            };
            gconv_init(&gconv->step);
            config.converter=gconv;

        }
        break;
        #endif
        #ifdef ENABLE_LIBICONV
        case LIBICONV: {
            /* Silly endianess hack */
            union {
                char is_little_endian:8;
                UChar32 a;
            } e={.a=1};
            config.converter=libiconv_open(
                e.is_little_endian?"UTF-16LE":"UTF-16BE",
                argv[optind]
            );
            if (config.converter==(void *)-1){
                config.fail=true;
                errmsg="No such codepage %s\n";
                goto end;
            }
            
        } break;
        #endif
        case MAPPING_FILE:
            config.converter=malloc(sizeof(MappingTable));
            FILE * mapping_file=fopen(argv[optind], "rt");
            if (!mapping_file) {
                errmsg="No such file %s\n";
                config.fail=true;
                goto end;
            }
                
            *(MappingTable *)config.converter=parse_mapping_file(mapping_file);
            fclose(mapping_file);
            if (!((MappingTable *)config.converter)->table){
                config.fail=true;
                errmsg="Invalid mapping file %s\n";
                goto end;
            }

        break;
    }
end:
    if (config.fail)
        fprintf(stderr,errmsg, argv[optind]);
    


    return config;
}

void print_fonttest(const Config config, inbuf_type * inbuf){
#define format for(bool _once=1; _once && !config.no_format_bool; _once=0)

    for (int table=config.from_table; table <= config.to_table; table++){
        format printf("Table %d:\n",table);

        format printf("  \e[7m0 1 2 3 4 5 6 7 8 9 a b c d e f \n\n"
            "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\na\nb\nc\nd\ne\nf\n"
            "\e[17A\e[27m\e7");
        for (int y=0; y<16; y++){
            for (int x=0; x<16; x++){
                {
                const char buf[3]={table,y*16+x,0};
                const char * inbyte;
                if (config.wide)
                    inbyte=buf;
                else 
                    inbyte=buf+1;

                memcpy(&inbuf->buf[inbuf->index],inbyte,(config.wide?3:2));
                }

                UChar str_utf16[17]={0};
                UChar* str_utf16_ptr=str_utf16+2;
                size_t length_utf16;
                switch(config.backend){

                    #ifdef ENABLE_ICONV
                    case ICONV: {
                        size_t inbytes_left=inbuf->index+(config.wide?2:1);
                        size_t outbytes_left=15*sizeof(UChar);
                        char * inbuf_ptr=inbuf->buf;
                        UChar* str_utf16_ptr_bak=str_utf16_ptr;

                        size_t result=iconv(
                            config.converter,
                            &inbuf_ptr,
                            &inbytes_left,
                            (char**)&str_utf16_ptr_bak,
                            &outbytes_left
                        );
                        length_utf16=15-outbytes_left/sizeof(UChar);

                        if(result == (size_t) -1 ){
                            if (errno == EINVAL) err=U_TRUNCATED_CHAR_FOUND;
                            else if (errno == EILSEQ) err=U_ILLEGAL_CHAR_FOUND;
                            else err=U_STANDARD_ERROR_LIMIT;
                        }
                            else err=U_ZERO_ERROR;
                    }
                    break;
                    #endif
                    #ifdef ENABLE_LIBICONV
                    case LIBICONV: {
                        size_t inbytes_left=inbuf->index+(config.wide?2:1);
                        size_t outbytes_left=15*sizeof(UChar);
                        char * inbuf_ptr=inbuf->buf;
                        UChar* str_utf16_ptr_bak=str_utf16_ptr;

                        size_t result=libiconv(
                            config.converter,
                            &inbuf_ptr,
                            &inbytes_left,
                            (char**)&str_utf16_ptr_bak,
                            &outbytes_left
                        );
                        length_utf16=15-outbytes_left/sizeof(UChar);

                        if(result == (size_t) -1 ){
                            if (errno == EINVAL) err=U_TRUNCATED_CHAR_FOUND;
                            else if (errno == EILSEQ) err=U_ILLEGAL_CHAR_FOUND;
                            else err=U_STANDARD_ERROR_LIMIT;
                        }
                            else err=U_ZERO_ERROR;
                    }
                    break;
                    #endif
                    case ICU:
                        err=U_ZERO_ERROR;
                        length_utf16=ucnv_toUChars (
                            config.converter,
                            str_utf16_ptr,
                            15,
                            inbuf->buf,
                            inbuf->index+(config.wide?2:1),
                            &err
                        );
                    break;
                    case LOCALE:
                    {
                        size_t bytes_converted=0;
                        length_utf16=0;
                        //UChar* str_utf16_ptr_bak=str_utf16_ptr;
                        mbstate_t mbstate={0};
                        bool done=false;
                        err=U_ZERO_ERROR;
                        while (!done && bytes_converted<inbuf->index+(config.wide?2:1)){
                            size_t result=mbrtoc16(
                                &str_utf16_ptr[length_utf16],
                                &inbuf->buf[bytes_converted],
                                inbuf->index+(config.wide?2:1)-bytes_converted,
                                &mbstate
                            );
                            switch (result){
                                case -3:
                                    length_utf16++;
                                break;
                                case -2:
                                    err=U_TRUNCATED_CHAR_FOUND;
                                    done=true;
                                break;
                                case -1:
                                    err=U_ILLEGAL_CHAR_FOUND;
                                    done=true;
                                break;
                                case 0:
                                    result=1;
                                default:
                                    length_utf16++;
                                    bytes_converted+=result;
                                break;


                            }
                        }
                        }
                    break;
                    #ifdef ENABLE_GCONV
                    case GCONV: {
                        struct __gconv_step_data step_data={
                            .__outbuf=str_utf16_ptr,
                            .__outbufend=str_utf16_ptr+15,

                            
                        };
                        gconv_nonsense * gconv=config.converter;
                        size_t inbytes_left=inbuf->index+(config.wide?2:1);
                        char * inbuf_ptr=inbuf->buf;
                        size_t written;
                        /*gconv->gconv(
                            &gconv->step,
                            &step_data,
                            &inbuf_ptr,
                            inbuf_ptr+inbuf->index+(config.wide?2:1),
                            &written,
                            0,0
                        );*/
                    } break;
                    #endif
                    case MAPPING_FILE:{
                        char out_buf_utf8[31];
                        size_t outlen;
                        convert_result r=convert(
                            *(MappingTable*)config.converter,
                            inbuf->buf,
                            inbuf->index+(config.wide?2:1),
                            out_buf_utf8,
                            31,
                            &outlen
                        );
                        if (r==CONVERSION_OK){
                            int32_t length_utf16_i;
                            err=U_ZERO_ERROR;
                            u_strFromUTF8(
                                str_utf16_ptr,
                                15,
                                &length_utf16_i,
                                out_buf_utf8,
                                outlen,
                                &err
                            );
                            length_utf16=length_utf16_i;
                        } else {
                            static const UErrorCode error_conversion[]={
                                [CONVERSION_OK]=U_ZERO_ERROR,
                                [INVALID_CHARACTER]=U_ILLEGAL_CHAR_FOUND,
                                [INCOMPLETE_CHARACTER]=U_TRUNCATED_CHAR_FOUND,
                                [BUFFER_NOT_BIG_ENOUGH]=U_STANDARD_ERROR_LIMIT
                            };
                            err=error_conversion[r];
                            length_utf16=0;
                        }
                        

                    }
                    break;
                    default:
                        fprintf(stderr, "Backend not compiled into the binary\n");
                }
                format printf("\e8\e[%dB\e[%dC",y+1,x*2+2);
                if (err==U_INVALID_CHAR_FOUND ||
                    err==U_ILLEGAL_CHAR_FOUND ||
                    err==U_ILLEGAL_ESCAPE_SEQUENCE ||
                    err==U_UNSUPPORTED_ESCAPE_SEQUENCE || 
                    find_predicate_in_string(str_utf16_ptr,u_isundefined,length_utf16)) 
                    format attrPrintSpace(attribute_red_background);
                else if (err==U_TRUNCATED_CHAR_FOUND)
                    format attrPrintSpace(attribute_green_background);
                else if (U_FAILURE(err) ) 
                    format attrPrintMessage(attribute_yellow_background,u_errorName(err));
                else {
                    const UChar *tmp;
                    if(
                        !config.control_codes_raw && 
                        (tmp=find_predicate_in_string(str_utf16_ptr,u_iscntrl,length_utf16))
                    ){
                        if (config.verbose_control_codes_and_whitespace)
                            format attrPrintCodepointAsHex(attribute_bright_blue_background, *tmp);
                        else 
                            format attrPrintSpace(attribute_blue_background);
                    }
                    else if(
                        !config.control_codes_raw && 
                        config.verbose_control_codes_and_whitespace && 
                        (tmp=find_predicate_in_string(str_utf16_ptr, u_isUWhiteSpace, length_utf16)) && 
                        (*tmp != ' ')
                    )
                        format attrPrintCodepointAsHex(attribute_light_gray_background, *tmp);
                    else {
                        char out_buf_utf8[33];
                        UChar32 c;
                        U16_GET(str_utf16_ptr, 0,0,length_utf16, c);
                        format if (u_getCombiningClass(c) > 0){
                            *--str_utf16_ptr=u'◌';
                            length_utf16++;
                        }
                        u_strToUTF8(
                            out_buf_utf8,
                            33, 
                            NULL,
                            str_utf16_ptr,
                            length_utf16, 
                            &idc
                        );
                        
                        if (!config.no_format_bool) {
                            UBool isPUA=(find_predicate_in_string(str_utf16_ptr,u_isPUA, length_utf16)!=NULL);
                            attrPrint(isPUA?attribute_magenta_background:attribute_default_background, out_buf_utf8);
                        }
                        else 
                            printf("%s", out_buf_utf8);
                            
                    }
                }
                
            }
        }

        format printf("\e[0m\n\n");
        format printAllMessages();


        if(config.interactive && table != config.to_table) {
            format printf("\n[q]: ");
            char c;
            while (((c=getchar()) != '\n') && (c !='q'));
            if (c=='q') {
                break;
            }
            format printf("\n");
        }
    }

}

int main(int argc, char * argv[]){
    inbuf_type * inbuf=malloc(8*sizeof(char)+sizeof(size_t)*3);
    *inbuf=(inbuf_type){
        .capacity=8,
        .index=0
    };

    Config config=CreateConfig(argc, argv, inbuf);
    int return_code=0;

    if (config.fail) return_code=1;
    else if (!config.help){
        print_fonttest(config, inbuf);
        switch (config.backend){
            #ifdef ENABLE_ICONV
            case ICONV: 
                iconv_close(config.converter);
            break;
            #endif
            #ifdef ENABLE_LIBICONV
            case LIBICONV: 
                libiconv_close(config.converter);
            break;
            #endif
            case ICU:
                ucnv_close(config.converter);
            break;
            #ifdef ENABLE_GCONV
            case GCONV:{
                gconv_nonsense * gconv = config.converter;
                gconv->gconv_end(&gconv->step);
                dlclose(gconv->shared_object);
                free(gconv);

            } break;
            #endif

                
        }
    }
    free(inbuf);
    return return_code;
}
