// Minimal libei sender client: connects to an EIS socket (e.g. gamescope-0-ei)
// and injects pointer/button/keyboard events. Isolated to that compositor.
//
// Usage: eidriver <socketpath> <cmd>...   where cmd is one of:
//   abs <x> <y>        absolute pointer move (logical px, within device region)
//   rel <dx> <dy>      relative pointer move
//   btn <code> <0|1>   button (code: 272=LEFT 273=RIGHT), 1=press 0=release
//   click              left press+release
//   key <kc> <0|1>     key by keycode, 1=press 0=release
//   tap <kc>           key press+release
//   sleep <ms>         wait
#include <libei.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

static struct ei *ei;
static struct ei_device *dev;

static void pump(int ms) {
    int fd = ei_get_fd(ei);
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        poll(&pfd, 1, 10);
        ei_dispatch(ei);
        struct ei_event *e;
        while ((e = ei_get_event(ei))) {
            enum ei_event_type t = ei_event_get_type(e);
            if (t == EI_EVENT_SEAT_ADDED) {
                struct ei_seat *seat = ei_event_get_seat(e);
                ei_seat_bind_capabilities(seat,
                    EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE,
                    EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_KEYBOARD, NULL);
                fprintf(stderr, "[ei] seat added, bound caps\n");
            } else if (t == EI_EVENT_DEVICE_ADDED) {
                dev = ei_device_ref(ei_event_get_device(e));
                fprintf(stderr, "[ei] device added\n");
            } else if (t == EI_EVENT_DEVICE_RESUMED) {
                if (!dev) dev = ei_device_ref(ei_event_get_device(e));
                fprintf(stderr, "[ei] device RESUMED (ready)\n");
            } else if (t == EI_EVENT_DISCONNECT) {
                fprintf(stderr, "[ei] DISCONNECTED by server\n");
                exit(3);
            }
            ei_event_unref(e);
        }
        if (ms < 0 && dev) {                 // wait-for-ready mode
            // need RESUMED; rely on caller checking dev + a resumed flag via region
            return;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        long el = (now.tv_sec-start.tv_sec)*1000 + (now.tv_nsec-start.tv_nsec)/1000000;
        if (ms >= 0 && el >= ms) return;
    }
}

static void frame(void){ ei_device_frame(dev, ei_now(ei)); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "need socketpath\n"); return 1; }
    const char *sock = argv[1];

    ei = ei_new(NULL);
    ei_configure_name(ei, "skyrim-driver");
    if (ei_setup_backend_socket(ei, sock) != 0) {
        fprintf(stderr, "failed to connect to EIS socket %s\n", sock);
        return 2;
    }
    fprintf(stderr, "[ei] connecting to %s ...\n", sock);

    // handshake: pump until a device is resumed (or timeout)
    bool ready = false;
    int fd = ei_get_fd(ei);
    struct timespec start, now; clock_gettime(CLOCK_MONOTONIC,&start);
    while (!ready) {
        struct pollfd pfd = { .fd=fd, .events=POLLIN };
        poll(&pfd, 1, 50);
        ei_dispatch(ei);
        struct ei_event *e;
        while ((e = ei_get_event(ei))) {
            enum ei_event_type t = ei_event_get_type(e);
            if (t == EI_EVENT_SEAT_ADDED) {
                struct ei_seat *seat = ei_event_get_seat(e);
                ei_seat_bind_capabilities(seat,
                    EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE,
                    EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_KEYBOARD, NULL);
                fprintf(stderr, "[ei] seat added, bound caps\n");
            } else if (t == EI_EVENT_DEVICE_ADDED) {
                dev = ei_device_ref(ei_event_get_device(e));
                fprintf(stderr, "[ei] device added\n");
            } else if (t == EI_EVENT_DEVICE_RESUMED) {
                if (!dev) dev = ei_device_ref(ei_event_get_device(e));
                ready = true;
                fprintf(stderr, "[ei] device RESUMED (ready)\n");
            } else if (t == EI_EVENT_DISCONNECT) {
                fprintf(stderr, "[ei] DISCONNECTED during handshake\n"); return 3;
            }
            ei_event_unref(e);
        }
        clock_gettime(CLOCK_MONOTONIC,&now);
        long el=(now.tv_sec-start.tv_sec)*1000+(now.tv_nsec-start.tv_nsec)/1000000;
        if (el > 5000) { fprintf(stderr,"[ei] timeout waiting for device\n"); return 4; }
    }

    // report regions (for absolute coords)
    for (size_t i=0;;i++){
        struct ei_region *r = ei_device_get_region(dev, i);
        if (!r) break;
        fprintf(stderr, "[ei] region %zu: %gx%g +%g+%g\n", i,
            (double)ei_region_get_width(r),(double)ei_region_get_height(r),
            (double)ei_region_get_x(r),(double)ei_region_get_y(r));
    }

    ei_device_start_emulating(dev, 1);

    // execute commands
    for (int i=2;i<argc;) {
        const char *c = argv[i++];
        if (!strcmp(c,"abs") && i+1<argc) {
            double x=atof(argv[i++]), y=atof(argv[i++]);
            ei_device_pointer_motion_absolute(dev,x,y); frame();
            fprintf(stderr,"[ei] abs %g %g\n",x,y);
        } else if (!strcmp(c,"rel") && i+1<argc) {
            double dx=atof(argv[i++]), dy=atof(argv[i++]);
            ei_device_pointer_motion(dev,dx,dy); frame();
            fprintf(stderr,"[ei] rel %g %g\n",dx,dy);
        } else if (!strcmp(c,"btn") && i+1<argc) {
            uint32_t b=atoi(argv[i++]); int p=atoi(argv[i++]);
            ei_device_button_button(dev,b,p); frame();
        } else if (!strcmp(c,"click")) {
            ei_device_button_button(dev,272,true); frame();
            pump(40);
            ei_device_button_button(dev,272,false); frame();
            fprintf(stderr,"[ei] click\n");
        } else if (!strcmp(c,"key") && i+1<argc) {
            uint32_t k=atoi(argv[i++]); int p=atoi(argv[i++]);
            ei_device_keyboard_key(dev,k,p); frame();
        } else if (!strcmp(c,"tap") && i<argc) {
            uint32_t k=atoi(argv[i++]);
            ei_device_keyboard_key(dev,k,true); frame(); pump(40);
            ei_device_keyboard_key(dev,k,false); frame();
        } else if (!strcmp(c,"sleep") && i<argc) {
            pump(atoi(argv[i++]));
        } else {
            fprintf(stderr,"unknown cmd: %s\n",c);
        }
        pump(30);
    }
    pump(150);                 // flush
    ei_device_stop_emulating(dev);
    pump(80);
    ei_unref(ei);
    return 0;
}
