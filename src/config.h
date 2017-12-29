#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define OUTPUT_BUFFER_SIZE      (3*1024*1024)               // output buffer size (3 MB for now)
#define REBOOT_WAIT             1                           // time in seconds to wait after sync()ing buffers
#define SYSTEMD                                             // Use systemd to reboot/shutdown
