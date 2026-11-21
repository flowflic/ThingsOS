// SPDX-License-Identifier: MIT
// Restart an ESP32-S31 after a ThingsOS Metalio install.
// Uses the S31 LP watchdog reset register sequence from Espressif esptool 5.5.0.

function isDisconnect(error) {
  return error?.name === 'NetworkError' || /device has been lost|disconnected|port is closed/i.test(String(error?.message ?? ''));
}

export async function resetS31AfterInstall(loader) {
  try {
    await loader.writeReg(0x20801018, 0x50d83aa1);
    await loader.writeReg(0x20801004, 2000);
    await loader.writeReg(0x20801000, 0xd0000102);
    await loader.writeReg(0x20801018, 0);
  } catch (error) {
    if (!isDisconnect(error)) throw error;
  }
}
