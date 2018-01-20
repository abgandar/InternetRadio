// special URI handler list entry
struct handler_struct { const char *url; int(*handler)(const req*); };

// embedded file list entry
struct content_struct { const char *url; const char *headers; const char *body; unsigned int len; };

// some return codes
enum retcode_enum { CLOSE_SOCKET = -1, SUCCESS = 0, WAIT_FOR_DATA = 1 };

// some HTTP status codes
enum statuscode_enum {
    HTTP_OK = 200,
    HTTP_NOT_MODIFIED = 304,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_NOT_ALLOWED = 405,
    HTTP_TOO_LARGE = 413,
    HTTP_SERVER_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
    HTTP_SERVICE_UNAVAILABLE = 503
};

// request state
enum state_enum { STATE_NEW, STATE_HEAD, STATE_BODY, STATE_TAIL, STATE_READY, STATE_FINISH };

// request flags (bitfield, assign powers of 2!)
enum flags_enum { FL_NONE = 0, FL_CRLF = 1, FL_CHUNKED = 2, FL_CLOSE = 4 };

// request version
enum version_enum { V_UNKNOWN, V_10, V_11 };

// request method
enum method_enum { M_UNKNOWN, M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT };

// an active request
typedef struct req_struct {
    int fd;                 // socket associated with this request
    char *data;             // data buffer pointer
    unsigned int max, len;  // max allocated length, current length
    unsigned int rl, cl;    // total request length parsed, total body content length
    char *version, *method, *url, *head, *body, *tail;      // request pointers into data buffer
    enum state_enum s;      // state of this request
    enum flags_enum f;      // flags for this request
    enum version_enum v;    // HTTP version of request
    enum method_enum m;     // enumerated method
} req;


// server main loop
int http_server_main( int argc, char *argv[] );

// write a response with given body and headers (must include the initial HTTP response header)
// automatically adds Date header and respects HEAD requests
// if body is non-NULL, it is sent as a string with appropriate Content-Length header
// if body is NULL, and bodylen is non-null, the value is sent, expecting caller to send the data on its own
void write_response( const req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen );

// get first matching header value from the request without leading whitespace (or NULL if not found)
// name must be of the form "Date:" (including colon)
const char* get_header_field( const req *c, const char* name );
