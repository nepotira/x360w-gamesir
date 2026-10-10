// x360w.c — Xbox 360 / GameSir Nova Lite 用户态驱动 (macOS 13-26 / Apple Silicon & Intel)
// 支持: 原装/山寨 Xbox 360 无线接收器，以及 GameSir Nova Lite 2.4G Dongle / 有线 360 手柄
#include <libusb-1.0/libusb.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vhid.h"

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char *name;
} SupportedDev;

static const SupportedDev kSupportedDevices[] = {
    {0x3537, 0x1040, "GameSir Nova Lite 2.4G (Xbox 360 Mode)"},
    {0x3151, 0x3020, "GameSir 2.4G Receiver (Idle)"},
    {0x045e, 0x028e, "Xbox 360 Wired Controller"},
    {0x045e, 0x0291, "Xbox 360 Wireless Receiver (Official)"},
    {0x045e, 0x0719, "Xbox 360 Wireless Receiver (Later Rev)"},
    {0x045e, 0x02a9, "Xbox 360 Wireless Receiver (3rd Party)"},
};

#define kMaxSlots      4
#define kReadTimeoutMs 250

enum {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3,
    BTN_LB = 1 << 4, BTN_RB = 1 << 5, BTN_BACK = 1 << 6, BTN_START = 1 << 7,
    BTN_L3 = 1 << 8, BTN_R3 = 1 << 9, BTN_GUIDE = 1 << 10,
};

typedef struct {
    uint16_t buttons;
    bool dpad_up, dpad_down, dpad_left, dpad_right;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
} PadState;

typedef struct {
    int slot;
    int interface_number;
    uint8_t ep_in, ep_out;
    bool is_wired;
    libusb_device_handle *dev;
    pthread_t thread;
    volatile bool thread_running;
    VPad *vpad;
    volatile bool connected;
    volatile bool headset;
    volatile unsigned long packets;
    PadState state;
} Slot;

static volatile bool g_running = true;
static Slot g_slots[kMaxSlots];
static int g_num_slots = 0;
static bool g_verbose = false;
static bool g_no_hid = false;
static bool g_hid_broken = false;
static int g_identity = VHID_IDENTITY_DUALSENSE;

static void on_sigint(int sig) { (void)sig; g_running = false; }

static int send_cmd(Slot *s, const uint8_t *cmd, int len) {
    if (!s->ep_out) return 0;
    int transferred = 0;
    int rc = libusb_interrupt_transfer(s->dev, s->ep_out, (uint8_t *)cmd, len,
                                       &transferred, 1000);
    return rc;
}

static void send_led(Slot *s, uint8_t mode) {
    if (s->is_wired) {
        uint8_t cmd[3] = {0x01, 0x03, (uint8_t)(0x06 + (mode & 0x03))};
        send_cmd(s, cmd, sizeof(cmd));
    } else {
        uint8_t cmd[12] = {0x00, 0x00, 0x08, (uint8_t)(0x40 + (mode & 0x0f))};
        send_cmd(s, cmd, sizeof(cmd));
    }
}

static void send_presence_query(Slot *s) {
    if (!s->is_wired) {
        uint8_t cmd[12] = {0x08, 0x00, 0x0f, 0xc0};
        send_cmd(s, cmd, sizeof(cmd));
    }
}

static void send_rumble(Slot *s, uint8_t strong, uint8_t weak) {
    if (s->is_wired) {
        uint8_t cmd[8] = {0x00, 0x08, 0x00, strong, weak, 0x00, 0x00, 0x00};
        send_cmd(s, cmd, sizeof(cmd));
    } else {
        uint8_t cmd[12] = {0x00, 0x01, 0x0f, 0xc0, 0x00, strong, weak};
        send_cmd(s, cmd, sizeof(cmd));
    }
}

static void on_rumble_cb(void *ctx, uint8_t strong, uint8_t weak) {
    Slot *s = (Slot *)ctx;
    if (s && s->connected) send_rumble(s, strong, weak);
}

static void vpad_attach(Slot *s) {
    if (s->vpad || g_no_hid || g_hid_broken) return;
    char err[160] = {0};
    s->vpad = vpad_create(s->slot, g_identity, on_rumble_cb, s, err, sizeof(err));
    if (!s->vpad) {
        g_hid_broken = true;
        fprintf(stderr, "\n*** Falha ao criar controle virtual: %s\n"
                        "*** Executando em modo monitor; apos relaxar AMFI reinicie o driver\n", err);
    }
}

static void vpad_detach(Slot *s) {
    if (!s->vpad) return;
    vpad_destroy(s->vpad);
    s->vpad = NULL;
}

