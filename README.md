# SpectrumScope (VST3 + Standalone)

ปลั๊กอินวิเคราะห์เสียงดีไซน์ดำ-ทองแชมเปญ เขียนด้วย C++17 + JUCE 8 + CMake
สร้างได้ 2 รูปแบบจากโค้ดชุดเดียว: **VST3** และ **Standalone (.exe)**
ตัวปลั๊กอินไม่แก้ไขสัญญาณเสียง (ผ่านตรง) ใช้เป็นเครื่องมือวัดบนแทร็กหรือ Master ได้

## หน้าตาและการทำงาน

**กราฟความถี่ (บน)** เป็นแท่งแบบอ็อกเทฟ อ่านง่ายกว่ากราฟเส้นละเอียด
- สีของแท่งบอกย่านความถี่: เบส = ส้ม, กลาง = ทอง/เขียวมิ้นต์, แหลม = ฟ้า/ม่วง
- **Bands**: 1/3 อ็อกเทฟ (ค่าเริ่มต้น, อ่านง่ายสุด) / 1/6 / 1/12
- แถบสี **PHASE** ใต้กราฟ บอกความสัมพันธ์เฟสของแต่ละแท่ง (เขียว = เฟสตรงกัน, เหลือง = กว้าง, แดง = ตรงข้ามเฟส)

**Phase Scope (ล่างซ้าย)** เชื่อมกับกราฟบน
- **ลากเมาส์บนกราฟความถี่** เพื่อเลือกช่วง: Phase Scope จะแสดงเฉพาะสัญญาณในช่วงนั้น
  จุดบนสโคปเปลี่ยนสีตามย่านที่เลือก แท่งนอกช่วงจะจางลง
- **ดับเบิลคลิก** บนกราฟเพื่อล้างการเลือก (กลับเป็นช่วงเต็ม)
- มีแถบ **Correlation** (-1 ถึง +1) และโซนแอนติเฟสสีแดงจาง ๆ

**ปุ่มควบคุม**
- **Show**: L+R / L / R / MID / SIDE
- **Detect**: RMS / PEAK, **Response**: 40–1000 ms
- **Weight**: FLAT / A / C
- **Freeze**, **Peak Hold** (ขีดทองค้างค่าสูงสุดบนแท่งและมิเตอร์), **Clear**
- **Save Data**: บันทึกค่าเป็น CSV, **Graph -> Mem**: เก็บเส้นอ้างอิงสีขาว, **Clear Mem**: ลบเส้นอ้างอิง

**มิเตอร์ L / M / R** ช่วง 0 ถึง -39 dB พร้อมไฟ clip และช่องแสดงค่าพีค (ต่ำสุด -40.0)

หน้าต่างย่อ/ขยายได้ (รักษาสัดส่วนอัตโนมัติ)

## วิธี build บน Windows

ต้องมี: Visual Studio 2022 (Desktop development with C++), CMake 3.22+, Git

```bat
cmake -B build -A x64
cmake --build build --config Release --parallel
```

ครั้งแรก CMake จะดาวน์โหลด JUCE ให้อัตโนมัติ (ต้องต่ออินเทอร์เน็ต)

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

> อย่าตั้งชื่อ repository ให้ลงท้ายด้วยจุด (เช่น `2.`) เพราะ Windows สร้างโฟลเดอร์ชื่อนั้นไม่ได้
> workflow นี้ใช้ `git clone` ไปที่ `C:\src` เพื่อเลี่ยงปัญหานี้ แต่ชื่อปกติ เช่น `SpectrumScope` ปลอดภัยที่สุด

## โครงสร้างโปรเจกต์

```
SpectrumScope/
├─ CMakeLists.txt
├─ Source/
│  ├─ PluginProcessor.h/.cpp   (พารามิเตอร์, บัฟเฟอร์เสียง, มิเตอร์)
│  └─ PluginEditor.h/.cpp      (UI, FFT, แถบอ็อกเทฟ, phase scope, ปุ่มควบคุม)
└─ .github/workflows/build-windows.yml
```
