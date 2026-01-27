# ESP32 3D Print & Font Setup

I didn’t include the fonts due to potential copyright issues. You can choose your own fonts and convert them using:

```bash
fontconvert Geologica-Bold.ttf 14 32 255 > Geologica_Bold14pt8b.h
```

> **Note:** The character range has been modified to include accented characters.

## 3D Print Notes

- There are **two battery holders** in the model. You only need to use **one**.
- The **ESP32 slot** is designed for the **Lolin D32**:
  - Achieves ~80 µA in deep sleep mode.
  - With the original Waveshare ESP32, the lowest I could reach was ~0.5 mA (even after removing the power LED).

## Google Script

You will find the Google Script included. Add it to your Google account here:  
[https://script.google.com/home/](https://script.google.com/home/)

