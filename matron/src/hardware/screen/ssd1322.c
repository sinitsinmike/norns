#include "ssd1322.h"

static int spidev_fd = 0;
static bool display_dirty = false;
static bool should_turn_on = true;
static bool should_translate_color = false;
static uint8_t * spidev_buffer;
static struct gpiod_chip * gpio_0;
static struct gpiod_line * gpio_dc;
static struct gpiod_line * gpio_reset;
static cairo_surface_t * surface_pointer;
static pthread_t ssd1322_pthread_t;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int open_spi() {
    uint8_t mode = SPI_MODE_0;
    uint8_t bits_per_word = SPI0_BUS_WIDTH;
    uint8_t little_endian = 0;
    uint32_t speed_hz = 1200000000 / 64; // 18.75Mhz, 1200Mhz is the CPU speed.

    int fd = open(SPIDEV_0_0_PATH, O_RDWR | O_SYNC);

    if( fd < 0 ){
        fprintf(stderr, "(screen) couldn't open %s\n", SPIDEV_0_0_PATH);
        return -1;
    }

    int outcome = 0
    || ( ioctl(fd, SPI_IOC_WR_MODE, &mode)                   < 0 )
    || ( ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0 )
    || ( ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz)       < 0 )
    || ( ioctl(fd, SPI_IOC_WR_LSB_FIRST, &little_endian)     < 0 );
    if( outcome != 0 ){
       fprintf(stderr, "could not set SPI WR settings via IOC\n");
       close(fd);
       return -1;
    }

    return fd;
}

int ssd1322_write_command(uint8_t command, uint8_t data_len, ...) {
    va_list args;
    uint8_t cmd_buf[1];
    uint8_t data_buf[256];
    struct spi_ioc_transfer cmd_transfer = {0};
    struct spi_ioc_transfer data_transfer = {0};

    pthread_mutex_lock(&lock);

    if( spidev_fd <= 0 ){
        fprintf(stderr, "%s: spidev not yet opened\n", __func__);
        goto fail;
    }

    gpiod_line_set_value(gpio_dc, 0);

    cmd_buf[0] = command;
    cmd_transfer.tx_buf = (unsigned long) cmd_buf;
    cmd_transfer.len = (uint32_t) sizeof(cmd_buf);

    if( ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &cmd_transfer) < 0 ){
        fprintf(stderr, "%s: could not send command-message.\n", __func__);
        goto fail;
    }

    if( data_len > 0 ){
        gpiod_line_set_value(gpio_dc, 1);

        va_start(args, data_len);

        for( uint8_t i = 0; i < data_len; i++ ){
            data_buf[i] = va_arg(args, int);
        }

        va_end(args);

        data_transfer.tx_buf = (unsigned long) data_buf;
        data_transfer.len = (uint32_t) data_len;

        if( ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &data_transfer) < 0 ){
            fprintf(stderr, "%s: could not send data-message.\n", __func__);
            goto fail;
        }
    }

    pthread_mutex_unlock(&lock);
    return 0;
fail:
    pthread_mutex_unlock(&lock);
    return -1;
}

#define NUMARGS(...)  (sizeof((int[]){__VA_ARGS__}) / sizeof(int))
#define write_command_with_data(x, ...) \
    (ssd1322_write_command(x, NUMARGS(__VA_ARGS__), __VA_ARGS__))
#define write_command(x) \
    (ssd1322_write_command(x, 0, 0))

static void* ssd1322_thread_run(void * p){
    (void)p;

    static struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = (1/60) * 1e9,
    };

    while( spidev_buffer ){
        if( display_dirty ){
            ssd1322_refresh();
        }
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }

    return NULL;
}

