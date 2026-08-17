# VibeSolaris security and encrypted configuration

VibeSolaris provides an encrypted configuration store. It prefers `/etc/vibesolaris` only when the current user can write it. Otherwise it automatically uses `~/.vibesolaris/config.enc` with `~/.vibesolaris/master.key`, giving every local user independent encrypted settings without requiring root.

The encrypted config contains provider selection, per-provider model/base URLs, protocol choices, cache settings, OAuth application metadata and session credentials, provider API keys, and MCP definitions/bearer tokens. The OAuth compatibility profile under `~/.vibesolaris/oauth.conf` may also contain OAuth session data and is restricted to mode `0600`.

## Encryption format

`config.enc` is a binary authenticated-encryption construction:

- AES-256-CBC encrypts the serialised configuration with a fresh random IV.
- HMAC-SHA256 authenticates the format marker, IV, and ciphertext before any
  decryption is attempted.
- Independent AES and HMAC keys are derived from the random 256-bit machine
  key using HMAC-SHA256 with different labels.
- Random material comes from OpenSSL/LibreSSL `RAND_bytes()`.
- `master.key` and `config.enc` are created mode `0600`; the directory is mode
  `0700` by default.
- Writes use a temporary file, `fsync()`, and `rename()` so a crash is much less
  likely to leave a partially-written config.

This protects the configuration at rest from casual disclosure and from
ciphertext modification.  It does not protect secrets from root or from an
attacker who can read both the key and ciphertext.

## Commands

TUI:

    /globalconfig status
    /globalconfig load
    /globalconfig save

GUI: use the `User config` or `System config` button in the left sidebar and choose Load or Save.

VibeSolaris automatically loads the selected encrypted config at startup when it exists and is readable. Normal users fall back to `~/.vibesolaris`, so root is not normally required.

## Managed-key mode

Instead of a file key, set a 64-hex-character machine key:

    export VIBESOLARIS_MASTER_KEY=0123...64_hex_chars_total...

This is useful when the key comes from an external secret-management system.
Set `VIBESOLARIS_GLOBAL_CONFIG_DIR` to explicitly override automatic location selection. Without that override, writable `/etc/vibesolaris` is preferred and an unwritable `/etc` automatically falls back to `~/.vibesolaris`.


## Per-user fallback

Location selection is evaluated for the current Unix account. If `/etc/vibesolaris` exists but that user lacks write/execute permission, VibeSolaris does not fail autosave and does not require elevation; it switches to `$HOME/.vibesolaris`. If the system directory does not exist, `/etc` itself must be writable before VibeSolaris will choose the system location. `HOME` is preferred; if it is absent, the account home directory is obtained from the password database.

The fallback directory is mode `0700`; `config.enc` and `master.key` are mode `0600`. Different Unix accounts therefore do not share API keys, models, MCP bearer tokens, or OAuth data by default.
