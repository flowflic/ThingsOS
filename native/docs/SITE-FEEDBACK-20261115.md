# Website feedback integration — 2026-11-15

User selected prototype A. The production homepage now links to a feedback section between installation and release history. Wide desktop windows load the Tencent questionnaire on expansion; phones and narrow windows open the original questionnaire to preserve attachment controls. No questionnaire responses were submitted during verification.

Questionnaire: https://wj.qq.com/s2/28648647/7bhd/

For a website-only update, run `python native/integrate_site_feedback.py <existing-site-directory>` against a verified complete deployment tree. It preserves release artifacts and installs the section plus feedback.css/js. The source template and package asset list also include the integration. Do not rebuild the mixed-device production website from the legacy template without preserving current device-specific release data.

Evidence: `build/site-feedback-20261115/`. Both official domains passed all 212 file checks (106 files each). Browser checks covered widths 320, 390, 768, 1024, 1100, 1280, 1440; desktop questionnaire loading, collapse, mobile external link, navigation, Pico 0.4.1 and Metalio 0.1.9 installer verification. Existing release validation, including corrupted package rejection, passed. Firmware, source archives, metadata and release history remain byte-identical to the preceding deployment. No firmware was built or flashed.

Deployment: dpl_CukqupTmSNxgrA5gi6f5sL3vvWSB
Production: https://thingsos.scholarcn.com/#feedback
