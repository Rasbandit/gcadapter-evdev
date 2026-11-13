#include <gcadapter_evdev.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void sigint(int sig)
{
    recieved_sigint = true;
}

char* port_to_str(gc_adapter_t* adapter, int port)
{
    char* str = malloc(3);
    if(adapter->pad[port] != GC_PAD_NONE)
        snprintf(str, 4, "[%i]", port + 1);
    else
        snprintf(str, 4, "[ ]");
    return str;
}

bool write_button_event(int btn, struct input_event* event, int uinput_fd, bool down)
{
    memset(event, 0, sizeof(*event));
    (*event).type = EV_KEY;
    (*event).code = btn;
    (*event).value = down;
    write(uinput_fd, event, sizeof(*event));
    return true;
}

bool write_axis_event(gc_pad_axis_t axis, int target_axis_x, int target_axis_y, int x_shift_by, int y_shift_by, bool invert_y, struct input_event* event, int uinput_fd)
{
    memset(event, 0, sizeof(*event));
    (*event).type = EV_ABS;
    (*event).code = target_axis_x;
    // we shift the value by about a quarter of our 'deadzone'.
    (*event).value = axis.x + x_shift_by;
    write(uinput_fd, event, sizeof(*event));
    (*event).type = EV_ABS;
    (*event).code = target_axis_y;
    (*event).value = axis.y + y_shift_by;
    if(invert_y)
        (*event).value = 255 - (*event).value;
    write(uinput_fd, event, sizeof(*event));
    return true;
}

void initialize_port(gc_adapter_t* adapter, int port)
{
    int uinput_fd = open("/dev/uinput", O_WRONLY);
    
    if(uinput_fd < 0)
    {
        uinput_fd = open("/dev/input/uinput", O_WRONLY);
        
        if(uinput_fd < 0)
        {
            return;
        }
    }
    
    uinput[port] = uinput_fd;
    
    struct uinput_user_dev evdev_device;
    memset(&evdev_device, 0, sizeof(evdev_device));
    
    snprintf(evdev_device.name, UINPUT_MAX_NAME_SIZE, "GameCube Controller");
    evdev_device.id.bustype = BUS_VIRTUAL;
    evdev_device.id.vendor = GC_ADAPTER_USB_VID;
    evdev_device.id.product = GC_ADAPTER_USB_PID;
    evdev_device.id.version = LIBGCADAPTER_VERSION;
    
    ioctl(uinput[port], UI_SET_EVBIT, EV_KEY);
    
    for(int i = 0; i < (int)(sizeof(evdev_map_buttons) / sizeof(evdev_map_buttons[0])); i += 2)
        ioctl(uinput[port], UI_SET_KEYBIT, evdev_map_buttons[i]);
        
    ioctl(uinput[port], UI_SET_EVBIT, EV_ABS);
   
    // Sticks (ABS_X/Y, ABS_RX/RY): use actual GC hardware range ~28-228; no kernel deadzone
    int stick_axes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
    for(int i = 0; i < 4; i++)
    {
        ioctl(uinput[port], UI_SET_ABSBIT, stick_axes[i]);
        evdev_device.absmin[stick_axes[i]] = 28;
        evdev_device.absmax[stick_axes[i]] = 228;
        evdev_device.absfuzz[stick_axes[i]] = 2;
        evdev_device.absflat[stick_axes[i]] = 0;
    }
    // Triggers (ABS_Z/ABS_RZ): rest near 0 so need full 0-255 range; no deadzone
    int trigger_axes[] = { ABS_Z, ABS_RZ };
    for(int i = 0; i < 2; i++)
    {
        ioctl(uinput[port], UI_SET_ABSBIT, trigger_axes[i]);
        evdev_device.absmin[trigger_axes[i]] = 0;
        evdev_device.absmax[trigger_axes[i]] = 255;
        evdev_device.absfuzz[trigger_axes[i]] = 2;
        evdev_device.absflat[trigger_axes[i]] = 0;
    }
    
    write(uinput[port], &evdev_device, sizeof(evdev_device));
    ioctl(uinput[port], UI_DEV_CREATE);
    
    GC_PAD_RESET_STATE(last_state[port]);
    state[port] = adapter->state[port];
    write_axis_event(state[port].stick, ABS_X, ABS_Y, 0, 8, true, &event, uinput[port]);
    write_axis_event(state[port].c_stick, ABS_RX, ABS_RY, 2, -2, true, &event, uinput[port]);
    write_axis_event(state[port].triggers, ABS_Z, ABS_RZ, 0, 0, false, &event, uinput[port]);
}