static int16_t le_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint8_t dpad_hat(const PadState *st) {
    int x = (st->dpad_right ? 1 : 0) - (st->dpad_left ? 1 : 0);
    int y = (st->dpad_down ? 1 : 0) - (st->dpad_up ? 1 : 0);
    if (x == 0 && y == 0) return 8;
    static const uint8_t map[3][3] = {{7, 0, 1}, {6, 8, 2}, {5, 4, 3}};
    return map[y + 1][x + 1];
}

static void handle_packet(Slot *s, const uint8_t *d, int len) {
    if (len < 2) return;

    if (g_verbose) {
        printf("[Slot %d] %2d bytes:", s->slot + 1, len);
        for (int i = 0; i < len; i++) printf(" %02x", d[i]);
        printf("\n");
        fflush(stdout);
    }

    // A: Wired Xbox 360 / GameSir Nova Lite 2.4G report (20 bytes: 0x00, 0x14...)
    if (d[0] == 0x00 && d[1] == 0x14 && len >= 14) {
        const uint8_t *p = d;
        PadState *st = (PadState *)&s->state;
        uint8_t d0 = p[2], d1 = p[3];
        st->dpad_up = d0 & 0x01; st->dpad_down = d0 & 0x02;
        st->dpad_left = d0 & 0x04; st->dpad_right = d0 & 0x08;
        uint16_t b = 0;
        if (d0 & 0x10) b |= BTN_START; if (d0 & 0x20) b |= BTN_BACK;
        if (d0 & 0x40) b |= BTN_L3;    if (d0 & 0x80) b |= BTN_R3;
        if (d1 & 0x01) b |= BTN_LB;    if (d1 & 0x02) b |= BTN_RB;
        if (d1 & 0x04) b |= BTN_GUIDE; if (d1 & 0x10) b |= BTN_A;
        if (d1 & 0x20) b |= BTN_B;     if (d1 & 0x40) b |= BTN_X;
        if (d1 & 0x80) b |= BTN_Y;
        st->buttons = b;
        st->lt = p[4]; st->rt = p[5];
        st->lx = le_i16(p + 6); st->ly = le_i16(p + 8);
        st->rx = le_i16(p + 10); st->ry = le_i16(p + 12);

        if (!s->connected) {
            s->connected = true;
            vpad_attach(s);
        }

        if (s->vpad) {
            uint16_t b_out = st->buttons;
            if (st->dpad_up) b_out |= 1 << 11;
            if (st->dpad_down) b_out |= 1 << 12;
            if (st->dpad_left) b_out |= 1 << 13;
            if (st->dpad_right) b_out |= 1 << 14;
            vpad_submit_state(s->vpad, b_out, dpad_hat(st),
                              st->lx, (int16_t)~st->ly, st->rx, (int16_t)~st->ry,
                              st->lt, st->rt);
        }
        return;
    }

    // B: Wireless receiver protocol packets
    if (d[0] & 0x08) {
        bool present = (d[1] & 0x80) != 0;
        s->headset = (d[1] & 0x40) != 0;
        if (present != s->connected) {
            s->connected = present;
            if (present) { send_led(s, (uint8_t)(6 + s->slot)); vpad_attach(s); }
            else { vpad_detach(s); memset((void *)&s->state, 0, sizeof(PadState)); }
        }
    }

    if (d[1] == 0x01 && len >= 18 && d[4] == 0x00) {
        const uint8_t *p = d + 4;
        PadState *st = (PadState *)&s->state;
        uint8_t d0 = p[2], d1 = p[3];
        st->dpad_up = d0 & 0x01; st->dpad_down = d0 & 0x02;
        st->dpad_left = d0 & 0x04; st->dpad_right = d0 & 0x08;
        uint16_t b = 0;
        if (d0 & 0x10) b |= BTN_START; if (d0 & 0x20) b |= BTN_BACK;
        if (d0 & 0x40) b |= BTN_L3;    if (d0 & 0x80) b |= BTN_R3;
        if (d1 & 0x01) b |= BTN_LB;    if (d1 & 0x02) b |= BTN_RB;
        if (d1 & 0x04) b |= BTN_GUIDE; if (d1 & 0x10) b |= BTN_A;
        if (d1 & 0x20) b |= BTN_B;     if (d1 & 0x40) b |= BTN_X;
        if (d1 & 0x80) b |= BTN_Y;
        st->buttons = b;
        st->lt = p[4]; st->rt = p[5];
        st->lx = le_i16(p + 6); st->ly = le_i16(p + 8);
        st->rx = le_i16(p + 10); st->ry = le_i16(p + 12);
        if (!s->connected) {
            s->connected = true;
            send_led(s, (uint8_t)(6 + s->slot));
            vpad_attach(s);
        }
        if (s->vpad) {
            uint16_t b_out = st->buttons;
            if (st->dpad_up) b_out |= 1 << 11;
            if (st->dpad_down) b_out |= 1 << 12;
            if (st->dpad_left) b_out |= 1 << 13;
            if (st->dpad_right) b_out |= 1 << 14;
            vpad_submit_state(s->vpad, b_out, dpad_hat(st),
                              st->lx, (int16_t)~st->ly, st->rx, (int16_t)~st->ry,
                              st->lt, st->rt);
        }
    }
}

