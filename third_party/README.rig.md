# Vendored headers for the rig/gb20x work

| Directory | Files | Origin | License |
|---|---|---|---|
| nvidia-open-gpu-doc/ | clcab5.h clcec0.h clcec0qmd.h clca6f.h | Mesa `src/nouveau/headers/nvidia/classes/` at 77781ae80d72d8202b73222ca181f5261027c5ce (copies of NVIDIA open-gpu-doc class headers) | MIT (NVIDIA), header text kept unmodified |
| wsl2-dxgkrnl/ | d3dkmthk.h | microsoft/WSL2-Linux-Kernel `include/uapi/misc/d3dkmthk.h` at d504d40ab83839cb61942a812a99d183c42227b9 (tag linux-msft-wsl-6.18.54.1) | GPL-2.0 WITH Linux-syscall-note (uapi header; user space may include it) |

Used only by the host-side target table, its unit tests and the WSL2 /dev/dxg backend.
The GB10 build (omegatool) does not include any of these files.

SHA-256:
```
a5984aa27589ed116c5739e4fbaaa00f325829cb70bba71607adcb2ac3196f15  nvidia-open-gpu-doc/clca6f.h
3ab945632771609ae7643b5db1df2f91eb51745244267dd8766b6d43076a84bc  nvidia-open-gpu-doc/clcab5.h
b87d096c5a9b477af15e5b9742a51d119ee26b1259ae03297eaf140d5bd039c5  nvidia-open-gpu-doc/clcec0.h
6ba3a6d4742a722346c725a1c95114a62738c9a165fe18ec1cdd84c2511b4cfc  nvidia-open-gpu-doc/clcec0qmd.h
00d7b0aaea43d0e2a1f4b06f4d3f55acd023ed17407567b5ec1552c99d54ccef  wsl2-dxgkrnl/d3dkmthk.h
```
