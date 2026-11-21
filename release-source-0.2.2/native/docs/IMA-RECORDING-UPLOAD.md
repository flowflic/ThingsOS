# ima recording upload — implementation and validation

Status (2026-10-26): UE approved by the user; firmware implementation complete,
host tests and main/rescue build checks recorded below. No desktop credentials
were accessed and no real recording was uploaded. Device/cloud acceptance pending.
Version remains 0.3.6. The approved applications/control sheet is retained.
New UE: `build/prototypes/metalio-apps-controls/index-ima.html`.

## Implemented behavior

- Local recording is completed, closed, and its WAV header finalized before upload.
- Save dialog offers Done or Upload to ima. Recent recordings expose Upload and
  upload status. Default is manual upload; local files remain on the SD card.
- Select a writable knowledge base. First version targets its root; users can
  change the target for an individual upload. No inferred destination from desktop
  knowledge-base history.
- Per the user's follow-up correction, ima settings are accessible from Recorder
  only; the Settings tab header entry and its touch target are removed. No launcher
  tile is added. A phone HTTPS page on port 8443 handles Client ID / API Key while device
  pairing is explicitly open (5 minutes, 8 attempts). No desktop keys are embedded.
- Credentials are encrypted in a dedicated NVS blob with AES-256-GCM. PBKDF2-HMAC-
  SHA256 (100000 iterations, random 16-byte salt) derives a key from a user-supplied
  unlock password of at least 12 characters. Neither password nor derived key is
  persisted. Reboot requires phone unlock. This is an application vault; it does
  not enable flash encryption, NVS partition encryption, Secure Boot or eFuse changes.
- Each pairing session generates an ephemeral EC P-256 TLS identity in RAM.
  Verify the certificate's complete SHA-256 fingerprint against the device before
  entering credentials. The browser's self-signed-certificate step is performed by
  the user; automation must not bypass it. No HTTP credential endpoint exists.
- Never show API keys in device UI, status JSON or logs. The phone clears password
  inputs after submission; pairing TLS private keys are erased on server shutdown.
- Progress, cancellation, network failure and expired-key recovery preserve local
  audio. An uncertain final request is reconciled before retry, avoiding a blind
  repeated add. Cloud parsing/transcription is separate from upload completion.
- Duplicate name: cancel or keep both using a timestamped remote copy. Do not
  replace cloud content or rename the local file. Remote title equals remote
  file_name including extension. Repeated taps do not create concurrent jobs.

## Contract verified against installed ima-skill 1.1.7

User reference: https://ima.qq.com/agent-interface

Skill files read: `ima-skill/SKILL.md`, `knowledge-base/SKILL.md`,
`knowledge-base/references/api.md`, and `scripts/preflight-check.cjs` under
`C:/Users/freed/.codex/skills/`.
The public interface is a client-rendered configuration page; the live upload
contract and account permissions still require authenticated integration validation.

- WAV: `media_type=15`, `content_type=audio/wav`, `file_ext=wav`.
- Documented maximum audio size 200 MiB and duration 2 hours; enforce both. At
  device PCM16/16kHz/mono, size reaches the limit before two hours.
- POST JSON to `https://ima.qq.com/openapi/wiki/v1/` with
  `ima-openapi-clientid` / `ima-openapi-apikey`. JSON UTF-8, binary WAV unchanged.
- `get_addable_knowledge_base_list` (cursor pagination) for destination selection;
  optional `get_knowledge_base` for names/descriptions. Hide internal IDs.
- `check_repeated_names` BEFORE `create_media`, then COS binary upload using only
  the scoped temporary credentials returned by create_media. Permanent IMA
  credentials never go to COS; restrict official API and returned COS hosts.
- Only after COS success call `add_knowledge` with media_type, media_id,
  knowledge_base_id, title and file_info {cos_key,file_size,file_name}.
- Check both HTTP and JSON business code, and read back target knowledge/media
  state. Do not equate HTTP 200, bytes sent or add acknowledgement with finished
  audio processing.

## Implementation and acceptance gates after UE approval