static void *reader_main(void *arg) {
    Slot *s = (Slot *)arg;
    uint8_t buf[64];
    int consecutive_errors = 0;
    while (g_running && s->thread_running) {
        int transferred = 0;
        int rc = libusb_interrupt_transfer(s->dev, s->ep_in, buf, sizeof(buf),
                                           &transferred, kReadTimeoutMs);
        if (rc == LIBUSB_ERROR_TIMEOUT) {
            consecutive_errors = 0;
            continue;
        }
        if (rc == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(s->dev, s->ep_in);
            continue;
        }
        if (rc == LIBUSB_ERROR_OVERFLOW) {
            continue;
        }
        if (rc != 0) {
            consecutive_errors++;
            if (rc == LIBUSB_ERROR_NO_DEVICE || consecutive_errors >= 8) {
                if (g_verbose || rc == LIBUSB_ERROR_NO_DEVICE) {
                    fprintf(stderr, "\n[Slot %d] Conexao USB encerrada (%s: %d)\n",
                            s->slot + 1, libusb_error_name(rc), rc);
                }
                break;
            }
            usleep(15000);
            continue;
        }
        consecutive_errors = 0;
        s->packets++;
        handle_packet(s, buf, transferred);
    }
    s->thread_running = false;
    return NULL;
}

static const char *hat_str(const PadState *st) {
    if (st->dpad_up && st->dpad_right) return "↗";
    if (st->dpad_up && st->dpad_left) return "↖";
    if (st->dpad_down && st->dpad_right) return "↘";
    if (st->dpad_down && st->dpad_left) return "↙";
    if (st->dpad_up) return "↑"; if (st->dpad_down) return "↓";
    if (st->dpad_left) return "←"; if (st->dpad_right) return "→";
    return "·";
}

static void render(void) {
    printf("\033[H\033[J");
    printf("x360w · Driver GameSir Nova Lite / Xbox 360 (macOS)\n");
    printf("======================================================\n");
    for (int i = 0; i < g_num_slots; i++) {
        Slot *s = &g_slots[i];
        if (!s->connected) {
            printf("[Slot %d] ○ Aguardando conexao do controle (pacotes: %lu)\n",
                   i + 1, s->packets);
            continue;
        }
        PadState *st = (PadState *)&s->state;
        char btn[64] = {0};
        strcat(btn, (st->buttons & BTN_A) ? "A " : "- ");
        strcat(btn, (st->buttons & BTN_B) ? "B " : "- ");
        strcat(btn, (st->buttons & BTN_X) ? "X " : "- ");
        strcat(btn, (st->buttons & BTN_Y) ? "Y " : "- ");
        strcat(btn, (st->buttons & BTN_LB) ? "LB " : "-- ");
        strcat(btn, (st->buttons & BTN_RB) ? "RB " : "-- ");
        strcat(btn, (st->buttons & BTN_BACK) ? "BK " : "-- ");
        strcat(btn, (st->buttons & BTN_START) ? "ST " : "-- ");
        strcat(btn, (st->buttons & BTN_L3) ? "L3 " : "-- ");
        strcat(btn, (st->buttons & BTN_R3) ? "R3 " : "-- ");
        strcat(btn, (st->buttons & BTN_GUIDE) ? "◉" : "- ");
        printf("[Slot %d] ● Conectado%s%s (Modo: %s)\n", i + 1,
               s->headset ? " +Fone" : "",
               s->vpad ? " 🎮 DualSense Virtual Ativo" : "",
               s->is_wired ? "2.4G Dongle" : "Wireless Receiver");
        printf("  Botoes: %s  D-Pad: %s\n", btn, hat_str(st));
        printf("  Gatilhos: LT=%3d RT=%3d   Analogico E: %6d,%6d   Analogico D: %6d,%6d\n",
               st->lt, st->rt, st->lx, st->ly, st->rx, st->ry);
    }
    printf("======================================================\n");
    printf("Pressione Ctrl+C para sair%s\n", g_verbose ? " | --verbose ativado" : "");
    fflush(stdout);
}

