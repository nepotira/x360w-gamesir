// vhid.c — IOHIDUserDevice 虚拟游戏手柄封装 (macOS 13-26)
// 全部 CoreFoundation 与 IOHIDUserDevice API 均通过 dlopen/dlsym 动态绑定
#include "vhid.h"
#include "ds_desc.h"

#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define kIOReturnSuccess 0
typedef int IOReturn;

typedef const void * CFAllocatorRef;
typedef const struct __CFString * CFStringRef;
typedef const struct __CFData * CFDataRef;
typedef const struct __CFNumber * CFNumberRef;
typedef struct __CFDictionary * CFMutableDictionaryRef;
typedef const struct __CFDictionary * CFDictionaryRef;
typedef long CFIndex;
typedef uint32_t CFStringEncoding;
typedef const void * CFTypeRef;

#define kCFStringEncodingUTF8 0x08000100
#define kCFNumberSInt32Type 3

typedef struct __IOHIDUserDevice *IOHIDUserDeviceRef;
typedef IOHIDUserDeviceRef (*CreateFn)(CFAllocatorRef, CFDictionaryRef, uint32_t options);
typedef void (*SetQueueFn)(IOHIDUserDeviceRef, dispatch_queue_t);
typedef void (*SetCancelFn)(IOHIDUserDeviceRef, dispatch_block_t);
typedef void (*ActivateFn)(IOHIDUserDeviceRef);
typedef void (*CancelFn)(IOHIDUserDeviceRef);
typedef IOReturn (*ReportFn)(IOHIDUserDeviceRef, uint64_t, const uint8_t *, CFIndex);
typedef IOReturn (*ReportCb)(void *refcon, int type, uint32_t reportID, uint8_t *report, CFIndex reportLength);
typedef void (*RegCbFn)(IOHIDUserDeviceRef, ReportCb, void *);

typedef CFStringRef (*CFStringCreateWithCStringFn)(CFAllocatorRef, const char *, CFStringEncoding);
typedef CFDataRef (*CFDataCreateFn)(CFAllocatorRef, const uint8_t *, CFIndex);
typedef CFNumberRef (*CFNumberCreateFn)(CFAllocatorRef, int, const void *);
typedef CFMutableDictionaryRef (*CFDictionaryCreateMutableFn)(CFAllocatorRef, CFIndex, const void *, const void *);
typedef void (*CFDictionarySetValueFn)(CFMutableDictionaryRef, const void *, const void *);
typedef void (*CFReleaseFn)(CFTypeRef);

static CreateFn pCreate;
static SetQueueFn pSetQueue;
static SetCancelFn pSetCancel;
static ActivateFn pActivate;
static CancelFn pCancel;
static ReportFn pReport;
static RegCbFn pRegSetCb, pRegGetCb;

static CFStringCreateWithCStringFn pCFStringCreate;
static CFDataCreateFn pCFDataCreate;
static CFNumberCreateFn pCFNumberCreate;
static CFDictionaryCreateMutableFn pCFDictionaryCreateMutable;
static CFDictionarySetValueFn pCFDictionarySetValue;
static CFReleaseFn pCFRelease;
static const void *pkCFTypeDictionaryKeyCallBacks;
static const void *pkCFTypeDictionaryValueCallBacks;

static bool g_api_ok = false;

static void bind_api(void) {
    static bool done = false;
    if (done) return;
    done = true;

    void *hCF = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
    void *hIO = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY);
    if (!hCF || !hIO) return;

    pCFStringCreate = (CFStringCreateWithCStringFn)dlsym(hCF, "CFStringCreateWithCString");
    pCFDataCreate = (CFDataCreateFn)dlsym(hCF, "CFDataCreate");
    pCFNumberCreate = (CFNumberCreateFn)dlsym(hCF, "CFNumberCreate");
    pCFDictionaryCreateMutable = (CFDictionaryCreateMutableFn)dlsym(hCF, "CFDictionaryCreateMutable");
    pCFDictionarySetValue = (CFDictionarySetValueFn)dlsym(hCF, "CFDictionarySetValue");
    pCFRelease = (CFReleaseFn)dlsym(hCF, "CFRelease");

    pkCFTypeDictionaryKeyCallBacks = dlsym(hCF, "kCFTypeDictionaryKeyCallBacks");
    pkCFTypeDictionaryValueCallBacks = dlsym(hCF, "kCFTypeDictionaryValueCallBacks");

    pCreate   = (CreateFn)  dlsym(hIO, "IOHIDUserDeviceCreateWithProperties");
    pSetQueue = (SetQueueFn)dlsym(hIO, "IOHIDUserDeviceSetDispatchQueue");
    pSetCancel= (SetCancelFn)dlsym(hIO, "IOHIDUserDeviceSetCancelHandler");
    pActivate = (ActivateFn)dlsym(hIO, "IOHIDUserDeviceActivate");
    pCancel   = (CancelFn)  dlsym(hIO, "IOHIDUserDeviceCancel");
    pReport   = (ReportFn)  dlsym(hIO, "IOHIDUserDeviceHandleReportWithTimeStamp");
    pRegSetCb = (RegCbFn)   dlsym(hIO, "IOHIDUserDeviceRegisterSetReportCallback");
    pRegGetCb = (RegCbFn)   dlsym(hIO, "IOHIDUserDeviceRegisterGetReportCallback");

    g_api_ok = pCreate && pSetQueue && pSetCancel && pActivate && pCancel && pReport &&
               pCFStringCreate && pCFDataCreate && pCFNumberCreate &&
               pCFDictionaryCreateMutable && pCFDictionarySetValue && pCFRelease;
}

