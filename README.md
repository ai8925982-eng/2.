# SpectrumScope (VST3 + Standalone)

ปลั๊กอินวิเคราะห์เสียงสไตล์เดียวกับภาพที่ให้มา เขียนด้วย C++17 + JUCE 8 + CMake
สร้างได้ 2 รูปแบบจากโค้ดชุดเดียว: **VST3** และ **Standalone (.exe)**

## ฟีเจอร์

- **Spectrum Analyzer** แกนความถี่แบบ log (8 Hz – 16 kHz) แกน dB 0 ถึง -80
- **Phase Scope** แบบครึ่งวงกลม (Left / Right / AntiPhase)
- **มิเตอร์ 3 ช่อง** L / M / R (0 ถึง -39 dB) พร้อมไฟ clip และค่าพีคสูงสุด
- **LF res**: 20 / 40 / 80 Hz (ขนาด FFT 32768 / 16384 / 8192)
- **Weight**: FLAT / A / C
- **Show**: L+R / L / R / MID / SIDE
- **Detect**: RMS / PEAK
- **Response**: 40 / 80 / 160 / 320 / 640 / 1000 ms
- **Freeze**, **Peak Hold**, **Clear**, **Navigate** (เลื่อนมุมมอง), **Zoom**, **Reset Zoom**
- **Peak Hold**: แสดงเส้น Max สีส้มบนกราฟ และขีดค้างค่าพีคบนมิเตอร์
- **Save Data**: บันทึกค่าสเปกตรัมปัจจุบันเป็นไฟล์ CSV (ความถี่, ระดับ, Max, Mem)
- **Graph → Mem**: เก็บกราฟปัจจุบันไว้เป็นเส้นอ้างอิงสีขาว / **Clear Mem**: ลบเส้นอ้างอิง
- ตัวเลขใต้มิเตอร์แสดงค่าพีคสูงสุดเป็น dB (ต่ำสุด -40.0) กด Clear เพื่อรีเซ็ต

ตัวปลั๊กอินไม่แก้ไขสัญญาณเสียง (ผ่านตรง) ใช้เป็นเครื่องมือวัดบนแทร็กหรือ Master ได้

## วิธี build บน Windows

ต้องมี: Visual Studio 2022 (Desktop development with C++), CMake 3.22+, Git

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

ครั้งแรก CMake จะดาวน์โหลด JUCE ให้อัตโนมัติ (ต้องต่ออินเทอร์เน็ต)

ไฟล์ที่ได้:

| ไฟล์ | ตำแหน่ง |
|---|---|
| VST3 | `build/SpectrumScope_artefacts/Release/VST3/SpectrumScope.vst3` |
| Standalone | `build/SpectrumScope_artefacts/Release/Standalone/SpectrumScope.exe` |

## ติดตั้ง VST3

คัดลอกโฟลเดอร์ `SpectrumScope.vst3` ไปที่
`C:\Program Files\Common Files\VST3\` แล้วสแกนปลั๊กอินใหม่ในโปรแกรมที่ใช้ (เช่น Nuendo)

## หมายเหตุเรื่อง Standalone

Standalone จะรับเสียงจากอินพุตของการ์ดเสียง ถ้าได้ยินเสียงหอน (feedback) ให้เปิด
**Options → Mute audio input** ที่เมนูของหน้าต่าง Standalone

## Build บน GitHub

ไฟล์ `.github/workflows/build-windows.yml` จะ build ให้อัตโนมัติทุกครั้งที่ push
แล้วดาวน์โหลด VST3 และ Standalone ได้จากแท็บ **Actions → Artifacts**

> คำแนะนำ: อย่าตั้งชื่อ repository ให้ลงท้ายด้วยจุด (เช่น `2.`) เพราะ Windows สร้างโฟลเดอร์ชื่อนั้นไม่ได้
> workflow นี้ใช้ `git clone` ไปที่ `C:\src` เพื่อเลี่ยงปัญหานี้ แต่ชื่อปกติ เช่น `SpectrumScope` ปลอดภัยที่สุด

## โครงสร้างโปรเจกต์

```
SpectrumScope/
├─ CMakeLists.txt
├─ Source/
│  ├─ PluginProcessor.h/.cpp   (พารามิเตอร์, บัฟเฟอร์เสียง, มิเตอร์)
│  └─ PluginEditor.h/.cpp      (UI, FFT, phase scope, ปุ่มควบคุม)
└─ .github/workflows/build-windows.yml
```