void ssd1322_init() {

    if( pthread_mutex_init(&lock, NULL) != 0 ){
        fprintf(stderr, "%s: pthread_mutex_init failed\n", __func__);
        return;
    }

    spidev_buffer = calloc(8192, sizeof(uint8_t));
    if( spidev_buffer == NULL ){
        fprintf(stderr, "%s: couldn't allocate spidev_buffer\n", __func__);
    }

    spidev_fd = open_spi(SPIDEV_0_0_PATH);
    if( spidev_fd < 0 ){
        fprintf(stderr, "%s: couldn't open %s.\n", __func__, SPIDEV_0_0_PATH);
        return;
    }

    gpio_0 = gpiod_chip_open_by_name(SSD1322_DC_AND_RESET_GPIO_CHIP);
    gpio_dc = gpiod_chip_get_line(gpio_0, SSD1322_DC_GPIO_LINE);
    gpio_reset = gpiod_chip_get_line(gpio_0, SSD1322_RESET_GPIO_LINE);

    gpiod_line_request_output(gpio_dc, "D/C", 0);
    gpiod_line_request_output(gpio_reset, "RST", 0);

    // SSD1322 Reference Document (v1.2) P 16/60
    // "Keep this pin pull HIGH during normal operation"
    gpiod_line_set_value(gpio_reset, 1);

    // All values copied from fbtft-ssd1322.c from monome/linux repo.
    write_command(SSD1322_SET_DISPLAY_OFF);
    write_command(SSD1322_SET_DEFAULT_LINEAR_GRAY_SCALE);
    write_command_with_data(SSD1322_SET_OSCILLATOR_FREQUENCY, 0x91);
    write_command_with_data(SSD1322_SET_MULTIPLEX_RATIO, NORNS_MUX_RATIO);
    write_command_with_data(SSD1322_SET_DISPLAY_OFFSET, 0x00);
    write_command_with_data(SSD1322_SET_DISPLAY_START_LINE, 0x00);
    write_command_with_data(SSD1322_SET_VDD_REGULATOR, 0x01);
    write_command_with_data(SSD1322_SET_DISPLAY_ENHANCEMENT_A, 0xA0, 0xFD);
    write_command_with_data(SSD1322_SET_CONTRAST_CURRENT, 0x7F);
    write_command_with_data(SSD1322_MASTER_CURRENT_CONTROL, 0x0F);
    write_command_with_data(SSD1322_SET_PHASE_LENGTH, NORNS_PHASE_LENGTH);
    write_command_with_data(SSD1322_SET_PRECHARGE_VOLTAGE, 0x1F);
    write_command_with_data(SSD1322_SET_VCOMH_VOLTAGE, 0x04);
    write_command(SSD1322_SET_DISPLAY_MODE_NORMAL);

    if( platform() != PLATFORM_CM3 ){
        write_command_with_data(SSD1322_SET_DUAL_COMM_LINE_MODE, 0x04, 0x11);
    }
    else{
        write_command_with_data(SSD1322_SET_DUAL_COMM_LINE_MODE, 0x16, 0x11);
    }

    ssd1322_set_refresh_rate(75);

    // Do not turn display on until the first update has been called,
    // otherwise previous GDDRAM (or noise) will display before the
    // "hello" startup screen.

    // Start thread.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&ssd1322_pthread_t, &attr, &ssd1322_thread_run, NULL);
    pthread_attr_destroy(&attr);
}

void ssd1322_deinit(){
    if( spidev_fd > 0 ){
        // Drive RST low to turn off screen.
        gpiod_line_set_value(gpio_reset, 0);

        // Destroy file descriptors and handles.
        pthread_mutex_destroy(&lock);
        gpiod_line_release(gpio_reset);
        gpiod_line_release(gpio_dc);
        gpiod_chip_close(gpio_0);
        close(spidev_fd);

        free(spidev_buffer);
        spidev_buffer = NULL;
    }
}

void ssd1322_update(cairo_surface_t * surface, bool surface_may_have_color){
    display_dirty = true;
    surface_pointer = surface;
    should_translate_color = surface_may_have_color;
}

void ssd1322_refresh(){
    struct spi_ioc_transfer transfer = {0};

    if( spidev_fd <= 0 ){
        fprintf(stderr, "%s: spidev not yet opened.\n", __func__);
        return;
    }

    write_command_with_data(SSD1322_SET_COLUMN_ADDRESS, 28, 91);
    write_command_with_data(SSD1322_SET_ROW_ADDRESS, 0, 63);
    write_command(SSD1322_WRITE_RAM_COMMAND);

    if( should_turn_on ){
        write_command(SSD1322_SET_DISPLAY_ON);
        should_turn_on = 0;
    }

    pthread_mutex_lock(&lock);

    const uint32_t surface_w = cairo_image_surface_get_width(surface_pointer);
    const uint32_t surface_h = cairo_image_surface_get_height(surface_pointer);
    cairo_format_t surface_f = cairo_image_surface_get_format(surface_pointer);

    if(surface_w != 128 || surface_h != 64 || surface_f != CAIRO_FORMAT_ARGB32){
        fprintf(stderr, "%s: %ux%u = invalid surface size\n", __func__, surface_w, surface_h);
        goto early_return;
    }

    const uint32_t tx_len = surface_w * surface_h;
    const uint32_t * data = (const uint32_t *) cairo_image_surface_get_data(surface_pointer);

    if( should_translate_color ){
        // Preserve luminance of RGB when converting to grayscale. Use the
        // closest multiple of 16 to the fraction to scale the channels'
        // grayscale value. Use a multiple of 16 because a 4-bit grayscale
        // value should fit into the upper nibble of the 8-bit value. The
        // decimal approximation is out of 256: 80 + 160 + 16 = 256.
        for( uint32_t i = 0; i < tx_len; i += 8 ){
            uint8x8x4_t pixel = vld4_u8((const uint8_t *) (data + i));
            uint16x8_t r = vmull_u8(pixel.val[2], vdup_n_u8( 80)); // R * ~ 0.30
            uint16x8_t g = vmull_u8(pixel.val[1], vdup_n_u8(160)); // G * ~ 0.59
            uint16x8_t b = vmull_u8(pixel.val[0], vdup_n_u8( 16)); // B * ~ 0.11
            vst1_u8(spidev_buffer + i, vaddhn_u16(vaddq_u16(r,g), b));
        }
    }
    else{
        // If the surface has only been drawn to, we can guarantee that RGB are
        // all equal values representing a grayscale value. So, we can take any
        // of those channels arbitrarily.
        for( uint32_t i = 0; i < tx_len; i += 16 ){
            const uint8x16x4_t RGB = vld4q_u8((uint8_t *) (data + i));
            vst1q_u8(spidev_buffer + i, RGB.val[0]);
        }
    }

    gpiod_line_set_value(gpio_dc, 1);

    const uint32_t spidev_bufsize = 8192;
    const uint32_t n_transfers = tx_len / spidev_bufsize;
    for( uint32_t i = 0; i < n_transfers; i++ ){
        transfer.tx_buf = (unsigned long) (spidev_buffer + (i * spidev_bufsize));
        transfer.len = (uint32_t) tx_len / n_transfers;
        if( ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &transfer) < 0 ){
            fprintf(stderr, "%s: SPI data transfer %d of %d failed.\n",
                            __func__,               i,    n_transfers);
            goto early_return;
        }
    }