// 通用手柄描述符: 16键 + Hat + 4×s16 摇杆轴 + 2×u8 扳机 + 震动输出
static const uint8_t kDescGeneric[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10,
    0x81, 0x02,
    0x05, 0x01, 0x09, 0x39,
    0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x65, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,
    0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
    0x05, 0x02, 0x09, 0xC5, 0x09, 0xC4,
    0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x85, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x01, 0x09, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x91, 0x02,
    0xC0
};

struct VPad {
    IOHIDUserDeviceRef dev;
    dispatch_queue_t queue;
    int slot;
    int identity;
    uint8_t seq;
    uint32_t ts;
    VPadRumbleFn on_rumble;
    void *rumble_ctx;
};

static void cf_set_num(CFMutableDictionaryRef d, const char *k, int32_t v) {
    CFStringRef key = pCFStringCreate(0, k, kCFStringEncodingUTF8);
    CFNumberRef num = pCFNumberCreate(0, kCFNumberSInt32Type, &v);
    pCFDictionarySetValue(d, key, num);
    pCFRelease(key); pCFRelease(num);
}

static void cf_set_str(CFMutableDictionaryRef d, const char *k, const char *v) {
    CFStringRef key = pCFStringCreate(0, k, kCFStringEncodingUTF8);
    CFStringRef val = pCFStringCreate(0, v, kCFStringEncodingUTF8);
    pCFDictionarySetValue(d, key, val);
    pCFRelease(key); pCFRelease(val);
}

static IOReturn on_set_report(void *refcon, int type, uint32_t reportID,
                              uint8_t *report, CFIndex len) {
    VPad *p = (VPad *)refcon;
    if (type != 1 || !p->on_rumble) return kIOReturnSuccess;
    if (p->identity == VHID_IDENTITY_GENERIC) {
        if (reportID == 2 && len >= 3) p->on_rumble(p->rumble_ctx, report[2], report[1]);
    } else {
        if (reportID == 2 && len >= 5) p->on_rumble(p->rumble_ctx, report[4], report[3]);
    }
    return kIOReturnSuccess;
}

static IOReturn on_get_report(void *refcon, int type, uint32_t reportID,
                              uint8_t *report, CFIndex len) {
    (void)refcon;
    if (len > 0) {
        memset(report, 0, len);
        report[0] = (uint8_t)reportID;
        if (reportID == 0x09 && len >= 7) {
            report[1] = 0x00; report[2] = 0x11; report[3] = 0x22;
            report[4] = 0x33; report[5] = 0x44; report[6] = 0x55;
        }
    }
    return kIOReturnSuccess;
}

