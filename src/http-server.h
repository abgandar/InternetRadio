#include <sys/types.h>
#include <sys/socket.h>     // for sockaddr etc
#include <netinet/in.h>

// some return codes
enum retcode_enum {
    FILE_NOT_FOUND =    -7,
    BUFFERED =          -6,
    BUFFER_OVERFLOW =   -5,
    WRITE_DATA =        -4,
    READ_DATA =         -3,
    READ_WRITE_DATA =   -2,
    CLOSE_SOCKET =      -1,
    SUCCESS =            0
};

// some HTTP status codes
enum statuscode_enum {
    HTTP_OK =                   200,
    HTTP_NOT_MODIFIED =         304,
    HTTP_REDIRECT =             308,
    HTTP_BAD_REQUEST =          400,
    HTTP_FORBIDDEN =            403,
    HTTP_NOT_FOUND =            404,
    HTTP_NOT_ALLOWED =          405,
    HTTP_TOO_LARGE =            413,
    HTTP_SERVER_ERROR =         500,
    HTTP_NOT_IMPLEMENTED =      501,
    HTTP_SERVICE_UNAVAILABLE =  503
};

// request flags (bitfield, assign powers of 2!)
enum req_flags_enum {
    REQ_NONE =       0,
    REQ_CRLF =       1,
    REQ_CHUNKED =    2,
    REQ_CLOSE =      4,
    REQ_SHUTDOWN =   8
};

// request state
enum state_enum {
    STATE_NEW,
    STATE_HEAD,
    STATE_BODY,
    STATE_TAIL,
    STATE_READY,
    STATE_FINISH
};

// request version
enum version_enum {
    V_UNKNOWN,
    V_10,
    V_11
};

// request method
enum method_enum {
    M_UNKNOWN,
    M_OPTIONS,
    M_GET,
    M_HEAD,
    M_POST,
    M_PUT,
    M_DELETE,
    M_TRACE,
    M_CONNECT
};

// memory flags
enum wbchain_flags_enum {
    MEM_FD   =      1,      // type is an FD
    MEM_PTR  =      2,      // type is a data pointer
    MEM_KEEP =      4,      // keep open/untouched when done
    FD_KEEP  =      4,
    MEM_FREE =      8,      // close/free when done
    FD_CLOSE =      8,
    MEM_COPY =      16      // copy buffer when passed in
};

// write buffer chain entry
struct wbchain_struct {
    struct wbchain_struct *next;
    enum wbchain_flags_enum flags;
    int len;
    off_t offset;
    union { char *data; int fd; } payload;
};

// an active request
typedef struct req_struct {
    int fd;                     // socket associated with this request
    struct sockaddr_storage rip;// sockaddr of the remote client
    socklen_t riplen;           // length of the sockaddr
    char *data;                 // data buffer pointer
    unsigned int max, len;      // max allocated length, current length
    struct wbchain_struct *wb;  // pointer to the head of the write buffer
    unsigned int rl, cl;        // total request length parsed, total body content length
    time_t time;                // last time the request was active
    char *ver, *meth,
         *url, *query, *host,
         *head, *body, *tail;   // request pointers into data buffer
    enum state_enum state;      // state of this request
    enum req_flags_enum flags;  // flags for this request
    enum version_enum version;  // HTTP version of request
    enum method_enum method;    // enumerated method
} req;

// dynamic file
struct content_struct;
struct dynamic_content_struct {
    int (*handler)( req*, const struct content_struct *cs );
    void* userdata;
};

// embedded file
struct embedded_content_struct {
    const char *headers;
    const char *body;
    unsigned int len;
};

// flags for disk content
enum disk_content_flags_enum {
    DISK_LIST_DIRS = 1
};

// disk file
struct disk_content_struct {
    const char *www_dir;
    const char* dir_index;
    enum disk_content_flags_enum flags;
};

// flags for content
enum content_flags_enum {
    CONT_NONE = 0,
    CONT_EMBEDDED = 1,
    CONT_DISK = 2,
    CONT_DYNAMIC = 4,
    CONT_STOP = 8,
    CONT_PREFIX_MATCH = 16,
    CONT_DIR_MATCH = 32
};

// content list entry
struct content_struct {
    const char *host;
    const char *url;
    enum content_flags_enum flags;
    union {
        struct embedded_content_struct embedded;
        struct dynamic_content_struct dynamic;
        struct disk_content_struct disk;
    } content;
};

// convenience macros for defining content list entries
#define CONTENT_DISK(host, url, flags, dir, index, dirflags)     { host, url, CONT_DISK | flags, { .disk = { dir, index, dirflags } } }
#define CONTENT_DYNAMIC(host, url, flags, handler, userarg)      { host, url, CONT_DYNAMIC | flags, { .dynamic = { handler, userarg } } }
#define CONTENT_EMBEDDED(host, url, flags, header, data, size)   { host, url, CONT_EMBEDDED | flags, { .dynamic = { header, data, size } } }
#define CONTENT_END { NULL, NULL }

// some common MIME types (note: extensions must be backwards for faster matching later!)
struct mimetype_struct {
    const char *ext;
    const char *mime;
};

// flags for configuration
enum config_flags_enum {
    CONF_CLEAN_URL = 1
};

// server configuration
struct server_config_struct {
    const char* unpriv_user;                    // unpriviliged user to drop to after binding if started as root
    const char* chroot;                         // directory to chroot into when running the server
    enum config_flags_enum flags;               // yes/no flags
    const char* extra_headers;                  // extra headers to send with all replies
    const char* ip;                             // IP address of interface to bind to
    const char* ip6;                            // IP6 address of interface to bind to
    short port;                                 // port to bind to
    unsigned int max_req_len, max_head_len,
                 max_body_len, max_wb_len;      // Maximum allowed sizees in bytes of request, headers, body, write buffer (hard limit for writes is twice this)
    unsigned int max_connections;               // maximum number of total connections
    unsigned int max_client_conn;               // max number of connections per client
    unsigned int timeout;                       // timeout in seconds before idle connections are closed
    const struct content_struct *contents;      // web server contents definitions
    const struct mimetype_struct *mimetypes;    // mapping extensions to mime types
};

// set server config to defaults
void http_server_config_defaults( struct server_config_struct *config );

// update server config from command line
void http_server_config_argv( int *argc, char ***argv, struct server_config_struct *config );

// server main loop
int http_server_main( const struct server_config_struct *config );

// write a response with given body and headers (must include the initial HTTP response header)
// automatically adds Date header and respects HEAD requests
// if body is non-NULL, it is sent as a string with appropriate Content-Length header
// if body is NULL, and bodylen is non-null, the value is sent, expecting caller to send the data on its own
// flag is a memory flag for the body, see bwrite.
int write_response( req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen, const enum wbchain_flags_enum flag );

// get matching header value from the request without leading whitespace, skipping skip entries (or NULL if not found)
// name must be of the form "Date:" (including colon)
const char* get_header_field( const req *c, const char* name, unsigned int skip );

// handle a redirect. Removes as much of the full request URL as matches the content url pattern,
// then appends the remainder to the string pointed to by userdata
int handle_redirect( req *c, const struct content_struct *cs );
