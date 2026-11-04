#pragma once
// Build identity and immutable family boundary for this separate product.
// This header is also directly testable outside the kernel.
#define IC_PRODUCT_SERVICE L"InterceptionV2"
#define IC_PRODUCT_XUSB_HID_PARENT_VALUE L"InterceptionV2XusbHidParent"
#define IC_PRODUCT_XUSB_PARENT_VALUE L"InterceptionV2XusbParent"
#define IC_PRODUCT_CONTROL_HWID L"ROOT\\INTERCEPTIONV2"
#define IC_PRODUCT_NT_DEVICE L"\\Device\\InterceptionV2"
#define IC_PRODUCT_DOS_DEVICE L"\\DosDevices\\InterceptionV2"
#define IC_PRODUCT_TRIAL_SLOT 0u
#define IC_PRODUCT_TRIAL_KEY L"\\Registry\\Machine\\HARDWARE\\InterceptionV2Trial"
#ifndef IC_PRODUCT_INPUT_MASK
#define IC_PRODUCT_INPUT_MASK 3u
#endif
#ifndef IC_PRODUCT_CONTROLLER_MASK
#define IC_PRODUCT_CONTROLLER_MASK 0u
#endif
#ifndef IC_PRODUCT_HAS_KEYBOARD_MOUSE
#define IC_PRODUCT_HAS_KEYBOARD_MOUSE 1
#endif
#ifndef IC_PRODUCT_HAS_CONTROLLERS
#define IC_PRODUCT_HAS_CONTROLLERS 0
#endif
#define IC_PRODUCT_STREAM_MASK (IC_PRODUCT_INPUT_MASK | (IC_PRODUCT_CONTROLLER_MASK << 2))
#define IC_PRODUCT_HID_GUID {0x8fc27c56,0xb747,0x4788,{0x81,0xb8,0xd1,0xca,0x60,0x34,0x9f,0x4d}}
#define IC_PRODUCT_XUSB_GUID {0x43a46d56,0x7c51,0x4b41,{0xa9,0x10,0x00,0xf5,0x1c,0x26,0xa3,0xac}}
#define IC_PRODUCT_XINPUTHID_GUID {0x840858f7,0xb3ab,0x4a59,{0xb2,0x3b,0x2c,0x40,0x0c,0xc9,0x5d,0xa0}}
#define IC_PRODUCT_GIP_GUID {0xb09cdb3c,0xf100,0x4020,{0x8b,0xaa,0xa0,0x47,0xf8,0xdb,0x51,0x8a}}
#define IC_PRODUCT_XUSB_MIRROR_GUID {0x17e8f5d7,0x9116,0x4f66,{0x90,0xcf,0x76,0x72,0x57,0x7c,0x11,0xb0}}
static __inline int IcProductTypeAllowed(unsigned int type) {
    return type>=1 && type<=6 && (IC_PRODUCT_STREAM_MASK & (1u<<(type-1)))!=0;
}
static __inline int IcProductCreateMasksAllowed(unsigned int input,unsigned int controllers) {
    return !(input & ~IC_PRODUCT_INPUT_MASK) && !(controllers & ~IC_PRODUCT_CONTROLLER_MASK);
}
// Function numbers are the pointer-free protocol's CTL_CODE function field.
// Unknown/common functions keep the normal control dispatch validation.
static __inline int IcProductIoctlFunctionAllowed(unsigned int fn) {
    switch(fn) {
    case 0xA01:case 0xA03:case 0xA07:
        return (IC_PRODUCT_INPUT_MASK & 1u)!=0;
    case 0xA02:case 0xA04:case 0xA08:
        return (IC_PRODUCT_INPUT_MASK & 2u)!=0;
    case 0xA09:case 0xA0A:case 0xA0B:case 0xA0C:case 0xA0D:
        return (IC_PRODUCT_CONTROLLER_MASK & 1u)!=0;
    case 0xA10:case 0xA11:case 0xA12:case 0xA13:case 0xA14:case 0xA15:
        return (IC_PRODUCT_CONTROLLER_MASK & 2u)!=0;
    case 0xA17:case 0xA18:case 0xA19:case 0xA1A:case 0xA1B:
        return (IC_PRODUCT_CONTROLLER_MASK & 4u)!=0;
    case 0xA20:case 0xA21:case 0xA22:case 0xA23:case 0xA24:case 0xA25:
    case 0xA29:case 0xA2A:case 0xA2B:
        return (IC_PRODUCT_CONTROLLER_MASK & 8u)!=0;
    default:return 1;
    }
}

