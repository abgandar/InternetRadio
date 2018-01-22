#include <sys/types.h>

// some return codes
enum retcode_enum {
    FILE_NOT_FOUND =    -6,
    BUFFERED =          -5,
    BUFFER_OVERFLOW =   -4,
    WRITE_DATA =        -3,
    READ_DATA =         -2,
    CLOSE_SOCKET =      -1,
    SUCCESS =            0
};

// some HTTP status codes
enum statuscode_enum {
    HTTP_OK =                   200,
    HTTP_NOT_MODIFIED =         304,
    HTTP_BAD_REQUEST =          400,
    HTTP_NOT_FOUND =            404,
    HTTP_NOT_ALLOWED =          405,
    HTTP_TOO_LARGE =            413,
    HTTP_SERVER_ERROR =         500,
    HTTP_NOT_IMPLEMENTED =      501,
    HTTP_SERVICE_UNAVAILABLE =  503
};

// request state
enum state_enum { STATE_NEW, STATE_HEAD, STATE_BODY, STATE_TAIL, STATE_READY, STATE_FINISH };

// request flags (bitfield, assign powers of 2!)
enum flags_enum {
    FL_NONE =       0,
    FL_CRLF =       1,
    FL_CHUNKED =    2,
    FL_CLOSE =      4,
    FL_SHUTDOWN =   8
};

// request version
enum version_enum { V_UNKNOWN, V_10, V_11 };

// request method
enum method_enum { M_UNKNOWN, M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT };

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


// server main loop
int http_server_main( int argc, char *argv[] );

// write a response with given body and headers (must include the initial HTTP response header)
// automatically adds Date header and respects HEAD requests
// if body is non-NULL, it is sent as a string with appropriate Content-Length header
// if body is NULL, and bodylen is non-null, the value is sent, expecting caller to send the data on its own
// flag is a memory flag for the body, see bwrite.
int write_response( req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen, enum memflags_enum flag )

// get matching header value from the request without leading whitespace, skipping skip entries (or NULL if not found)
// name must be of the form "Date:" (including colon)
const char* get_header_field( const req *c, const char* name, unsigned int skip );

