/* SPDX-License-Identifier: Unlicense */
#include "vibesolaris.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/opensslv.h>
#ifdef LIBRESSL_VERSION_NUMBER
#include <openssl/opensslv.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define VS_CFG_MAGIC_LEN 8
#define VS_CFG_IV_LEN 16
#define VS_CFG_MAC_LEN 32
#define VS_CFG_KEY_LEN 32

static const unsigned char vs_cfg_magic[VS_CFG_MAGIC_LEN] = {'V','S','C','F','G','0','1','\n'};

static EVP_CIPHER_CTX *vs_cipher_ctx_new(void)
{
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER < 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
    EVP_CIPHER_CTX *ctx = (EVP_CIPHER_CTX*)malloc(sizeof(EVP_CIPHER_CTX));
    if (ctx) EVP_CIPHER_CTX_init(ctx);
    return ctx;
#else
    return EVP_CIPHER_CTX_new();
#endif
}

static void vs_cipher_ctx_free(EVP_CIPHER_CTX *ctx)
{
    if (!ctx) return;
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER < 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
    EVP_CIPHER_CTX_cleanup(ctx);
    free(ctx);
#else
    EVP_CIPHER_CTX_free(ctx);
#endif
}

static void seterr(char *err, size_t cap, const char *msg)
{
    if (!err || cap == 0) return;
    if (!msg) msg = "unknown error";
    strncpy(err, msg, cap - 1);
    err[cap - 1] = 0;
}

static int system_config_writable(void)
{
    struct stat st;
    if (stat(VS_GLOBAL_CONFIG_DIR, &st) == 0)
        return S_ISDIR(st.st_mode) && access(VS_GLOBAL_CONFIG_DIR, W_OK | X_OK) == 0;
    return access("/etc", W_OK | X_OK) == 0;
}

static const char *user_home(void)
{
    const char *h = getenv("HOME");
    struct passwd *pw;
    if (h && *h) return h;
    pw = getpwuid(getuid());
    return (pw && pw->pw_dir && *pw->pw_dir) ? pw->pw_dir : NULL;
}

static const char *config_dir(void)
{
    const char *e = getenv("VIBESOLARIS_GLOBAL_CONFIG_DIR");
    const char *h;
    static char user_dir[VS_MAX_PATH];
    if (e && *e) return e;
    if (system_config_writable()) return VS_GLOBAL_CONFIG_DIR;
    h = user_home();
    if (h && *h && snprintf(user_dir, sizeof(user_dir), "%s/%s", h, VS_USER_CONFIG_SUBDIR) < (int)sizeof(user_dir))
        return user_dir;
    return VS_GLOBAL_CONFIG_DIR;
}

int vs_secure_config_dir(char *out, size_t cap)
{
    const char *d = config_dir();
    size_t n;
    if (!out || cap == 0 || !d) return -1;
    n = strlen(d);
    if (n + 1 > cap) return -1;
    memcpy(out, d, n + 1);
    return 0;
}

int vs_secure_config_path(char *out, size_t cap)
{
    const char *d = config_dir();
    size_t n = strlen(d), l = strlen("config.enc");
    if (!out || cap == 0 || n + 1 + l + 1 > cap) return -1;
    strcpy(out, d);
    if (n && out[n-1] != '/') strcat(out, "/");
    strcat(out, "config.enc");
    return 0;
}

int vs_secure_config_is_per_user(void)
{
    const char *e = getenv("VIBESOLARIS_GLOBAL_CONFIG_DIR");
    if (e && *e) return 0;
    return system_config_writable() ? 0 : 1;
}

static int make_path(char *out, size_t cap, const char *leaf)
{
    const char *d = config_dir();
    size_t n = strlen(d), l = strlen(leaf);
    if (n + 1 + l + 1 > cap) return -1;
    strcpy(out, d);
    if (n && out[n-1] != '/') strcat(out, "/");
    strcat(out, leaf);
    return 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode_key(const char *s, unsigned char key[VS_CFG_KEY_LEN])
{
    int i, a, b;
    if (!s || strlen(s) < VS_CFG_KEY_LEN * 2) return -1;
    for (i = 0; i < VS_CFG_KEY_LEN; i++) {
        a = hexval((unsigned char)s[i*2]);
        b = hexval((unsigned char)s[i*2+1]);
        if (a < 0 || b < 0) return -1;
        key[i] = (unsigned char)((a << 4) | b);
    }
    return 0;
}

static void hex_encode_key(const unsigned char key[VS_CFG_KEY_LEN], char out[VS_CFG_KEY_LEN*2+2])
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < VS_CFG_KEY_LEN; i++) {
        out[i*2] = hex[(key[i] >> 4) & 15];
        out[i*2+1] = hex[key[i] & 15];
    }
    out[VS_CFG_KEY_LEN*2] = '\n';
    out[VS_CFG_KEY_LEN*2+1] = 0;
}

