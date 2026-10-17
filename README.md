# Ollama DOCK

An English terminal menu for managing an existing Ollama installation on Linux. This version uses the **official `ollama` command-line program only** for model listing, downloading, running, stopping/inspection and removal. It does **not** call the Ollama HTTP API.
## Project and license

- **Author:** Bahadir Altintas
- **GitHub:** https://github.com/bahadiraltintas
- **License:** [MIT](LICENSE)


## Features

1. List installed models (`ollama list`)
2. Search the official Ollama website, choose a listed model by number, and download it with live CLI progress (`ollama pull MODEL`)
3. Choose an installed model and open interactive chat in a separate terminal (`ollama run MODEL`); the manager returns immediately to its menu
4. Show running models (`ollama ps`)
5. Remove a model after explicit confirmation (`ollama rm MODEL`)
6. Display RAM and NVIDIA GPU information (`free -h`, `nvidia-smi` when installed)
7. Show general model examples and guidance
8. Open the official Ollama model library (`https://ollama.com/library`)

## Requirements

- Linux / Ubuntu
- Ollama installed and available as `ollama` in `PATH`
- GCC and Make to compile
- `curl` and an internet connection for fetching public model links from the official website
- `xdg-open` for opening the official model catalog from the menu
- `nvidia-smi` is optional; GPU information is only shown when available

## Build and run

```bash
sudo apt update
sudo apt install build-essential
make
./ollama-dock
```

Optional install:

```bash
sudo make install
ollama-dock
```

## Downloading from the official library

Choose menu option 2 to fetch model-family names from the official `https://ollama.com/library` page. The program builds its own numbered pages locally (20 models per page), so **N/P** navigation does not depend on the website’s dynamic “Show more” behavior. Use **S** to filter the loaded names, **A** to clear the filter and show all names, or choose a number to run `ollama pull MODEL` and display CLI progress. This uses `curl` to read the public website HTML, not the Ollama HTTP API. Because the source is public HTML, a future website markup change may require an update. Option 8 opens the same official library as a browser fallback. For chat, menu option 3 lists locally installed models and requires choosing one by number.

## Design notes

- No Ollama HTTP API calls are made. Website catalog lookup uses `curl` against the public website.
- Ollama operations are launched as child processes with argument arrays rather than shell-concatenated user input.
- Model removal requires typing `DELETE` exactly.
- Model recommendations are generic examples; check model size, quantization and hardware requirements before downloading.


## Chat in a separate terminal

Menu option 3 launches the selected installed model in a new terminal window, keeping the manager available for other menu actions. It detects common terminal emulators (GNOME Terminal, Konsole, XFCE Terminal, MATE Terminal, xterm, or `x-terminal-emulator`). If none is available (for example, on a headless system), install a terminal emulator or run `ollama run MODEL` in a second terminal manually. Chatting alongside other workloads may increase RAM/VRAM use.
