# PhaseLatch

PhaseLatch – 6502-Interfaced High-Speed ADC for Experimental SDR

PhaseLatch is an open-source hardware project that explores how far a classic MOS 6502-class CPU can be pushed into modern signal-processing territory. The board implements a dual-channel, 10-bit, 20 MSPS analog-to-digital converter with a straight binary parallel output, allowing the ADC to be memory-mapped directly onto a 6502 (8 bit) data bus.

Unlike typical SDR designs that rely on high-performance microcontrollers or FPGAs, PhaseLatch intentionally interfaces high-speed conversion hardware with a severely resource-constrained 8-bit CPU. This makes it a platform for experimenting with timing limits, bus contention, memory expansion, and minimalist DSP techniques such as Goertzel tone detection executed entirely on the 6502.

The board is designed as a modular Arduino-shield-style form factor and is intended to stack with the PhaseLoom IQ mixer and the 65uino 6502 system. Direct access to the CPU address, data, and control signals enables single-instruction ADC reads and deterministic sampling under software control.

PhaseLatch is not intended to achieve full theoretical ADC bandwidth on a 6502. Instead, it serves as a practical exploration of real-world limitations, architectural trade-offs, and creative signal-processing approaches on vintage computing hardware.

Though noone says it can't be used with a more powerful platform.. But.. Spoilers :)

<img src="PhaseLatch.jpg" alt="PhaseLatch board photo" style="width: 440px;">

## Purchase

Interested in owning a PhaseLatch?  
[Buy one here](https://www.imania.dk/index.php?cPath=204&sort=5a&language=en).

## How to Use
[6502 source code still lives over on the 65uino project](https://github.com/AndersBNielsen/65uino/)

The main 65uino firmware currently comes with support for the PhaseLatch built in. Time of writing, use the SDR branch. 

## Getting a PCB

## Production & Manufacturing
- The `jlcpcb/gerber/` folder contains all necessary files for PCB fabrication.
- The `production_files/` folder includes:
  - `BOM-PhaseLatch.csv`: Bill of Materials
  - `CPL-PhaseLatch.csv`: Component Placement List
  - `GERBER-PhaseLatch.zip`: Zipped Gerber files for easy upload

This project is kindly sponsored by JLCPCB. They offer cheap, professional looking PCBs and super fast delivery.

Step 1: Get the gerber file zip package from the /hardware folder

Step 2: Upload to JLCPCB [https://jlcpcb.com/?from=Anders_N](https://jlcpcb.com/?from=Anders_N)

<img src="https://github.com/AndersBNielsen/65uino/blob/main/images/upload.png?raw=true" alt="Upload" style="width: 220px;">

Step 3: Pick your color, surface finish and order.

<img src="https://github.com/AndersBNielsen/65uino/blob/main/images/settings.png?raw=true" alt="Select settings" style="width: 220px;">

<img src="https://github.com/AndersBNielsen/65uino/blob/main/images/save.png?raw=true" alt="Save your choice" style="width: 220px;">


You can use these affiliate links to get a board for $2 and also get $54 worth of New User Coupons at: https://jlcpcb.com/?from=Anders_N

And in case you also want to order a 3D-printed case you can use this link. 
How to Get a $7 3D Printing Coupon: [https://3d.jlcpcb.com/?from=Anders3DP](https://jlc3dp.com/?from=Anders_N)


---

For more details, technical explanations, and the full story behind PhaseLoom, check out the project video and documentation. Special thanks to JLCPCB and Fnirsi for supporting the development of this project!

