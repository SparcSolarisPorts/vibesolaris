# OAuth 2.0 / PKCE authentication

VibeSolaris 0.9.5 includes a generic native-desktop OAuth 2.0 Authorisation Code flow with PKCE (S256). It is designed so that an officially registered OpenAI/ChatGPT OAuth client can be configured later without changing the program.

## Important OpenAI status

VibeSolaris does not ship a guessed OpenAI OAuth client ID, authorisation endpoint, token endpoint, or scope list. Enter only values officially issued for VibeSolaris by OpenAI or another OAuth provider.

A successful generic OAuth login does not by itself prove that the resulting token carries ChatGPT subscription/model-API entitlement. That depends on the provider's officially issued client, scopes, audience, and API documentation. The OpenAI API-key path remains available independently.

## GUI setup

Open **AUTHENTICATION -> ChatGPT OAuth -> OAuth settings** and enter:

1. **OAuth Client ID** — the public/native client ID issued for VibeSolaris.
2. **Authorisation URL** — the provider's authorisation endpoint.
3. **Token URL** — the provider's token endpoint.
4. **Scopes** — the exact space-separated scopes issued/documented for the application.
5. **Loopback redirect URI** — defaults to `http://127.0.0.1:14555/callback`.

The exact loopback redirect URI must also be allowed in the OAuth application's registration. A different port may be used if the provider registration permits it.

After saving, return to Authentication and press **Sign in with ChatGPT / OpenAI**. VibeSolaris opens the system browser and keeps a loopback listener active while the X11 GUI remains responsive.

## TUI setup

Example structure (replace every placeholder with officially issued values):

```text
/oauth client YOUR_CLIENT_ID
/oauth authorize OFFICIAL_AUTHORIZATION_URL
/oauth token OFFICIAL_TOKEN_URL
/oauth scopes OFFICIAL_SCOPE_LIST
/oauth redirect http://127.0.0.1:14555/callback
/oauth save
/oauth login
```

Other commands:

```text
/oauth status
/oauth logout
/auth
/login
```

`/login` is an alias for `/oauth login`.

## Claude OAuth and compliance

VibeSolaris supports Claude OAuth as a second, independent credential slot. The OpenAI slot and the Claude slot never share tokens.

### What is and is not supported