early_return:
    pthread_mutex_unlock(&lock);
    return;
}


void ssd1322_set_brightness(uint8_t b){
    write_command_with_data(SSD1322_SET_PRECHARGE_VOLTAGE, b);
}

void ssd1322_set_contrast(uint8_t c){
    write_command_with_data(SSD1322_SET_CONTRAST_CURRENT, c);
}

void ssd1322_set_display_mode(ssd1322_display_mode_t mode_offset){
    write_command(SSD1322_SET_DISPLAY_MODE_ALL_OFF + mode_offset);
}

void ssd1322_set_gamma(uint8_t *gs){
    write_command_with_data(
            SSD1322_SET_GRAYSCALE_TABLE, // GS0 is skipped.
            gs[0x1], gs[0x2], gs[0x3], gs[0x4], gs[0x5],
            gs[0x6], gs[0x7], gs[0x8], gs[0x9], gs[0xA],
            gs[0xB], gs[0xC], gs[0xD], gs[0xE], gs[0xF]
    );
    write_command(SSD1322_ENABLE_GRAYSCALE_TABLE);
}

void ssd1322_set_refresh_rate(uint8_t hz){
    // From the SSD1322 reference doc (rev 1.2, P 23/60):
    //
    // D = Clock-divide ratio, set by 0xA3 command [bits 0-3], Int range [1,16]
    // X = DCLCKs in current drive period. Default is 10 + GS15 value, i.e. 122
    // K = Phase-1 period + Phase-2 period + X.
    //
    // Frequency of Frame = Frequency of Oscillator (F)
    //                      ---------------------------
    //                           D * K * No. of Mux
    //
    // F has a range of 1.75Mhz through 2.13 Mhz, with a step size of 23.75Khz.
    // Derived from (2.13Mhz - 1.75Mhz) / 16 steps. (SSD1322, rev 1.2, P 50/60)
    //
    // Note: I can't see a reason for changing "K", or "No. of Mux" at this
    //       time, so controlling the refresh rate will primarily be done via
    //       altering the oscillator frequency and clock-divide ratio. If the
    //       max grayscale value (i.e. GS15, 112) is ever raised, this function
    //       will need to be rewritten.
    //
    //       We can find an approximate solution for values to give F and D, in
    //       order to have a frame frequency close to the argument "hz".
    const uint8_t x_constant = 10; // (SSD1322, rev 1.2, P 23/60)
    const uint8_t x = x_constant + SSD1322_GRAYSCALE_MAX_VALUE;
    const uint8_t p1 = SSD1322_PHASE_1_LENGTH_FROM_HEX(NORNS_PHASE_1_LENGTH);
    const uint8_t p2 = SSD1322_PHASE_2_LENGTH_FROM_HEX(NORNS_PHASE_2_LENGTH);
    const uint8_t k = p1 + p2 + x;
    const uint8_t mux_count = SSD1322_MUX_RATIO_FROM_HEX(NORNS_MUX_RATIO);

    static uint8_t past_solutions[256] = {};

    if( past_solutions[hz] == 0 ){
        // There MUST be a better algorithm for this, but for now it will just
        // brute-force the approximations, saving them for subsequent calls.
        double closest_solution = 0.0;
        for( uint8_t osc_div = 0; osc_div < 0xFF; osc_div++ ){
            double osc = (double) (osc_div >> 4);
            double div = (double) (osc_div & 0xF);
            osc = 1.75 + (osc *  0.02375);
            div = pow(2.0, div);

            double solution = osc / (div * (double) k * (double) mux_count);
            double mhz = (double) (hz * 0.000001);
            if( fabs(mhz - solution) < fabs(mhz - closest_solution) ){
                past_solutions[hz] = osc_div;
                closest_solution = solution;
            }
        }
    }

    uint8_t freq = past_solutions[hz];

    write_command_with_data(SSD1322_SET_OSCILLATOR_FREQUENCY, freq);
}

uint8_t* ssd1322_resize_buffer(size_t size){
    spidev_buffer = realloc(spidev_buffer, size);
    return spidev_buffer;
}