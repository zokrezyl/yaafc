# yaafc-on-riscv64 demo (yemu)

Boots a tiny Alpine riscv64 VM with the cross-compiled `yaafc` +
`yaafc-frontend` baked in. Same shape as yaapp's `tmp/yemu/`, but
yaafc is fully static so the image only needs Alpine's base
userland — there's nothing to provision on first boot.

## One-time setup (host)

```sh
sudo apt-get install -y \
    gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
    qemu-system-misc e2fsprogs util-linux curl \
    bc bison flex libssl-dev libelf-dev cpio rsync
```

You also need passwordless `sudo` for `losetup` / `mount` (used by
`build-image.sh` to drop files into the rootfs).

## Build the image

```sh
# One target chains everything:
#   1. cross-compile yaafc for riscv64
#   2. stage the host-runnable build-deploy/ tree
#   3. build opensbi locally (build-tools/3rdparty/opensbi/)
#   4. build the linux kernel locally (build-tools/3rdparty/linux/)
#   5. fetch alpine minirootfs from dl-cdn.alpinelinux.org
#   6. bake the bootable ext4 image
#
# First run takes ~15 minutes (kernel build). Subsequent runs are
# instant — the artefacts are stamped in build-yemu-release/3rdparty/.
make -C ../../.. build-yemu-release
```

Outputs land under `build-yemu-release/` at the repo root:

```
build-yemu-release/
├── alpine-rootfs.img        256 MiB rootfs (with /opt/git-yaafc/ injected)
├── kernel-riscv64.bin       → 3rdparty/linux/lib/kernel-riscv64.bin
├── opensbi-fw_dynamic.bin   → 3rdparty/opensbi/lib/opensbi-fw_dynamic.bin
├── opensbi-fw_jump.elf      → 3rdparty/opensbi/lib/opensbi-fw_jump.elf
├── 3rdparty/{linux,opensbi}/lib/  extracted recipe outputs
├── 3rdparty-cache/                produced tarballs
└── work/                          sudo loop-mount scratch
```

No more `yetty/releases/download` URLs — the kernel + opensbi come from
local recipes under `build-tools/3rdparty/{linux,opensbi}/`, the alpine
minirootfs comes straight from `dl-cdn.alpinelinux.org`. Same fetcher
pattern as yetty's `build-tools/3rdparty/<lib>/`: each recipe has a
`version` file + `_build.sh` that produces a tarball under
`3rdparty-cache/`, then build-image.sh extracts into `3rdparty/<lib>/`
with a `.fetched-<version>` stamp to skip on subsequent runs. Bump the
`version` file to force a rebuild.

The yaafc payload comes from `build-deploy/` (staged from
`scenarios/git-yaafc/{yaafc.yaml,frontend/static,deploy/run.sh.tmpl}`
by `make build-deploy`); inside the VM it lands at `/opt/git-yaafc/`
with the riscv64 yaafc binary swapped in.

## Boot it

```sh
./run-vm.sh
```

The launcher (baked at `/opt/git-yaafc/run.sh` inside the image)
mounts /proc /sys /dev, brings up slirp net, starts a control yaafc
on `127.0.0.1:8800`, then POSTs `/create` + `/invoke
mesh_store_reconcile_from_config` so the full mesh comes up:
gateway on `0.0.0.0:8080` plus 11 backends on `127.0.0.1:820X`.
From the host:

| Service        | Guest port | Host port forward |
|----------------|------------|-------------------|
| Gateway (HTTP) | 8080       | 18080             |

Open `http://127.0.0.1:18080/login` in a browser to hit the C
gateway (which serves the HTML UI directly and forwards everything
to the backends over yrpc).

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
# 1. Bake the rootfs (chains riscv64 build + build-deploy + build-image.sh).
#    Needs sudo for losetup/mount.
make -C ../../.. build-yemu-release

# 2. Compile tinyemu to wasm and stage the VM assets next to the bundle.
make -C ../../.. build-webasm-yemu-release

# 3. Serve locally
python3 ../../../build-webasm-yemu-release/serve.py 8000 ../../../build-webasm-yemu-release
# open http://127.0.0.1:8000/
```

The Emscripten SDK has to live at `$HOME/.local/emsdk` (yetty's
convention — see `yetty/build-tools/install-emscripten.sh`); the
build script auto-prepends it to `$PATH`. Override with `EMSDK=…`.

Output layout (`build-webasm-yemu-release/`):

| File                          | What                                     |
|-------------------------------|------------------------------------------|
| `yaafc-yemu.js`               | emscripten loader (~125 KB)              |
| `yaafc-yemu.wasm`             | compiled tinyemu + slirp + temu (~220 KB)|
| `index.html`                  | thin iframe wrapper                      |
| `tinyemu-iframe.html`         | main UI — boot overlay, slirp shim, netlog |
| `yaafc.cfg`                   | tinyemu VM config (MEMFS paths)          |
| `assets/kernel-riscv64.bin`   | yetty linux kernel                       |
| `assets/opensbi-fw_jump.elf`  | yetty opensbi (tinyemu-compatible ELF)   |
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