static const SupportedDev *find_supported(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < sizeof(kSupportedDevices)/sizeof(kSupportedDevices[0]); i++) {
        if (kSupportedDevices[i].vid == vid && kSupportedDevices[i].pid == pid)
            return &kSupportedDevices[i];
    }
    return NULL;
}

int main(int argc, char **argv) {
    uint16_t want_vid = 0, want_pid = 0;
    bool hid_probe = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) g_verbose = true;
        else if (!strcmp(argv[i], "--no-hid")) g_no_hid = true;
        else if (!strcmp(argv[i], "--hid-probe")) hid_probe = true;
        else if (!strcmp(argv[i], "--identity") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "generic") || !strcmp(v, "g")) g_identity = VHID_IDENTITY_GENERIC;
            else g_identity = VHID_IDENTITY_DUALSENSE;
        }
        else if (!strcmp(argv[i], "--pid") && i + 1 < argc) want_pid = (uint16_t)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--vid") && i + 1 < argc) want_vid = (uint16_t)strtol(argv[++i], NULL, 0);
        else {
            printf("Uso: %s [--vid 0x3537] [--pid 0x1040] [--no-hid] [--hid-probe] [-v]\n", argv[0]);
            printf("  --no-hid     Apenas monitor de terminal, sem controle virtual\n");
            printf("  --hid-probe  Demonstracao de 30s do controle virtual\n");
            return 0;
        }
    }

    if (hid_probe) {
        char err[160] = {0};
        printf("[hid-probe] Criando controle virtual (%s)…\n",
               g_identity == VHID_IDENTITY_DUALSENSE ? "DualSense" : "Generic");
        VPad *p = vpad_create(0, g_identity, NULL, NULL, err, sizeof(err));
        if (!p) { printf("[hid-probe] Falha: %s\n", err); return 2; }
        printf("[hid-probe] ✅ Criado com sucesso! Teste em Ajustes do Sistema ou gamepad-tester\n");
        for (int t = 0; t < 300; t++) {
            double a = t * 0.21;
            vpad_submit_state(p, (t / 25 % 2) ? BTN_A : 0, (uint8_t)(t / 12 % 9),
                              (int16_t)(30000 * cos(a)), (int16_t)(30000 * sin(a)),
                              0, 0, (uint8_t)(t % 256), 0);
            usleep(100000);
        }
        vpad_destroy(p);
        printf("[hid-probe] Concluido.\n");
        return 0;
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    int rc = libusb_init(NULL);
    if (rc != 0) { fprintf(stderr, "libusb_init falhou: %s\n", libusb_error_name(rc)); return 1; }

    bool printed_wait = false;
    while (g_running) {
        libusb_device **list = NULL;
        ssize_t n = libusb_get_device_list(NULL, &list);
        libusb_device *found = NULL;
        struct libusb_device_descriptor desc;
        const SupportedDev *match_dev = NULL;

        for (ssize_t i = 0; i < n; i++) {
            if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
            // Se for o dongle em repouso sem controle ligado, ignorar ate ligar
            if (desc.idVendor == 0x3151 && desc.idProduct == 0x3020) continue;
            if (want_vid && desc.idVendor != want_vid) continue;
            if (want_pid && desc.idProduct != want_pid) continue;

            if (!want_vid && !want_pid) {
                if (desc.idVendor == 0x3537 && desc.idProduct == 0x1040) {
                    match_dev = find_supported(desc.idVendor, desc.idProduct);
                    found = list[i];
                    break;
                }
                const SupportedDev *sup = find_supported(desc.idVendor, desc.idProduct);
                if (sup) {
                    match_dev = sup;
                    found = list[i];
                }
            } else {
                found = list[i];
                break;
            }
        }

        if (!found) {
            if (!printed_wait) {
                printf("Aguardando conexao do GameSir Nova Lite (ligue o controle no dongle)... \n");
                fflush(stdout);
                printed_wait = true;
            }
            if (list) libusb_free_device_list(list, 1);
            for (int t = 0; t < 15 && g_running; t++) usleep(100000);
            continue;
        }

        printed_wait = false;
        printf("Dispositivo conectado: %04x:%04x (%s)\n", desc.idVendor, desc.idProduct,
               match_dev ? match_dev->name : "Custom");

        libusb_device_handle *dev = NULL;
        rc = libusb_open(found, &dev);
        if (rc != 0) {
            fprintf(stderr, "Erro ao abrir dispositivo: %s (tentando novamente em 2s)\n", libusb_error_name(rc));
            libusb_free_device_list(list, 1);
            sleep(2);
            continue;
        }

        struct libusb_config_descriptor *cfg = NULL;
        rc = libusb_get_active_config_descriptor(found, &cfg);
        if (rc != 0) rc = libusb_get_config_descriptor(found, 0, &cfg);
        if (rc != 0 || !cfg) {
            fprintf(stderr, "Erro ao ler descritores de configuracao\n");
            libusb_close(dev);
            libusb_free_device_list(list, 1);
            sleep(2);
            continue;
        }

        g_num_slots = 0;
        for (uint8_t i = 0; i < cfg->bNumInterfaces && g_num_slots < kMaxSlots; i++) {
            const struct libusb_interface *iface = &cfg->interface[i];
            for (int a = 0; a < iface->num_altsetting; a++) {
                const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
                if (alt->bInterfaceClass != 0xFF || alt->bInterfaceSubClass != 0x5D) continue;
                if (alt->bInterfaceProtocol != 0x01 && alt->bInterfaceProtocol != 0x81) continue;

                Slot *s = &g_slots[g_num_slots];
                memset(s, 0, sizeof(*s));
                s->slot = g_num_slots;
                s->interface_number = alt->bInterfaceNumber;
                s->is_wired = (alt->bInterfaceProtocol == 0x01);

                for (uint8_t e = 0; e < alt->bNumEndpoints; e++) {
                    const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                    if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
                    if (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) s->ep_in = ep->bEndpointAddress;
                    else s->ep_out = ep->bEndpointAddress;
                }
                if (!s->ep_in) continue;
                g_num_slots++;
                break;
            }
        }
        libusb_free_config_descriptor(cfg);

        if (g_num_slots == 0) {
            fprintf(stderr, "Nenhum endpoint de controle compativel encontrado.\n");
            libusb_close(dev);
            libusb_free_device_list(list, 1);
            sleep(2);
            continue;
        }

        bool claim_ok = true;
        for (int i = 0; i < g_num_slots; i++) {
            Slot *s = &g_slots[i];
            s->dev = dev;

            if (libusb_kernel_driver_active(dev, s->interface_number) == 1) {
                libusb_detach_kernel_driver(dev, s->interface_number);
            }

            rc = libusb_claim_interface(dev, s->interface_number);
            int retries = 0;
            while (rc == LIBUSB_ERROR_ACCESS && g_running && retries < 10) {
                static int warned = 0;
                if (!warned) {
                    fprintf(stderr, "[Aviso] Porta ocupada por outro app (ex: Steam), aguardando liberacao…\n");
                    warned = 1;
                }
                sleep(1);
                retries++;
                rc = libusb_claim_interface(dev, s->interface_number);
            }
            if (rc != 0) {
                fprintf(stderr, "[Slot %d] Falha ao assumir interface %d: %s\n",
                        i + 1, s->interface_number, libusb_error_name(rc));
                claim_ok = false;
                break;
            }
            send_presence_query(s);
            if (s->is_wired) {
                s->connected = true;
                vpad_attach(s);
            }
            s->thread_running = true;
            rc = pthread_create(&s->thread, NULL, reader_main, s);
            if (rc != 0) {
                s->thread_running = false;
                fprintf(stderr, "[Slot %d] Falha ao iniciar thread leitora: %d\n", i + 1, rc);
                claim_ok = false;
                break;
            }
        }

        if (!claim_ok) {
            libusb_close(dev);
            libusb_free_device_list(list, 1);
            sleep(2);
            continue;
        }

        printf("✅ Gamepad ativado com sucesso! Pronto para jogar no xCloud / Navegador.\n");
        printf("Pressione Ctrl+C para sair.\n");
        fflush(stdout);

        usleep(150000); // 150ms settling time

        while (g_running) {
            bool any_alive = false;
            for (int i = 0; i < g_num_slots; i++) {
                if (g_slots[i].thread_running) any_alive = true;
            }
            if (!any_alive) {
                printf("\nControle desconectado. Aguardando reconexão...\n");
                break;
            }

            if (isatty(fileno(stdout)) && !g_verbose) {
                render();
                usleep(50000);
            } else {
                usleep(250000);
            }
        }

        for (int i = 0; i < g_num_slots; i++) {
            Slot *s = &g_slots[i];
            s->thread_running = false;
            pthread_join(s->thread, NULL);
            vpad_detach(s);
            libusb_release_interface(dev, s->interface_number);
        }
        libusb_close(dev);
        libusb_free_device_list(list, 1);
    }

    libusb_exit(NULL);
    printf("Encerrado com sucesso.\n");
    return 0;
}
