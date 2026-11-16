# Running tests

The default `make test` runs the headless suite and does not require an X11 display. For an SSH session, you can also run:

```sh
unset DISPLAY WAYLAND_DISPLAY
make -k test-headless
```

The groups are:

- `make test-basic`: tests whose build recipes do not link X11, libcurl, or libcrypto.
- `make test-headless-deps`: headless tests that link libcurl and/or libcrypto.
- `make test-headless`: both groups; equivalent to `make test`.
- `make test-optional`: the separate computer-use backend test. Depending on the platform and active desktop, this test may interact with the mouse or keyboard; run it only in an appropriate environment.

A C compiler, make, and the dependencies required by each test are still needed. Some tests use threads, git, temporary files, or subprocesses. Headless does not mean dependency-free or guarantee that every test is independent of the local environment. The optional computer-use test is not an X11 integration test.
