// SPDX-License-Identifier: Apache-2.0
// ESP32-S3 RTC register definitions from the official esptool target documentation.
// A USB RTS reset can retain DOWNLOAD straps. RTC watchdog reset re-samples them.
export async function resetS3AfterInstall(loader) {
  if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw Error('Unsupported reset target');
  await loader.writeReg(0x6000812c, 0, 1); // Clear FORCE_DOWNLOAD_BOOT only.
  await loader.writeReg(0x600080b0, 0x50d83aa1); // Volatile RTC watchdog unlock.
  await loader.writeReg(0x6000809c, 2000);
  // Do not send another command after enabling reset: USB can disappear immediately.
  try {
    await loader.writeReg(0x60008098, 0xd0000102);
  } catch (error) {
    // Only a USB disconnect during this final reset command is expected.
    // Register setup failures and timeouts remain installation errors.
    if (error?.name !== 'NetworkError' && !/device.*disconnect|device.*lost/i.test(String(error))) throw error;
  }
  loader.info('ThingsOS: watchdog reboot requested; reconnect to verify application startup.');
}
