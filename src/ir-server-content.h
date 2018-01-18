// various external binary files linked in later
extern const char _binary_ir_html_start;
extern const char _binary_ir_html_size;
extern const char _binary_radio_0_75x_png_start;
extern const char _binary_radio_0_75x_png_size;
extern const char _binary_radio_1x_png_start;
extern const char _binary_radio_1x_png_size;
extern const char _binary_radio_2_6x_png_start;
extern const char _binary_radio_2_6x_png_size;
extern const char _binary_radio_2x_png_start;
extern const char _binary_radio_2x_png_size;
extern const char _binary_radio_4x_png_start;
extern const char _binary_radio_4x_png_size;
extern const char _binary_radio_5_3x_png_start;
extern const char _binary_radio_5_3x_png_size;
#ifdef EASTEREGG
extern const char _binary_easteregg_png_start;
extern const char _binary_easteregg_png_size;
#endif

// list of embedded files to serve directly
typedef struct content_struct { const char *url; const char *headers; const char *body; unsigned int len; } content;
static const content contents[] = {
    { "/", "Content-Type: text/html\r\n", &_binary_ir_html_start, (unsigned int)&_binary_ir_html_size },
    { "/ir.html", "Content-Type: text/html\r\n", &_binary_ir_html_start, (unsigned int)&_binary_ir_html_size },
    { "/radio-0-75x.png", "Content-Type: image/png\r\n", &_binary_radio_0_75x_png_start, (unsigned int)&_binary_radio_0_75x_png_size },
    { "/radio-1x.png", "Content-Type: image/png\r\n", &_binary_radio_1x_png_start, (unsigned int)&_binary_radio_1x_png_size },
    { "/radio-2x.png", "Content-Type: image/png\r\n", &_binary_radio_2x_png_start, (unsigned int)&_binary_radio_2x_png_size },
    { "/radio-2-6x.png", "Content-Type: image/png\r\n", &_binary_radio_2_6x_png_start, (unsigned int)&_binary_radio_2_6x_png_size },
    { "/radio-4x.png", "Content-Type: image/png\r\n", &_binary_radio_4x_png_start, (unsigned int)&_binary_radio_4x_png_size },
    { "/radio-5-3x.png", "Content-Type: image/png\r\n", &_binary_radio_5_3x_png_start, (unsigned int)&_binary_radio_5_3x_png_size },
#ifdef EASTEREGG
    { "/hidden/easteregg", "Content-Type: image/png\r\n", &_binary_easteregg_png_start, (unsigned int)&_binary_easteregg_png_size },
#endif
    { NULL, NULL, NULL, 0 }
};
