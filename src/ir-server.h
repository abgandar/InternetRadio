#include <sys/types.h>

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
    HTTP_BAD_REQUEST =          400,
    HTTP_FORBIDDEN =            403,
    HTTP_NOT_FOUND =            404,
    HTTP_NOT_ALLOWED =          405,
    HTTP_TOO_LARGE =            413,
    HTTP_SERVER_ERROR =         500,
    HTTP_NOT_IMPLEMENTED =      501,
    HTTP_SERVICE_UNAVAILABLE =  503
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

// request flags (bitfield, assign powers of 2!)
enum flags_enum {
    FL_NONE =       0,
    FL_CRLF =       1,
    FL_CHUNKED =    2,
    FL_CLOSE =      4,
    FL_SHUTDOWN =   8
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
enum memflags_enum {
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
    enum memflags_enum f;
    int len;
    off_t offset;
    union { char *data; int fd; } payload;
};

// an active request
typedef struct req_struct {
    int fd;                     // socket associated with this request
    char *data;                 // data buffer pointer
    unsigned int max, len;      // max allocated length, current length
    struct wbchain_struct *wb;  // pointer to the head of the write buffer
    unsigned int rl, cl;        // total request length parsed, total body content length
    time_t time;                // last time the request was active
    char *version, *method,
         *url, *query, *head,
         *body, *tail;          // request pointers into data buffer
    enum state_enum s;          // state of this request
    enum flags_enum f;          // flags for this request
    enum version_enum v;        // HTTP version of request
    enum method_enum m;         // enumerated method
} req;

// special URI handler list entry
struct handler_struct {
    const char *url;
    int (*handler)( req* );
};

// embedded file list entry
struct content_struct {
    const char *url;
    const char *headers;
    const char *body;
    unsigned int len;
};

// some common MIME types (note: extensions must be backwards for faster matching later!)
struct mimetype_struct {
    const char *ext;
    const char *mime;
};

// server configuration
struct server_config_struct {
    const char* unpriv_user;                    // unpriviliged user to drop to after binding if started as root
    const char* www_dir;                        // directory where to look for files (should end in / for safety)
    const char* dir_index;                      // directory index file used when requesting a directory from disk
    char dir_list;                              // try to list directories if requested
    const char* extra_headers;                  // extra headers to send with all replies
    const char* ip;                             // IP address of interface to bind to
    short port;                                 // port to bind to
    unsigned int max_req_len, max_rep_len;      // Maximum allowed size of a request (1 MB), Maximum allowed size of the write buffer (10 MB)
    unsigned int timeout;                       // timeout in seconds before idle connections are closed
    const struct content_struct *contents;      // static embedded file content
    const struct handler_struct *handlers;      // dynamic content handlers
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
int write_response( req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen, const enum memflags_enum flag );

// get matching header value from the request without leading whitespace, skipping skip entries (or NULL if not found)
// name must be of the form "Date:" (including colon)
const char* get_header_field( const req *c, const char* name, unsigned int skip );