VPad *vpad_create(int slot, int identity, VPadRumbleFn on_rumble, void *rumble_ctx,
                  char *err, int err_len) {
    bind_api();
    if (!g_api_ok) { snprintf(err, err_len, "IOHIDUserDevice/CF API symbols missing"); return NULL; }

    const uint8_t *desc; CFIndex desc_len;
    int32_t vid, pid;
    const char *mfr, *prod;
    if (identity == VHID_IDENTITY_DUALSENSE) {
        desc = kDSDesc; desc_len = sizeof(kDSDesc);
        vid = 0x054C; pid = 0x0CE6;
        mfr = "Sony Interactive Entertainment";
        prod = "DualSense Wireless Controller";
    } else {
        desc = kDescGeneric; desc_len = sizeof(kDescGeneric);
        vid = 0x1209; pid = 0x2360;
        mfr = "X360W"; prod = "X360W Controller";
    }

    CFMutableDictionaryRef props = pCFDictionaryCreateMutable(
        0, 0, pkCFTypeDictionaryKeyCallBacks, pkCFTypeDictionaryValueCallBacks);
    CFDataRef d = pCFDataCreate(0, desc, desc_len);
    CFStringRef rDescKey = pCFStringCreate(0, "ReportDescriptor", kCFStringEncodingUTF8);
    pCFDictionarySetValue(props, rDescKey, d);
    pCFRelease(d);
    pCFRelease(rDescKey);

    cf_set_num(props, "VendorID", vid);
    cf_set_num(props, "ProductID", pid);
    cf_set_num(props, "VersionNumber", 0x0100);
    cf_set_num(props, "PrimaryUsagePage", 0x01);
    cf_set_num(props, "PrimaryUsage", 0x05);
    cf_set_num(props, "LocationID", 0x360000 + slot);
    cf_set_str(props, "Manufacturer", mfr);
    char tmp[64];
    if (identity == VHID_IDENTITY_DUALSENSE) snprintf(tmp, sizeof(tmp), "%s", prod);
    else snprintf(tmp, sizeof(tmp), "%s %d", prod, slot + 1);
    cf_set_str(props, "Product", tmp);
    snprintf(tmp, sizeof(tmp), "X360W-%c%d", identity == VHID_IDENTITY_DUALSENSE ? 'D' : 'G', slot + 1);
    cf_set_str(props, "SerialNumber", tmp);
    cf_set_str(props, "Transport", "USB");

    IOHIDUserDeviceRef dev = pCreate(0, props, 0);
    pCFRelease(props);
    if (!dev) {
        snprintf(err, err_len,
                 "IOHIDUserDevice creation rejected — requires AMFI relaxation + entitlement signature");
        return NULL;
    }

    VPad *p = calloc(1, sizeof(VPad));
    p->dev = dev; p->slot = slot; p->identity = identity;
    p->on_rumble = on_rumble; p->rumble_ctx = rumble_ctx;
    char qname[48];
    snprintf(qname, sizeof(qname), "x360w.vhid.slot%d", slot);
    p->queue = dispatch_queue_create(qname, DISPATCH_QUEUE_SERIAL);
    pSetQueue(dev, p->queue);
    IOHIDUserDeviceRef held = dev;
    pSetCancel(dev, ^{ if (pCFRelease) pCFRelease(held); });
    if (pRegSetCb) pRegSetCb(dev, on_set_report, p);
    if (pRegGetCb) pRegGetCb(dev, on_get_report, p);
    pActivate(dev);
    vpad_submit_state(p, 0, 8, 0, 0, 0, 0, 0, 0);
    return p;
}

static inline uint8_t s16_to_u8(int16_t v) { return (uint8_t)((v >> 8) + 128); }

bool vpad_submit_state(VPad *p, uint16_t b, uint8_t hat,
                       int16_t lx, int16_t ly, int16_t rx, int16_t ry,
                       uint8_t lt, uint8_t rt) {
    if (!p || !p->dev) return false;
    IOReturn rc;
    if (p->identity == VHID_IDENTITY_DUALSENSE) {
        uint8_t r[64];
        memset(r, 0, sizeof(r));
        r[0] = 0x01;
        r[1] = s16_to_u8(lx); r[2] = s16_to_u8(ly);
        r[3] = s16_to_u8(rx); r[4] = s16_to_u8(ry);
        r[5] = lt; r[6] = rt;
        r[7] = p->seq++;
        if (b & (1 << 2)) r[8] |= 0x10;
        if (b & (1 << 0)) r[8] |= 0x20;
        if (b & (1 << 1)) r[8] |= 0x40;
        if (b & (1 << 3)) r[8] |= 0x80;
        r[8] |= hat & 0x0F;
        if (b & (1 << 4)) r[9] |= 0x01;
        if (b & (1 << 5)) r[9] |= 0x02;
        if (lt > 30) r[9] |= 0x04;
        if (rt > 30) r[9] |= 0x08;
        if (b & (1 << 6)) r[9] |= 0x10;
        if (b & (1 << 7)) r[9] |= 0x20;
        if (b & (1 << 8)) r[9] |= 0x40;
        if (b & (1 << 9)) r[9] |= 0x80;
        if (b & (1 << 10)) r[10] |= 0x01;
        p->ts += 1000;
        r[12] = p->ts & 0xFF; r[13] = (p->ts >> 8) & 0xFF;
        r[14] = (p->ts >> 16) & 0xFF; r[15] = (p->ts >> 24) & 0xFF;
        rc = pReport(p->dev, mach_absolute_time(), r, sizeof(r));
    } else {
        uint8_t r[VHID_INPUT_REPORT_LEN];
        memset(r, 0, sizeof(r));
        r[0] = 1;
        r[1] = b & 0xFF; r[2] = (b >> 8) & 0xFF;
        r[3] = hat & 0x0F;
        r[4] = lx & 0xFF; r[5] = (lx >> 8) & 0xFF;
        r[6] = ly & 0xFF; r[7] = (ly >> 8) & 0xFF;
        r[8] = rx & 0xFF; r[9] = (rx >> 8) & 0xFF;
        r[10] = ry & 0xFF; r[11] = (ry >> 8) & 0xFF;
        r[12] = lt; r[13] = rt;
        rc = pReport(p->dev, mach_absolute_time(), r, sizeof(r));
    }
    return rc == kIOReturnSuccess;
}

void vpad_destroy(VPad *p) {
    if (!p) return;
    vpad_submit_state(p, 0, 8, 0, 0, 0, 0, 0, 0);
    IOHIDUserDeviceRef dev = p->dev;
    p->dev = NULL;
    if (pCancel) pCancel(dev);
    free(p);
}
