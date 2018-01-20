#include <sys/types.h>

// Handle each request in a CGI query string
int handleQuery( char *arg );

// Close connection to MPD
void disconnectMPD( );

// initialize output buffer and print HTTP/CGI headers
int output_start( char **obuf, size_t *obuf_size );

// finish outputting results, general stats, and close the output buffer
int output_end( );

// reset buffered output and output an error instead, then close the output buffer
int error( const int code, const char* msg, const char* message );

