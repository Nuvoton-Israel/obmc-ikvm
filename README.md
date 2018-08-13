# obmc ikvm
This is a Virtual Network Computing (VNC) server program.

In order to support some hardware features of Nuvoton's NPCM750, please use our modified [LibVNCServer](https://github.com/Nuvoton-Israel/libvncserver) to build this program.
1) Support Video Capture and Differentiation(VCD) and hwarward 16 bit hextile
    * rfbnpcm750.c
    * rfbnpcm750.h
2) Support USB HID, support Keyboard and Mouse.
    * usb_hid.c
    * usb_hid.h
    * usb_hid_key.h 
3) VNC server main program
    * obmc-ikvm.c
