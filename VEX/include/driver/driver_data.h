#pragma once
/**
 * driver_data.h — NOT USED
 *
 * We don't embed the driver anymore.
 * The cheat opens the existing HWiNFO64 device handle.
 * Just run HWiNFO64 first, then the cheat.
 *
 * This file exists only to satisfy the build system.
 */

// No driver data needed. The driver is provided by HWiNFO64 at runtime.
// Make sure HWiNFO64 is running as Administrator before launching the cheat.
static const unsigned char driver_bytes[1] = { 0x00 };
static const size_t driver_bytes_len = 0;
