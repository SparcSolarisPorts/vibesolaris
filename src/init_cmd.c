/* SPDX-License-Identifier: Unlicense */
/* VibeSolaris /init — Auto-onboarding: analyze repo and create CLAUDE.md */
/*
 * Mirrors Claude Code's /init: analyzes the repository structure, identifies
 * project type, language, build system, and generates CLAUDE.md.
 */

#include "vibesolaris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *init_dupstr(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (s) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int init_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

/* Detect project type from files */
static const char *init_detect_type(const char *path)
{
    char check[VS_MAX_PATH + 64];
    
    snprintf(check, sizeof(check), "%s/package.json", path);
    if (init_file_exists(check)) return "Node.js";
    
    snprintf(check, sizeof(check), "%s/Cargo.toml", path);
    if (init_file_exists(check)) return "Rust";
    
    snprintf(check, sizeof(check), "%s/go.mod", path);
    if (init_file_exists(check)) return "Go";
    
    snprintf(check, sizeof(check), "%s/pom.xml", path);
    if (init_file_exists(check)) return "Java (Maven)";
    
    snprintf(check, sizeof(check), "%s/build.gradle", path);
    if (init_file_exists(check)) return "Java (Gradle)";
    
    snprintf(check, sizeof(check), "%s/requirements.txt", path);
    if (init_file_exists(check)) return "Python";
    
    snprintf(check, sizeof(check), "%s/pyproject.toml", path);
    if (init_file_exists(check)) return "Python";
    
    snprintf(check, sizeof(check), "%s/Makefile", path);
    if (init_file_exists(check)) return "C/C++";
    
    snprintf(check, sizeof(check), "%s/CMakeLists.txt", path);
    if (init_file_exists(check)) return "C/C++ (CMake)";
    
    snprintf(check, sizeof(check), "%s/Gemfile", path);
    if (init_file_exists(check)) return "Ruby";
    
    snprintf(check, sizeof(check), "%s/pubspec.yaml", path);
    if (init_file_exists(check)) return "Dart/Flutter";
    
    return "Unknown";
}

/* Detect build command */
static const char *init_build_cmd(const char *path)
{
    char check[VS_MAX_PATH + 64];
    
    snprintf(check, sizeof(check), "%s/Makefile", path);
    if (init_file_exists(check)) return "make";
    
    snprintf(check, sizeof(check), "%s/package.json", path);
    if (init_file_exists(check)) return "npm run build";
    
    snprintf(check, sizeof(check), "%s/Cargo.toml", path);
    if (init_file_exists(check)) return "cargo build";
    
    snprintf(check, sizeof(check), "%s/go.mod", path);
    if (init_file_exists(check)) return "go build ./...";
    
    snprintf(check, sizeof(check), "%s/pom.xml", path);
    if (init_file_exists(check)) return "mvn package";
    
    snprintf(check, sizeof(check), "%s/build.gradle", path);
    if (init_file_exists(check)) return "gradle build";
    
    snprintf(check, sizeof(check), "%s/CMakeLists.txt", path);
    if (init_file_exists(check)) return "cmake . && make";
    
    return "unknown";
}

/* Detect test command */
static const char *init_test_cmd(const char *path)
{
    char check[VS_MAX_PATH + 64];
    
    snprintf(check, sizeof(check), "%s/Makefile", path);
    if (init_file_exists(check)) return "make test";
    
    snprintf(check, sizeof(check), "%s/package.json", path);
    if (init_file_exists(check)) return "npm test";
    
    snprintf(check, sizeof(check), "%s/Cargo.toml", path);
    if (init_file_exists(check)) return "cargo test";
    
    snprintf(check, sizeof(check), "%s/go.mod", path);
    if (init_file_exists(check)) return "go test ./...";
    
    snprintf(check, sizeof(check), "%s/pom.xml", path);
    if (init_file_exists(check)) return "mvn test";
    
    snprintf(check, sizeof(check), "%s/requirements.txt", path);
    if (init_file_exists(check)) return "pytest";
    
    snprintf(check, sizeof(check), "%s/Gemfile", path);
    if (init_file_exists(check)) return "bundle exec rspec";
    
    return "unknown";
}

/* ---- Public API ---- */

char *vs_init_repo(const char *path)
{
    if (!path) path = ".";
    
    const char *type = init_detect_type(path);
    const char *build = init_build_cmd(path);
    const char *test = init_test_cmd(path);
    
    /* Read README if it exists */
    char readme_path[VS_MAX_PATH + 32];
    char *readme = NULL;
    snprintf(readme_path, sizeof(readme_path), "%s/README.md", path);
    if (init_file_exists(readme_path)) {
        readme = vs_read_file(readme_path);
    }
    
    /* Generate CLAUDE.md */
    char *result = (char *)malloc(4096);
    if (!result) { free(readme); return NULL; }
    
    int pos = 0;
    pos += snprintf(result + pos, 4096 - pos,
                    "# %s Project\n\n", type);
    pos += snprintf(result + pos, 4096 - pos,
                    "## Overview\n"
                    "This is a %s project.\n\n", type);
    pos += snprintf(result + pos, 4096 - pos,
                    "## Build & Test\n"
                    "- Build: `%s`\n"
                    "- Test: `%s`\n\n", build, test);
    pos += snprintf(result + pos, 4096 - pos,
                    "## Conventions\n"
                    "- Follow the existing code style in the repository.\n"
                    "- Run tests before submitting changes.\n"
                    "- Use descriptive commit messages.\n\n");
    
    if (readme && pos < 3500) {
        /* Include first 100 chars of README as context */
        pos += snprintf(result + pos, 4096 - pos,
                        "## Project Description\n%s...\n\n",
                        readme);
    }
    
    /* Write CLAUDE.md */
    char claude_path[VS_MAX_PATH + 32];
    snprintf(claude_path, sizeof(claude_path), "%s/CLAUDE.md", path);
    
    /* Only write if CLAUDE.md doesn't exist */
    if (!init_file_exists(claude_path)) {
        FILE *f = fopen(claude_path, "w");
        if (f) {
            fprintf(f, "%s", result);
            fclose(f);
            char *msg = (char *)malloc(512);
            if (msg) snprintf(msg, 512,
                             "OK: Created CLAUDE.md for %s project.\n"
                             "Build: %s\nTest: %s\n"
                             "Review and edit CLAUDE.md to customize.",
                             type, build, test);
            free(result);
            free(readme);
            return msg ? msg : init_dupstr("OK: CLAUDE.md created");
        }
    } else {
        char *msg = (char *)malloc(256);
        if (msg) snprintf(msg, 256,
                         "CLAUDE.md already exists at %s. Use /goal to set session goals instead.",
                         claude_path);
        free(result);
        free(readme);
        return msg ? msg : init_dupstr("CLAUDE.md already exists");
    }
    
    free(readme);
    return result;
}