Anthropic does not permit third-party applications to use the sign-in credentials of Claude subscriptions (Claude Pro/Max, claude.ai, or Claude Code's first-party login). VibeSolaris therefore does **not**:

- ship or guess any Anthropic OAuth client ID, authorisation URL, token URL, or scope list;
- reuse Claude Code's client ID, or any token from a Claude Code or claude.ai login;
- read `~/.claude` or any other tool's credential files;
- send a Claude OAuth token to any host other than `https://api.anthropic.com`.

Claude OAuth is available only when you enter a client ID that **Anthropic has issued to VibeSolaris** for API access, together with the exact endpoints and scopes Anthropic documents for that client. The Anthropic API key path (`/key` or the API key button) remains the default and is unaffected.

### Enforced in code

When a Claude OAuth flow starts, refreshes, or exchanges a code, VibeSolaris refuses the configuration unless:

- the client ID is present and is not Anthropic's Claude Code subscription client ID (compared case-insensitively);
- the authorisation and token URLs use `https://` and their host is `api.anthropic.com` or `console.anthropic.com`. Hosts such as `claude.ai` are refused, and so are userinfo tricks such as `api.anthropic.com@example.net`;
- explicit scopes are configured. VibeSolaris does not supply default scopes;
- the redirect URI is a loopback `http://127.0.0.1` or `http://localhost` callback, as for the OpenAI flow.

Requests for a Claude OAuth bearer token go only to `https://api.anthropic.com/...`. If the Claude base URL is a proxy or gateway, the OAuth session is not sent; the configured Anthropic API key is used instead, or VibeSolaris reports that OAuth cannot be used for that base URL.

The host allowlist and the refused client ID are in `src/oauth.c` (`CLAUDE_ALLOWED_HOSTS`, `CLAUDE_CONSUMER_CLIENT_ID`). If Anthropic publishes a different official console or OAuth host for issued clients, update the allowlist deliberately; do not add `claude.ai`.

### Setting it up

Replace every placeholder with the values Anthropic issued to VibeSolaris:

```text
/claude-oauth client ISSUED_CLIENT_ID
/claude-oauth authorize ISSUED_AUTHORISATION_URL
/claude-oauth token ISSUED_TOKEN_URL
/claude-oauth scopes ISSUED_SCOPE_LIST
/claude-oauth redirect http://127.0.0.1:14556/callback
/claude-oauth login
/claude-oauth status
/claude-oauth logout
```

In the GUI, select the Claude provider and use **AUTHENTICATION -> Claude OAuth**. The default Claude callback port is `14556`, so it does not collide with the OpenAI default `14555`. The callback URI must also be registered for the issued client.

### Where Claude credentials are stored

Claude OAuth settings and tokens are stored only in the encrypted VibeSolaris configuration (see `SECURITY.md`). They are never written to `~/.vibesolaris/oauth.conf`, and the legacy `/saveconfig PATH` plain-text export omits Claude access and refresh tokens. The Claude non-secret settings (client ID, URLs, scopes, expiry) are still exported.

### Current limits

- Anthropic's third-party OAuth availability, required endpoints, and any extra request headers can change. VibeSolaris does not guess them. Use the values Anthropic documents for your issued client.
- The OpenAI compatibility profile at `~/.vibesolaris/oauth.conf` remains plain text with mode `0600`, as described under Token storage. It does not contain Claude material.
- Refresh failures fall back to the Claude API key if one is configured. If neither works, sign in again.

## Security properties

The implementation:

- uses Authorisation Code + PKCE with `code_challenge_method=S256`;
- generates a fresh cryptographically random PKCE verifier and OAuth `state` for every login;
- validates the returned `state` exactly;
- binds the callback listener only to loopback (`127.0.0.1`);
- accepts only a configured `http://127.0.0.1:PORT/path` or `http://localhost:PORT/path` callback;
- never requests or stores a ChatGPT password;
- never reads browser cookies;
- sends no client secret, because this is a native/public-client PKCE design;
- exchanges the authorisation code directly with the configured token endpoint;
- stores refresh tokens when returned and refreshes an access token before expiry;
- times out an unfinished login after five minutes.

## Token storage

OAuth application settings and session credentials are included in VibeSolaris's encrypted autosaved configuration (`/etc/vibesolaris/config.enc` when writable, otherwise `~/.vibesolaris/config.enc`). The encrypted store is protected by the matching `master.key` or by `VIBESOLARIS_MASTER_KEY`.

For compatibility with the standalone OAuth profile loader, VibeSolaris also maintains this OpenAI-only profile (Claude OAuth never writes here):

```text
~/.vibesolaris/oauth.conf
```

The directory is restricted to mode `0700` and the compatibility profile to mode `0600` where supported. That compatibility profile is permission-protected plain text, so users requiring all OAuth material to exist only in encrypted/keychain storage should account for this current limitation. Root or malware with the same account permissions can read it.

The legacy `/saveconfig PATH` command also writes plain text; normal autosave uses the encrypted configuration store instead.

## Refresh and API use

For the OpenAI provider, VibeSolaris prefers a valid configured OAuth bearer token when one exists. If the access token is close to expiry and a refresh token is available, it performs a refresh-token grant. If OAuth cannot be refreshed but an OpenAI API key is configured, the OpenAI adapter falls back to that API key.

Whether an officially issued ChatGPT sign-in token is accepted for a particular model endpoint is controlled by OpenAI's issued scopes/audience and service documentation; VibeSolaris does not convert an identity token or browser session into an API credential.
