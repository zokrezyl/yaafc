# yaafc-on-riscv64 demo (yemu)

Boots a tiny Alpine riscv64 VM with the cross-compiled `yaafc` +
`yaafc-frontend` baked in. Same shape as yaapp's `tmp/yemu/`, but
yaafc is fully static so the image only needs Alpine's base
userland — there's nothing to provision on first boot.

## One-time setup (host)

```sh
sudo apt-get install -y \
    gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
    qemu-system-misc brotli e2fsprogs util-linux curl
```

You also need passwordless `sudo` for `losetup` / `mount` (used by
`build-image.sh` to drop files into the rootfs).

## Build the image

```sh
# 1. cross-compile yaafc for riscv64 (gateway + frontend binaries)
make -C ../../.. build-linux-riscv64-release

# 2. fetch yetty's kernel + opensbi + alpine-disk, bake binaries +
#    scenario assets into a bootable ext4 image
./build-image.sh
```

Outputs land under `./build/`:

```
build/
├── alpine-rootfs.img        256 MiB rootfs
├── kernel-riscv64.bin       riscv64 linux 7.0 (from yetty release)
└── opensbi-fw_dynamic.bin   opensbi 1.4 (from yetty release)
```

## Boot it

```sh
./run-vm.sh
```

The launcher (baked at `/opt/git-yaafc/run.sh` inside the image)
brings up loopback + slirp net and runs gateway + frontend. From the
host:

| Service        | Guest port | Host port forward |
|----------------|------------|-------------------|
| Gateway (HTTP) | 8080       | 18080             |
| HTML frontend  | 8081       | 18081             |

Open `http://127.0.0.1:18081/login` in a browser to hit the C
frontend talking to the C gateway inside the VM.

Quit with `Ctrl-A X`.

## What's running where

```
host browser
   └─→ http://127.0.0.1:18081 ──── slirp ─→ yaafc-frontend (guest:8081)
                                                │
                                                │  POST /_rpc
                                                ▼
                                          yaafc gateway (guest:8080)
                                                │
                                                │  yrpc (in-process here,
                                                │  no mesh children)
                                                ▼
                                          backend plugins (same proc)
```

The scenario `yaafc.yaml` is the same one the desktop smoke uses;
inside the VM we just run the gateway + frontend without spawning
the mesh children — every plugin is linked in and dispatched locally.

## Browser deploy (wasm)

In addition to host qemu, the same `alpine-rootfs.img` boots in a
browser via TinyEMU compiled to wasm — no host emulator, no install
on the visitor's side.

```sh
# 1. Cross-compile yaafc + yaafc-frontend for riscv64 (host work, ~3 min)
make -C ../../.. build-linux-riscv64-release

# 2. Bake the rootfs (needs sudo — see "Build the image" above)
./build-image.sh

# 3. Compile tinyemu to wasm and stage the VM assets next to it
make -C ../../.. build-webasm-yemu-release

# 4. Serve locally
python3 web/build/serve.py 8000 web/build
# open http://127.0.0.1:8000/
```

The Emscripten SDK has to live at `$HOME/.local/emsdk` (yetty's
convention — see `yetty/build-tools/install-emscripten.sh`); the
build script auto-prepends it to `$PATH`. Override with `EMSDK=…`.

Output layout (`scenarios/git-yaafc/yemu/web/build/`):

| File                          | What                                     |
|-------------------------------|------------------------------------------|
| `yaafc-yemu.js`               | emscripten loader (114 KB)               |
| `yaafc-yemu.wasm`             | compiled tinyemu + slirp + temu (~220 KB)|
| `index.html`                  | UI shell — terminal `<pre>` + input form |
| `yaafc.cfg`                   | tinyemu VM config (MEMFS paths)          |
| `assets/kernel-riscv64.bin`   | yetty linux kernel                       |
| `assets/opensbi-fw_dynamic.bin` | yetty opensbi                          |
| `assets/alpine-rootfs.img`    | yaafc-baked rootfs                       |
| `serve.py`                    | dev HTTP server (sets COOP/COEP)         |

The page loads `yaafc-yemu.js`, fetches the three VM assets, writes
them into MEMFS at the `/vm/...` paths the `yaafc.cfg` references,
then calls `Module.callMain(['/vm/yaafc.cfg'])` — same temu.c
entrypoint as the desktop build. Console output goes to a `<pre>`
via `Module.print`; keystrokes from the input form get queued into
`Module.stdin`.

### Known limitations (this iteration)

- Console is line-buffered (input form → submit). xterm.js + per-key
  delivery would give a real shell; that's a UX iteration on the page
  alone — the wasm side is the same.
- Assets are served uncompressed. The rootfs is ~256 MB — fine on a
  fast LAN, slow on the open internet. Brotli pre-compression + a
  decoder linked into the wasm (yetty's pattern) is the obvious
  follow-up.
- Network inside the VM uses slirp's user-mode NAT (10.0.2.0/24). The
  guest can talk to itself; outbound from the wasm sandbox is browser-
  contained.