static int read_all_fd(int fd, unsigned char *buf, size_t n)
{
    size_t off = 0;
    ssize_t r;
    while (off < n) {
        r = read(fd, buf + off, n - off);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int write_all_fd(int fd, const unsigned char *buf, size_t n)
{
    size_t off = 0;
    ssize_t w;
    while (off < n) {
        w = write(fd, buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int ensure_dir(char *err, size_t err_cap)
{
    const char *d = config_dir();
    struct stat st;
    if (stat(d, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) { seterr(err,err_cap,"secure config path exists but is not a directory"); return -1; }
        (void)chmod(d,0700);
        return 0;
    }
    if (mkdir(d, 0700) != 0) {
        char b[256];
        snprintf(b,sizeof(b),"cannot create %s: %s",d,strerror(errno));
        seterr(err,err_cap,b); return -1;
    }
    (void)chmod(d,0700);
    return 0;
}

static int load_or_create_master(unsigned char master[VS_CFG_KEY_LEN], int create, char *err, size_t err_cap)
{
    const char *env = getenv("VIBESOLARIS_MASTER_KEY");
    char path[VS_MAX_PATH], buf[VS_CFG_KEY_LEN*2+16], hex[VS_CFG_KEY_LEN*2+2];
    int fd;
    ssize_t n;
    if (env && *env) {
        if (hex_decode_key(env, master) != 0) { seterr(err,err_cap,"VIBESOLARIS_MASTER_KEY must contain 64 hexadecimal characters"); return -1; }
        return 0;
    }
    if (make_path(path,sizeof(path),"master.key") != 0) { seterr(err,err_cap,"secure key path is too long"); return -1; }
    fd = open(path,O_RDONLY);
    if (fd >= 0) {
        n = read(fd,buf,sizeof(buf)-1); close(fd);
        if (n <= 0) { seterr(err,err_cap,"secure master key is empty or unreadable"); return -1; }
        buf[n] = 0;
        if (hex_decode_key(buf,master) != 0) { seterr(err,err_cap,"secure master key is invalid"); return -1; }
        return 0;
    }
    if (!create) { seterr(err,err_cap,"secure master key not found"); return -1; }
    if (ensure_dir(err,err_cap) != 0) return -1;
    if (RAND_bytes(master,VS_CFG_KEY_LEN) != 1) { seterr(err,err_cap,"OpenSSL RAND_bytes failed while creating the secure master key"); return -1; }
    fd = open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd < 0) {
        if (errno == EEXIST) return load_or_create_master(master,0,err,err_cap);
        {
            char b[256]; snprintf(b,sizeof(b),"cannot create secure master key: %.180s",strerror(errno)); seterr(err,err_cap,b);
        }
        return -1;
    }
    hex_encode_key(master,hex);
    if (write_all_fd(fd,(const unsigned char*)hex,strlen(hex)) != 0) {
        close(fd); unlink(path); seterr(err,err_cap,"failed writing secure master key"); return -1;
    }
    (void)fchmod(fd,0600);
    close(fd);
    return 0;
}

static int derive_keys(const unsigned char master[VS_CFG_KEY_LEN], unsigned char enc[32], unsigned char mac[32])
{
    unsigned int n = 0;
    static const unsigned char elabel[] = "VibeSolaris system config encryption v1";
    static const unsigned char mlabel[] = "VibeSolaris system config authentication v1";
    if (!HMAC(EVP_sha256(),master,VS_CFG_KEY_LEN,elabel,sizeof(elabel)-1,enc,&n) || n != 32) return -1;
    if (!HMAC(EVP_sha256(),master,VS_CFG_KEY_LEN,mlabel,sizeof(mlabel)-1,mac,&n) || n != 32) return -1;
    return 0;
}

static int secure_equal(const unsigned char *a, const unsigned char *b, size_t n)
{
    size_t i;
    unsigned char d = 0;
    for (i = 0; i < n; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

int vs_global_config_exists(void)
{
    char path[VS_MAX_PATH];
    struct stat st;
    if (make_path(path,sizeof(path),"config.enc") != 0) return 0;
    return stat(path,&st) == 0 && S_ISREG(st.st_mode);
}

int vs_global_config_save(const VSContext *ctx, char *err, size_t err_cap)
{
    unsigned char master[32], ekey[32], mkey[32], iv[16], mac[32];
    unsigned char *cipher = NULL, *auth = NULL;
    char *plain = NULL;
    char path[VS_MAX_PATH], tmp[VS_MAX_PATH];
    size_t plen, clen, authlen;
    int l1 = 0, l2 = 0, fd = -1, rc = -1;
    unsigned int mlen = 0;
    EVP_CIPHER_CTX *ec = NULL;

    if (!ctx) { seterr(err,err_cap,"invalid context"); return -1; }
    if (ensure_dir(err,err_cap) != 0) return -1;
    if (load_or_create_master(master,1,err,err_cap) != 0) return -1;
    if (derive_keys(master,ekey,mkey) != 0) { seterr(err,err_cap,"could not derive encryption keys"); goto done; }
    if (RAND_bytes(iv,sizeof(iv)) != 1) { seterr(err,err_cap,"OpenSSL RAND_bytes failed"); goto done; }
    plain = vs_config_serialize(ctx);
    if (!plain) { seterr(err,err_cap,"could not serialize configuration"); goto done; }
    plen = strlen(plain);
    cipher = (unsigned char*)malloc(plen + EVP_MAX_BLOCK_LENGTH + 1);
    if (!cipher) { seterr(err,err_cap,"out of memory"); goto done; }
    ec = vs_cipher_ctx_new();
    if (!ec) { seterr(err,err_cap,"could not create OpenSSL cipher context"); goto done; }
    if (EVP_EncryptInit_ex(ec,EVP_aes_256_cbc(),NULL,ekey,iv) != 1 ||
        EVP_EncryptUpdate(ec,cipher,&l1,(const unsigned char*)plain,(int)plen) != 1 ||
        EVP_EncryptFinal_ex(ec,cipher+l1,&l2) != 1) {
        seterr(err,err_cap,"AES-256-CBC encryption failed"); goto done;
    }
    clen = (size_t)(l1+l2);
    authlen = VS_CFG_MAGIC_LEN + VS_CFG_IV_LEN + clen;
    auth = (unsigned char*)malloc(authlen);
    if (!auth) { seterr(err,err_cap,"out of memory"); goto done; }
    memcpy(auth,vs_cfg_magic,VS_CFG_MAGIC_LEN);
    memcpy(auth+VS_CFG_MAGIC_LEN,iv,VS_CFG_IV_LEN);
    memcpy(auth+VS_CFG_MAGIC_LEN+VS_CFG_IV_LEN,cipher,clen);
    if (!HMAC(EVP_sha256(),mkey,32,auth,authlen,mac,&mlen) || mlen != 32) {
        seterr(err,err_cap,"HMAC-SHA256 failed"); goto done;
    }
    if (make_path(path,sizeof(path),"config.enc") != 0) { seterr(err,err_cap,"secure config path too long"); goto done; }
    if (snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",path,(long)getpid()) >= (int)sizeof(tmp)) { seterr(err,err_cap,"temporary config path too long"); goto done; }
    fd = open(tmp,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd < 0) { char b[256]; snprintf(b,sizeof(b),"cannot write secure config: %s",strerror(errno)); seterr(err,err_cap,b); goto done; }
    if (write_all_fd(fd,vs_cfg_magic,VS_CFG_MAGIC_LEN) || write_all_fd(fd,iv,VS_CFG_IV_LEN) ||
        write_all_fd(fd,mac,VS_CFG_MAC_LEN) || write_all_fd(fd,cipher,clen)) {
        seterr(err,err_cap,"failed writing encrypted secure config"); goto done;
    }
    (void)fchmod(fd,0600);
    if (fsync(fd) != 0) { seterr(err,err_cap,"failed syncing encrypted secure config"); goto done; }
    close(fd); fd = -1;
    if (rename(tmp,path) != 0) { char b[256]; snprintf(b,sizeof(b),"cannot install encrypted secure config: %s",strerror(errno)); seterr(err,err_cap,b); unlink(tmp); goto done; }
    rc = 0;
    seterr(err,err_cap,vs_secure_config_is_per_user() ? "saved encrypted per-user configuration" : "saved encrypted system configuration");
done:
    if (fd >= 0) { close(fd); if (tmp[0]) unlink(tmp); }
    if (ec) vs_cipher_ctx_free(ec);
    if (plain) { memset(plain,0,strlen(plain)); free(plain); }
    if (cipher) { memset(cipher,0,plen + EVP_MAX_BLOCK_LENGTH + 1); free(cipher); }
    if (auth) free(auth);
    memset(master,0,sizeof(master)); memset(ekey,0,sizeof(ekey)); memset(mkey,0,sizeof(mkey));
    return rc;
}

int vs_global_config_load(VSContext *ctx, char *err, size_t err_cap)
{
    unsigned char master[32], ekey[32], mkey[32], iv[16], stored[32], calc[32], magic[8];
    unsigned char *cipher = NULL, *auth = NULL, *plain = NULL;
    char path[VS_MAX_PATH];
    struct stat st;
    size_t clen, authlen;
    int fd = -1, l1 = 0, l2 = 0, rc = -1;
    unsigned int mlen = 0;
    EVP_CIPHER_CTX *dc = NULL;

    if (!ctx) { seterr(err,err_cap,"invalid context"); return -1; }
    if (make_path(path,sizeof(path),"config.enc") != 0) { seterr(err,err_cap,"secure config path too long"); return -1; }
    if (stat(path,&st) != 0) { seterr(err,err_cap,"encrypted secure config not found"); return -1; }
    if (st.st_size < VS_CFG_MAGIC_LEN+VS_CFG_IV_LEN+VS_CFG_MAC_LEN+16) { seterr(err,err_cap,"encrypted secure config is truncated"); return -1; }
    if (load_or_create_master(master,0,err,err_cap) != 0) return -1;
    if (derive_keys(master,ekey,mkey) != 0) { seterr(err,err_cap,"could not derive encryption keys"); goto done; }
    clen = (size_t)st.st_size - VS_CFG_MAGIC_LEN - VS_CFG_IV_LEN - VS_CFG_MAC_LEN;
    cipher = (unsigned char*)malloc(clen);
    plain = (unsigned char*)malloc(clen + EVP_MAX_BLOCK_LENGTH + 1);
    authlen = VS_CFG_MAGIC_LEN + VS_CFG_IV_LEN + clen;
    auth = (unsigned char*)malloc(authlen);
    if (!cipher || !plain || !auth) { seterr(err,err_cap,"out of memory"); goto done; }
    fd = open(path,O_RDONLY);
    if (fd < 0) { char b[256]; snprintf(b,sizeof(b),"cannot read secure config: %s",strerror(errno)); seterr(err,err_cap,b); goto done; }
    if (read_all_fd(fd,magic,sizeof(magic)) || read_all_fd(fd,iv,sizeof(iv)) || read_all_fd(fd,stored,sizeof(stored)) || read_all_fd(fd,cipher,clen)) {
        seterr(err,err_cap,"failed reading encrypted secure config"); goto done;
    }
    close(fd); fd = -1;
    if (memcmp(magic,vs_cfg_magic,VS_CFG_MAGIC_LEN) != 0) { seterr(err,err_cap,"unsupported encrypted config format"); goto done; }
    memcpy(auth,magic,VS_CFG_MAGIC_LEN); memcpy(auth+VS_CFG_MAGIC_LEN,iv,VS_CFG_IV_LEN); memcpy(auth+VS_CFG_MAGIC_LEN+VS_CFG_IV_LEN,cipher,clen);
    if (!HMAC(EVP_sha256(),mkey,32,auth,authlen,calc,&mlen) || mlen != 32 || !secure_equal(stored,calc,32)) {
        seterr(err,err_cap,"encrypted config authentication failed (wrong key or modified file)"); goto done;
    }
    dc = vs_cipher_ctx_new();
    if (!dc) { seterr(err,err_cap,"could not create OpenSSL cipher context"); goto done; }
    if (EVP_DecryptInit_ex(dc,EVP_aes_256_cbc(),NULL,ekey,iv) != 1 ||
        EVP_DecryptUpdate(dc,plain,&l1,cipher,(int)clen) != 1 ||
        EVP_DecryptFinal_ex(dc,plain+l1,&l2) != 1) {
        seterr(err,err_cap,"encrypted config decryption failed"); goto done;
    }
    plain[l1+l2] = 0;
    if (vs_config_apply_text(ctx,(const char*)plain) != 0) { seterr(err,err_cap,"decrypted configuration could not be parsed"); goto done; }
    rc = 0;
    seterr(err,err_cap,vs_secure_config_is_per_user() ? "loaded encrypted per-user configuration" : "loaded encrypted system configuration");
done:
    if (fd >= 0) close(fd);
    if (dc) vs_cipher_ctx_free(dc);
    if (cipher) free(cipher);
    if (auth) free(auth);
    if (plain) { memset(plain,0,clen + EVP_MAX_BLOCK_LENGTH + 1); free(plain); }
    memset(master,0,sizeof(master)); memset(ekey,0,sizeof(ekey)); memset(mkey,0,sizeof(mkey));
    return rc;
}
