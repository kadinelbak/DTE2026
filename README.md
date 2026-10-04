# DTE 2026

Hardware for the DTE Designathon project.

## EMG + Stretch Sensor Board

KiCad 8 project in [`hardware/emg_board/`](hardware/emg_board/). Open `emg_board.kicad_pro` in KiCad; the symbol and footprint libraries are project-local, so nothing needs to be installed.

```
hardware/emg_board/
├── emg_board.kicad_pro     project
├── emg_board.kicad_sch     schematic
├── emg_board.kicad_pcb     PCB layout
├── EMG_Board.kicad_sym     project symbol library
├── EMG_Board.pretty/       project footprint library (*.kicad_mod)
├── sym-lib-table           points to ${KIPRJMOD}/EMG_Board.kicad_sym
├── fp-lib-table            points to ${KIPRJMOD}/EMG_Board.pretty
└── docs/                   schematic and PCB previews and screenshots
```

The board runs from a 9 V battery through a switch, a reverse-polarity diode and a 7805 regulator into an ESP32 DevKit (38-pin). It carries:

- an EMG front end (instrumentation amp, 8 Hz high-pass, ~800 Hz low-pass, total gain ~1200) on IO34
- a Wheatstone bridge for two stretch sensors on IO35 / IO33
- a potentiometer on IO32
- three indicator LEDs on IO25 / IO26 / IO27

![Schematic](hardware/emg_board/docs/schematic_preview.png)
![PCB](hardware/emg_board/docs/pcb_preview.png)

### KiCad screenshots

PCB layout:

![PCB layout in KiCad](hardware/emg_board/docs/pcb_screenshot.png)

Schematic:

![Schematic in KiCad](hardware/emg_board/docs/schematic_screenshot.png)
