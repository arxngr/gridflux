
---

# gridflux

`gridflux` is stand for "Window Management",  a window management tool inspired by tiling window managers. Unlike tiling window managers, `gridflux` allows you to manage application windows without relying on a full tiling manager. It's designed to provide flexibility for arranging and managing windows in a non-intrusive way, without imposing a tiling window management style.

This project is mainly targeted for users who prefer to manage windows in a more manual, customizable fashion while still benefiting from automation in tasks such as window resizing, arrangement, and workspace management. ⚙️

# Layout Visualizations

Here are various layout arrangements for windows. The configurations showcase how windows can be arranged in different layouts.

https://github.com/user-attachments/assets/2d46a00c-1b1a-4e33-becc-c7081cafd3d1


### Note 📌:  
Currently, `gridflux` **only supports Linux with X11**. It won’t work on Wayland or other windowing systems, and it’s not available on other operating systems (like macOS or Windows) at this time.

---

## Installation 🛠️

### Dependencies 📦

Make sure you have the following installed:
- X11 development libraries (`libx11-dev`) 🖥️
- X11 utilities like `xprop` 🔧
- Other standard libraries for C development (e.g., `gcc`, `cmake`) 🛠️

### Clone the Repository 📂

```bash
git clone https://github.com/zyxidra/gridflux.git
cd gridflux
chmod +x install.sh
./install.sh
```

### Build the Project ⚙️

If you're using `make`, run the following commands to build `gridflux`:

```bash
make
```

Alternatively, if you're using `CMake`, create a build directory and run the following commands:

```bash
mkdir build
cd build
cmake ..
make
```

---

## Usage 🚀

Once compiled, you can run the `gridflux` executable to manage your windows. For example:

```bash
./gridflux
```

---

## Development 🧑‍💻

This project is open-source, and contributions are welcome. If you'd like to contribute, please fork the repository, create a branch, and submit a pull request with your changes. 🛠️

For further development, you may also want to modify the configuration settings based on your preferred window manager (e.g., X11, Wayland).

---

## Acknowledgements 🙏

- Inspired by tiling window managers. 💡
- Thanks to the X11 community for providing useful tools like `xprop` and `xwininfo`. 👏

---

## Alternative 

- [bspwm](https://github.com/baskerville/bspwm)
- [FancyWM](https://github.com/FancyWM/fancywm) (for Windows User)


---