void update_port(gc_adapter_t* adapter, int port)
{
    state[port] = adapter->state[port];
    bool written_this_poll = false;
    for(int i = 1; i < (int)(sizeof(evdev_map_buttons) / sizeof(evdev_map_buttons[0])); i += 2)
    {
        int value = -1;
        if(GC_PAD_BTN_DOWN(state[port], evdev_map_buttons[i]) && !(GC_PAD_BTN_DOWN(last_state[port], evdev_map_buttons[i])))
            value = 1;
        if(!(GC_PAD_BTN_DOWN(state[port], evdev_map_buttons[i])) && GC_PAD_BTN_DOWN(last_state[port], evdev_map_buttons[i]))
            value = 0;
        if(value != -1)
            written_this_poll = write_button_event(evdev_map_buttons[i - 1], &event, uinput[port], (bool)value);
    }
    if(!(GC_PAD_AXISES_EQUAL(state[port].stick, last_state[port].stick)))
        written_this_poll = write_axis_event(state[port].stick, ABS_X, ABS_Y, 0, 8, true, &event, uinput[port]);
    if(!(GC_PAD_AXISES_EQUAL(state[port].c_stick, last_state[port].c_stick)))
        written_this_poll = write_axis_event(state[port].c_stick, ABS_RX, ABS_RY, 2, -2, true, &event, uinput[port]);
    if(!(GC_PAD_AXISES_EQUAL(state[port].triggers, last_state[port].triggers)))
        written_this_poll = write_axis_event(state[port].triggers, ABS_Z, ABS_RZ, 0, 0, false, &event, uinput[port]);
    if(written_this_poll)
    {
        memset(&event, 0, sizeof(event));
        event.type = EV_SYN;
        event.code = SYN_REPORT;
        write(uinput[port], &event, sizeof(event));
    }
    last_state[port] = state[port];
}

void close_port(gc_adapter_t* adapter, int port)
{
    ioctl(uinput[port], UI_DEV_DESTROY);
    close(uinput[port]);
    uinput[port] = 0;
}

int main(int argc, char** argv)
{
    int adapter_index = 0;

    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "--adapter") == 0 && i + 1 < argc)
        {
            adapter_index = atoi(argv[++i]);
        }
    }

    printf("Wii U GameCube Adapter interface for evdev (libgcadapter %s) [adapter %i]\n",
        LIBGCADAPTER_VERSION_STR, adapter_index);

    if(LIBGCADAPTER_VERSION != gc_adapter_get_version())
    {
        printf("Invalid version of libgcadapter detected (not %s)\n", LIBGCADAPTER_VERSION_STR);
        return -1;
    }

    last_update.tv_sec = 0;
    last_update.tv_usec = 0;

    for(int i = 0; i < GC_ADAPTER_PORTS; i++)
    {
        uinput[i] = 0;
    }

    signal(SIGINT, sigint);

    gc_adapter_t* adapter;
    char last_status[256] = "";
    int stdout_is_tty = isatty(STDOUT_FILENO);

    if(gc_adapter_initialize(adapter_index, &adapter))
    {
        while(!recieved_sigint)
        {
            struct timeval update;
            gettimeofday(&update, NULL);
            double update_ms = (update.tv_sec * 1000.0) + (update.tv_usec / 1000.0);
            double last_update_ms = (last_update.tv_sec * 1000.0) + (last_update.tv_usec / 1000.0);
            
            if(update_ms - last_update_ms >= 100.0)
            {
                gc_adapter_update(adapter);
                
                char status[256];
                if(adapter->open)
                {
                    char* ports[GC_ADAPTER_PORTS];
                    for(int i = 0; i < GC_ADAPTER_PORTS; i++)
                        ports[i] = port_to_str(adapter, i);
                    snprintf(status, sizeof(status), "Adapter connected (%p) %s %s %s %s",
                    adapter->usb_device_handle,
                    ports[0],
                    ports[1],
                    ports[2],
                    ports[3]);
                    for(int i = 0; i < GC_ADAPTER_PORTS; i++)
                        free(ports[i]);
                }
                else if(adapter->reserved)
                    snprintf(status, sizeof(status), "Waiting for adapter... (connected but reserved)");
                else
                    snprintf(status, sizeof(status), "Waiting for adapter...");

                // Only print on state change; use \r redraw on a tty, plain lines
                // otherwise so journald doesn't get one giant unterminated line.
                if(strcmp(status, last_status) != 0)
                {
                    printf(stdout_is_tty ? "\r%-60s" : "%s\n", status);
                    fflush(stdout);
                    strcpy(last_status, status);
                }
                last_update = update;
            }
            
            gc_adapter_poll(adapter);
            
            for(int i = 0; i < GC_ADAPTER_PORTS; i++)
            {
                if(uinput[i] == 0 && adapter->pad[i] != GC_PAD_NONE)
                    initialize_port(adapter, i);
                else if(uinput[i] != 0 && adapter->pad[i] == GC_PAD_NONE)
                    close_port(adapter, i);
                if(uinput[i] != 0 && adapter->pad[i] != GC_PAD_NONE)
                    update_port(adapter, i);
            }
    
            // When no adapter is open gc_adapter_poll() returns immediately, so
            // throttle. The open path blocks in libusb and stays unthrottled.
            if(!adapter->open)
            {
                struct timespec idle = { 0, 100 * 1000 * 1000 };
                nanosleep(&idle, NULL);
            }
            else
                sched_yield();
        }
        
        printf("\n");
        
        for(int i = 0; i < GC_ADAPTER_PORTS; i++)
        {
            if(uinput[i] != 0)
            {
                close_port(adapter, i);
            }
        }
        
        gc_adapter_free(adapter);
        return 0;
    }
    else
    {
        printf("Could not initialize libgcadapter.");
        return -1;
    }
}
