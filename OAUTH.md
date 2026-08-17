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

For compatibility with the standalone OAuth profile loader, VibeSolaris also maintains:

```text
~/.vibesolaris/oauth.conf
```

The directory is restricted to mode `0700` and the compatibility profile to mode `0600` where supported. That compatibility profile is permission-protected plain text, so users requiring all OAuth material to exist only in encrypted/keychain storage should account for this current limitation. Root or malware with the same account permissions can read it.

The legacy `/saveconfig PATH` command also writes plain text; normal autosave uses the encrypted configuration store instead.

## Refresh and API use

For the OpenAI provider, VibeSolaris prefers a valid configured OAuth bearer token when one exists. If the access token is close to expiry and a refresh token is available, it performs a refresh-token grant. If OAuth cannot be refreshed but an OpenAI API key is configured, the OpenAI adapter falls back to that API key.

Whether an officially issued ChatGPT sign-in token is accepted for a particular model endpoint is controlled by OpenAI's issued scopes/audience and service documentation; VibeSolaris does not convert an identity token or browser session into an API credential.