Implement independent firmware protocol/client code using ESP-IDF HTTPS, bounded
streaming buffers and short SD mutex holds. Keep UI/display ownership on the main
task, exclude OTA/USB/other SD writes, and stop/suspend recording before upload.
State machine: local_ready -> checking -> transferring -> adding -> uploaded /
failed / uncertain / cancelled. Persist enough per-file state for reconciliation
after reset without persisting temporary COS secrets or complete responses.

Host verification must cover filename/title consistency, size and duration limits,
malformed credential responses, untrusted redirect/host rejection, failed COS
never adding knowledge, duplicate handling and uncertain retries. Device checks:
pairing, writable destination selection, actual WAV upload + readback, network loss,
credential expiry, local WAV retention, cancellation, and reading/display safety.
All firmware, flash and cloud evidence must be reported separately.

## Source and checks

- Independent portable protocol: `src/ima_protocol.c`, `include/thingsos/ima_protocol.h`.
  HTTPS/streaming and encrypted storage: `firmware/components/thingsos_ima/`.
  Native pages: `firmware/main/ima_ui.inc` and recorder integration in `apps_controls.inc`.
- Upload streams in 8 KiB blocks with short shared SD lock holds. The worker uses
  an internal 16 KiB stack; signatures/response buffers are off stack. No display
  operation is performed on that worker. OTA, USB and audio storage claims exclude
  concurrent uploads. Sleep/USB/upgrade cancel and wait for the upload to quiesce.
- A durable pending journal is committed before add_knowledge. An uncertain add
  blocks a new upload until its media ID is found in the selected knowledge base.
  Readback pages through the root listing, rather than waiting for audio search
  indexing; it never interprets an empty page as permission to repeat add_knowledge.
  A bounded 32-entry receipt ring preserves file statuses across resets, keyed to
  local path/size/mtime. Local recordings are never removed by the IMA code.
  Explicit Disconnect clears only the dedicated ima credential/tracking namespace;
  it leaves all SD files and cloud content intact, and permits a fresh connection
  when an old account can no longer confirm its pending upload.
- `ima_upload_failure_reconciliation`: success, duplicate, permissions failure,
  invalid COS credential/host, failed COS never adding knowledge, journal-write
  failure, uncertain submission, readback and cancellation. Signature cross-checked
  against an independent Node crypto vector.
- `ima_encrypted_credentials_vault`: production vault code with Windows CNG as its
  host crypto adapter; independent PBKDF2 vector, encrypted persistence, unlock,
  wrong-password zeroing, changed nonce, saved target and GCM tamper rejection.
- `device_ui_controller`: native draw/tap checks and PGM captures for connection,
  destination selection, explicit send, cancellation, duplicate choice, uncertain
  reconciliation and save-then-upload. These are host renders, not panel evidence.
- 29/29 host tests passed. Main and rescue ESP-IDF builds pass. The IDF partition
  warning compares the main binary against the smaller rescue slot; main must fit
  ota_0 (0xae0000), and the independently built rescue must fit 0x200000. Packaging
  validates these separately. No flash or publication is implied by build success.

Live acceptance still needed: device TLS provisioning/fingerprint, vault unlock,
real writable knowledge-base listing, WAV upload/readback, account quota and key
expiry, network-loss/cancellation, reboot reconciliation, memory/stack margin,
and panel interaction while uploading. The installed skill documents media_type 15
for recordings; its older repeated-name reference omits 15 while the mandatory
workflow includes it. The implementation performs the required check and reports
any backend refusal; live compatibility has not been claimed.

## Authorized device flash (2026-10-26)

User explicitly requested installation. COM5 device USB serial 1C:29:04:D0:4A:B0.
A complete 16 MiB backup was retained before selecting the existing rescue. Rescue
boot was confirmed before writing ota_0 at 0x220000. Full readback matched the
6,422,304-byte candidate (SHA-256
`016ca5beedabad26babde891d4594e3e87d48bbc1125dce021de8360155e140e`).
NVS, bootloader, partition table, PHY, rescue and system-font bytes matched backup.
Boot selector readback and the new ELF identity matched. The 45-second main boot
capture confirmed display/font/touch startup health and panel output-off state;
no draw error, underrun, assertion or panic was observed.
Evidence: `build/ima-upload-20261026/device-flash/flash-validation.json`.
IMA phone provisioning, actual audio capture/upload and cloud readback still need
on-device acceptance; startup health does not establish those results.
