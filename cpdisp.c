static const char helptext[] = "\n\
Generate nice looking charts of character encodings within the terminal.\n\
\n\
Usage:\n\
    -h : print this help.\n\
    -w : print 2 byte table.\n\
    -d [filename] : load custom icu data file.\n\
    -i : require user input between pages (only if -w is enabled).\n\
    -r [from]:[to] : display only pages associated with this range of bytes.\n\
    -n : no format.\n\
    -N : no format and print control character raw.\n\
    -x [byte]:[byte]:[byte]... : prefix in hex.\n\
    -c : print hex code and name of control characters and whitespace characters.\n\
\n\
Legend:\n\
    Blue: Control Character\n\
    Red: Invalid Character\n\
    Green : Prefix of incomplete character\n\
    Purple/Dark Magenta: Private Use Character\n\
    Dark Yellow: Something I didn't expect\n\
\n";

#include <unicode/ucnv.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

static UErrorCode err=U_ZERO_ERROR , idc=U_ZERO_ERROR;

static const int attribute_red_background = 41;
static const int attribute_green_background=42;
static const int attribute_yellow_background=43;
static const int attribute_blue_background=44;
static const int attribute_magenta_background=45;
static const int attribute_light_gray_background=47;
static const int attribute_default_background=49;
static const int attribute_bright_blue_background = 104;


const UChar32 * find_predicate_in_string(const UChar32 * str, UBool (*predicate)(UChar32), size_t length ){
    for (size_t i=0; i<length; i++)
        if (predicate(str[i])) return &str[i];
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
        int32_t name_length = u_charName(
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
static const char * optstring = "wNhnd:x:r:i2c";

typedef struct {
    size_t capacity;
    size_t index;
    char buf[];
} inbuf_type;
typedef struct {
    UConverter * converter;
    uint8_t from_table, to_table;
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
    while ((opt=getopt(argc, argv, optstring))!=-1){
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
        }
    }
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
    

    
    if (U_FAILURE(err)){
        fprintf(stderr,"No such codepage %s\n", argv[optind]);
        config.fail=true;
        return config;
    }
    ucnv_setToUCallBack(
        config.converter,
        UCNV_TO_U_CALLBACK_STOP,
        NULL,
        NULL,
        NULL,
        &idc
    );
    return config;
}

typedef struct {
    int32_t length_utf16;
    int32_t length_utf32;
} make_strings_lengths;
make_strings_lengths make_strings(
    const Config config,
    inbuf_type * inbuf,
    UChar* str_utf16_ptr,
    UChar32 * str_utf32_ptr
){
    make_strings_lengths lengths;
    ucnv_reset(config.converter);
    err=U_ZERO_ERROR;
    lengths.length_utf16 = ucnv_toUChars (
        config.converter,
        str_utf16_ptr,
        15,
        inbuf->buf,
        inbuf->index+(config.wide?2:1),
        &err
    );
    if (U_SUCCESS(err)) u_strToUTF32(
        str_utf32_ptr,
        9,
        &lengths.length_utf32,
        str_utf16_ptr,
        lengths.length_utf16,
        &idc
    );
    return lengths;
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

                UChar str_utf16[17];
                UChar* str_utf16_ptr=str_utf16+2;
                UChar32 str_utf32[9];
                make_strings_lengths lengths = make_strings(
                    config,
                    inbuf,
                    str_utf16_ptr,
                    str_utf32
                );
                
                format printf("\e8\e[%dB\e[%dC",y+1,x*2+2);
                if (err==U_INVALID_CHAR_FOUND ||
                    err==U_ILLEGAL_CHAR_FOUND ||
                    err==U_ILLEGAL_ESCAPE_SEQUENCE ||
                    err==U_UNSUPPORTED_ESCAPE_SEQUENCE || 
                    !u_isdefined(*str_utf32)) 
                    format attrPrintSpace(attribute_red_background);
                else if (err==U_TRUNCATED_CHAR_FOUND)
                    format attrPrintSpace(attribute_green_background);
                else if (U_FAILURE(err) ) 
                    format attrPrintMessage(attribute_yellow_background,u_errorName(err));
                else {
                    const UChar32 *tmp;
                    if(
                        !config.control_codes_raw && 
                        (tmp=find_predicate_in_string(str_utf32,u_iscntrl,lengths.length_utf32))
                    ){
                        if (config.verbose_control_codes_and_whitespace)
                            format attrPrintCodepointAsHex(attribute_bright_blue_background, *tmp);
                        else 
                            format attrPrintSpace(attribute_blue_background);
                    }
                    else if(
                        !config.control_codes_raw && 
                        config.verbose_control_codes_and_whitespace && 
                        (tmp=find_predicate_in_string(str_utf32, u_isUWhiteSpace, lengths.length_utf32)) && 
                        (*tmp != ' ')
                    )
                        format attrPrintCodepointAsHex(attribute_light_gray_background, *tmp);
                    else {
                        char out_buf_utf8[33];
                            
                        format if (u_getCombiningClass(*str_utf32) > 0){
                            *--str_utf16_ptr=u'◌';
                            lengths.length_utf16++;
                        }
                        u_strToUTF8(
                            out_buf_utf8,
                            33, 
                            NULL,
                            str_utf16_ptr,
                            lengths.length_utf16, 
                            &idc
                        );
                        
                        if (!config.no_format_bool) {
                            UBool isPUA=(find_predicate_in_string(str_utf32,u_isPUA, lengths.length_utf32)!=NULL);
                            const UCharDirection direction = u_charDirection(*str_utf32);
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
        ucnv_close(config.converter);
    }
    free(inbuf);
    return return_code;
}
